# TopOpt — Théorie complète de l'algorithme

*Document de référence : la théorie derrière le solveur de topology optimization
multiphysique TopOpt — hypothèses, sacrifices assumés, fonctionnement détaillé,
théorèmes invoqués. Public : ingénieur/étudiant avancé (FEM, calcul scientifique,
optimisation). Le code fait foi : chaque formule ci-dessous correspond à une
implémentation identifiée (`src/…`), chaque chiffre à une mesure du repo
(tests, rapports de phase, ADR). Rien n'est extrapolé.*

Notation : `ρ` (ou `γ` en contexte fluide) champ de densité, `ρ̃` densité filtrée,
`ρ̄` densité projetée, `U/T/w=(u,p)` champs d'état, `λ` champs adjoints,
`J` objectif, `g_k` contraintes, `K/A` opérateurs discrets, `∂` dérivée partielle
(à état figé), `d` dérivée totale.

---

## 0. En une phrase

TopOpt trouve **où mettre la matière** dans un volume donné pour optimiser une
performance (rigidité, masse, refroidissement, dissipation) sous contraintes, en
laissant un **algorithme de gradient** découvrir la topologie. Toute la théorie
ci-dessous répond à trois questions : pourquoi ce problème est difficile (§1),
comment on le rend soluble (§2-§4, au prix de sacrifices explicites), et comment
on calcule et on **prouve** le gradient qui pilote tout (§5-§8).

---

## 1. Le problème d'optimisation topologique

### 1.1 Formulation continue

Le problème idéal est un problème de **design binaire** : trouver le sous-domaine
matière `Ω ⊂ D` (D = domaine de design) minimisant un objectif sous les EDP de la
physique et des contraintes de ressources :

```
min_{Ω ⊂ D}   J(Ω, champs(Ω))
s.t.          EDP(Ω, champs) = 0          (équilibre, chaleur, Stokes)
              |Ω| ≤ V_max,  g_k(Ω) ≤ 0
```

La **méthode des densités** remplace l'indicatrice binaire `χ_Ω ∈ {0,1}` par un
champ continu `ρ : D → [0,1]`, ce qui rend le problème différentiable :

```
min_ρ   J(ρ, champs)
s.t.    R(ρ, champs) = 0                  (résidus FEM des physiques)
        g_k(ρ, champs) ≤ 0                (volume, von Mises, T_max, …)
        0 ≤ ρ_e ≤ 1   pour chaque élément e
```

C'est la formulation effectivement résolue par le solveur (une variable `ρ_e`
par élément de la grille structurée, cf. `ProblemSpec`, `docs/INPUT_LANGUAGE.md`).

### 1.2 Pourquoi le problème est mal posé

Le problème binaire continu **n'a en général pas de solution** : c'est le résultat
fondateur du domaine. Intuition : à volume fixé, remplacer une barre épaisse par
deux barres deux fois plus fines est (presque) toujours plus rigide ; en itérant,
la suite minimisante développe une microstructure infiniment fine — des
oscillations de plus en plus rapides de `χ_Ω` — qui ne converge vers aucun domaine
admissible. La borne inférieure de l'objectif n'est atteinte que par un **matériau
composite homogénéisé**, hors de l'espace de design (Bendsøe & Kikuchi 1988 ;
c'est ce qui a motivé l'approche par homogénéisation).

Au niveau discret, cette pathologie se manifeste par deux symptômes bien connus :

- **Damier (checkerboard)** : alternance `ρ=0/ρ=1` cellule par cellule. Ce n'est
  pas une microstructure « optimale » découverte par l'algorithme mais un artefact
  numérique : sur des éléments à interpolation linéaire (Q1/H8), le motif en damier
  a une rigidité surestimée par la FEM (défaut d'approximation, apparenté aux modes
  parasites — même famille de pathologies que l'inf-sup de Stokes, §4.6).
- **Dépendance au maillage** : raffiner la grille ne raffine pas la *précision* du
  même design, il produit un design *différent*, avec des membres plus fins — la
  suite minimisante ci-dessus, tronquée à l'échelle `h`. Un résultat qui dépend de
  la résolution n'est pas exploitable en ingénierie.

Ces deux symptômes ont été observés et traités dans le projet dès qu'attendus
(LL-LIT-005, LL-LIT-006 dans `archive/orchestration/LESSONS_LEARNED.md`).

### 1.3 Relaxation vs restriction

Deux familles de remèdes rendent le problème bien posé :

1. **Relaxation** : élargir l'espace de design pour inclure la limite des suites
   minimisantes — les composites. C'est l'approche par homogénéisation (Bendsøe &
   Kikuchi 1988) : la variable devient une microstructure paramétrée (fraction,
   orientation). Mathématiquement propre (l'existence est restaurée), mais le
   résultat est un champ de matériau composite, difficile à fabriquer, et la
   machinerie (tenseurs homogénéisés) est lourde.

2. **Restriction** : garder le design binaire visé, mais **restreindre l'espace
   admissible** aux champs de variation bornée — concrètement, imposer une taille
   minimale de feature via un filtre (Bourdin 2001 ; Lazarov & Sigmund 2011). La
   compacité de l'espace restreint restaure l'existence d'une solution, et le
   design reste (quasi) 0/1.

**TopOpt choisit la restriction** : SIMP (§2) + filtre de Helmholtz à rayon
physique (§3.1) + projection de Heaviside (§3.3). Le sacrifice : la taille de
feature minimale est une donnée d'entrée (`filter.radius_mm`), pas un résultat —
l'optimiseur ne peut pas découvrir de détail plus fin que le rayon choisi.

### 1.4 Non-convexité : on trouve des optima LOCAUX

Même régularisé, le problème reste **non convexe** : l'état (U, T, u) dépend
non-linéairement de ρ à travers `K(ρ)⁻¹`, et la pénalisation SIMP (§2) ajoute
délibérément de la non-convexité pour chasser le gris. Conséquences assumées :

- Les méthodes de gradient (MMA, OC) convergent vers un **minimum local**,
  dépendant de l'initialisation (ici : champ uniforme `ρ = volfrac`), du chemin de
  continuation (p, β, α_max) et des paramètres du filtre. Aucune garantie de
  globalité n'est revendiquée nulle part dans le projet, et aucune n'existe pour
  cette classe de problèmes en général.
- Les **continuations** (§2.5, §3.3) ne sont pas des raffinements cosmétiques :
  ce sont des heuristiques de convexification progressive. On résout d'abord un
  problème « mou » (p≈1 : quasi-convexe pour la compliance, β=1 : projection
  quasi-identité), puis on suit sa solution pendant qu'on durcit le problème. Cela
  ne supprime pas les minima locaux ; cela sélectionne empiriquement de meilleurs
  bassins d'attraction.
- Symétriser explicitement le design quand le problème est symétrique évite des
  minima locaux asymétriques « coudés » — leçon mesurée sur le diffuseur
  Borrvall-Petersson (LL-011).

C'est le premier sacrifice structurel du solveur, et le plus fondamental :
**la qualité d'un design TopOpt est celle d'un optimum local bien choisi**, jugée
a posteriori sur la physique (validations §8), pas sur un certificat d'optimalité.

---

## 2. Interpolation matériau : SIMP

### 2.1 La loi

SIMP (Solid Isotropic Material with Penalization) interpole le module de Young :

```
E(ρ) = E_min + ρ^p (E_0 − E_min),        p = 3 (défaut)
```

(`SIMP3D::youngModulus`, `ThermoElasticAdjoint`, `AxiStressAdjoint`). En v3
(fluide-thermique), la convention s'inverse : `γ=1` = fluide, et c'est la
**solidité** `s = 1−γ` qui porte la raideur, `E(γ) = E_min + (E_0−E_min)(1−γ)^p`
(`TripleAdjoint.hpp`) — le solide est rigide là où le fluide est absent.

### 2.2 Pourquoi pénaliser

Une densité intermédiaire `ρ=0.5` n'a pas de sens physique en soi. La pénalisation
`ρ^p` rend le gris **inefficace** : à p=3, un élément à `ρ=0.5` coûte 50 % de la
masse mais ne fournit que 12.5 % de la rigidité. Sous contrainte de volume,
l'optimiseur a donc intérêt à polariser le champ vers 0/1 — la binarisation est
obtenue *par l'économie du problème*, pas par une contrainte combinatoire. Notons
la mécanique exacte : SIMP ne binarise pas par magie, il rend le gris
**sous-optimal** ; c'est la projection de Heaviside (§3.3) qui achève la
binarisation effective.

### 2.3 Justification physique : l'interprétation composite (Bendsøe & Sigmund)

La pénalisation pose une question de légitimité : un élément à `ρ=0.5, E=0.125·E_0`
correspond-il à un matériau réalisable ? Bendsøe & Sigmund (1999, repris dans le
livre 2003) répondent par les **bornes de Hashin-Shtrikman** : la loi `E = ρ^p E_0`
reste au-dessous de la borne supérieure des composites isotropes à fraction
volumique ρ (donc réalisable par une microstructure) si p est assez grand.
En 3D avec ν = 1/3, la condition est :

```
p ≥ max( 15(1−ν)/(7−5ν) , 3/2 · (1−ν)/(1−2ν) )  =  3     (3D, ν = 1/3)
```

D'où le **p = 3 canonique** utilisé dans tout le projet : c'est la plus petite
pénalisation à la fois suffisante pour polariser le design et physiquement
interprétable (chaque densité intermédiaire transitoire correspond à un composite
admissible). En 2D la borne est plus basse (p ≥ 3 reste le choix usuel).

### 2.4 E_min : plancher numérique, pas physique

`E_min = 1e-4·E_0` n'est pas un paramètre matériau : c'est un plancher qui évite
une matrice singulière dans le vide. Sa valeur est dictée par le solveur linéaire
(leçon mesurée LL-006) : avec `E_min = 1e-9` (toléré par un solveur direct), le
contraste de rigidité ~1e9 rend le **CG Jacobi float32 GPU non convergent**
(compliance oscillante, valeurs négatives). À `1e-4`, la boucle TO 3D converge
proprement (compliance 230 → 18.5, monotone, MBB). Sacrifice associé : le « vide »
conserve une rigidité résiduelle 1e-4·E_0 — négligeable pour la compliance, mais à
garder en tête pour toute lecture fine des champs dans le vide.

### 2.5 Continuation de p

Démarrer directement à p=3 fige trop tôt la topologie (le problème est déjà très
non convexe). La continuation augmente p progressivement de 1 vers 3
(`ContinuationPolicy`, rampe sur `rampIters`). En multi-grille, la politique est
un choix de design par pièce :

- `Inherit` (défaut) : continuation complète 1→3 sur le niveau grossier seulement,
  p=3 ensuite — le standard grande échelle, économique.
- `Restart` : continuation à chaque niveau — liberté topologique maximale, pour
  les features fines qui doivent nucléer tard (canaux de refroidissement).
- `Custom` : cible de p par niveau.

Piège documenté (Phase 3, §4.4 du rapport) : la continuation détermine si
l'optimiseur *peut nucléer* une feature fine ; le rayon du filtre (mm) détermine
si elle *peut exister*. Pour des canaux fins, il faut les deux.

Attention pratique mesurée (LL-008) : le filtre Helmholtz peut produire des
undershoots `ρ̃ < 0` ; `pow(négatif, p non entier)` = NaN → clamper ρ à [0,1]
avant tout `pow` fractionnaire, et borner toute bissection.

### 2.6 Les autres lois d'interpolation du solveur

Chaque physique a sa propre interpolation, chacune avec sa justification :

| Champ | Loi | Où | Pourquoi cette forme |
|---|---|---|---|
| Young E | `E_min + ρ^p ΔE` (p=3) | élasticité | pénalisation SIMP (§2.2-2.3) |
| Conductivité k | `k_s + (k_f − k_s)·γ` (linéaire) | CHT | les deux phases conduisent ; pas besoin de pénaliser un champ qui n'est pas la variable de rigidité du design (`CHTSolver::kInterp`) |
| Inverse-perméabilité α | `α_max + (α_min − α_max)·γ(1+q)/(γ+q)`, q=0.1 | Brinkman | interpolation **convexe** de Borrvall-Petersson 2003 (§4.5) : q petit rend α(γ) très raide près de γ=0 (le solide bloque vite) tout en gardant dα/dγ exploitable |
| Conductivité (v2 3D) | `k_min + ρ^q Δk` (q=3) | thermo-élastique | même logique SIMP côté conduction (`ThermoElasticAdjoint::Material`) |

---

## 3. Régularisation : filtre de Helmholtz + projection de Heaviside

### 3.1 Le filtre de Helmholtz (Lazarov & Sigmund 2011)

Au lieu du filtre de convolution classique (moyenne pondérée sur un voisinage —
coûteux et pénible aux bords), la densité filtrée est définie comme la solution
d'une **EDP elliptique de réaction-diffusion** :

```
−r_len² ∇²ρ̃ + ρ̃ = ρ     sur D,      ∂ρ̃/∂n = 0 au bord
```

Propriétés qui en font le bon choix ici :

- **Équivalence** : la solution est la convolution de ρ par la fonction de Green
  de l'opérateur (noyau exponentiel) ; le lien avec le rayon R du filtre à cône
  classique est `r_len = R / (2√3)` — exactement la constante du code
  (`Helmholtz3D.hpp` : `rlen = radiusCells/(2√3)`).
- **Coût** : c'est un Laplacien scalaire SPD — résolu par le même CG matrix-free
  GPU que la physique, sans structure de voisinage explicite.
- **Conservativité** : avec les BC de Neumann, le filtre **conserve la moyenne**
  (`Σρ̃ = Σρ`). Conséquence pratique mesurée (LL-007) : la contrainte de volume
  peut être vérifiée sur ρ directement, on ne filtre qu'une fois par itération
  (73 s → 51.7 s sur le cas 60×20×20).
- **Auto-adjonction** : l'opérateur est symétrique, donc la transposée du filtre
  dans la chaîne de gradient (§3.4) est le filtre lui-même.

### 3.2 Rayon physique et indépendance au maillage

Le point de maturité décisif (Phase 3) : le rayon est exprimé en **millimètres**,
pas en cellules (`HelmholtzFilterPhysical` : `radiusCells = radius_mm/cellSize_mm`).
La taille minimale de feature devient une grandeur physique invariante : raffiner
le maillage (h↓) augmente le rayon en cellules dans la même proportion, et **le
design converge quand le maillage se raffine** au lieu de dériver vers des membres
plus fins. Vérifié en Phase 3 : topologies cohérentes à 32³/64³/128³ à rayon fixe
r = 2 mm, volume tenu à 0.3000 partout (rapport Phase 3, §« mesh independence »).
C'est l'implémentation concrète de la *restriction* du §1.3 — le mécanisme par
lequel le problème discret est bien posé.

### 3.3 Projection de Heaviside (Wang, Lazarov & Sigmund 2011)

Le filtre régularise mais floute : il crée une bande grise de largeur ~r autour de
chaque interface. Pour récupérer un design net, on projette la densité filtrée par
une Heaviside régularisée :

```
ρ̄ = [tanh(βη) + tanh(β(ρ̃ − η))] / [tanh(βη) + tanh(β(1 − η))]
```

avec η = 0.5 (seuil) et β la raideur (`cooling_jacket.cpp::heaviside`). β suit une
**continuation** — dans le démonstrateur cooling jacket : β = 1 (it≤12), 2, 4, 8,
puis 16 (it>48). À β=1 la projection est quasi affine (le problème reste doux) ;
à β=16 c'est un quasi-échelon (design binaire). Augmenter β brutalement casserait
la continuité du chemin d'optimisation (le gradient `H'(ρ̃)` devient un Dirac
autour de η).

Remarque d'honnêteté : le schéma **robuste** de Wang et al. 2011 (optimiser
simultanément les designs érodé/nominal/dilaté, η ∈ {0.3, 0.5, 0.7}, pour garantir
la tenue aux variations de fabrication) n'est **pas implémenté** — seule la
projection nominale η=0.5 l'est. De plus, β a dû être **plafonné à 4** sur le
démonstrateur `nozzle_profiled` (instabilité de la projection en présence de la
contrainte de stress — documenté dans le commit du démonstrateur). Deux limites
assumées.

### 3.4 La chaîne de design et son gradient

La variable d'optimisation est ρ (celle que MMA voit) ; la physique voit ρ̄ :

```
ρ  ──filtre W──►  ρ̃  ──Heaviside H_β──►  ρ̄  ──►  physique
```

Le gradient remonte par règle de chaîne (`cooling_jacket.cpp`, en-tête) :

```
dJ/dρ = Wᵀ ( H'_β(ρ̃) ∘ dJ/dρ̄ )        (∘ = produit élément par élément)
```

avec `Wᵀ = W` (filtre auto-adjoint, §3.1). Tous les gradients adjoints du §5 sont
calculés en ρ̄ puis remontés ainsi.

### 3.5 Le compromis filtrage/netteté et la fraction grise

Filtre et projection tirent en sens opposés : le filtre impose une largeur
minimale (et donc du gris), la projection élimine le gris (et peut sous-résoudre
la largeur imposée). Le diagnostic quantitatif est la **fraction grise**
(proportion d'éléments loin de 0 et de 1). Mesures du repo : cooling jacket,
gris 1.0 → **0.056** en fin de continuation β (rapport Phase 5) ; diffuseur
Borrvall-Petersson, gris 2.5 % (commit de reproduction). Un design finement
binaire *et* mesh-independent est la preuve que le couple (r_mm, schedule β) est
bien réglé ; du gris résiduel élevé signale un conflit filtre/projection ou une
continuation trop courte.

---

## 4. Les physiques : hypothèses et sacrifices explicites

Cadre commun à tous les blocs : grille structurée régulière, élément trilinéaire
(hexaèdre H8 en 3D — 24 DOF élastiques, 8 thermiques, 32 Stokes ; quadrilatère Q4
en axisymétrique r-z), quadrature de Gauss 2×2(×2), **régime stationnaire
partout**. Les oracles et adjoints tournent en CPU double précision (Eigen direct,
ADR-018) ; la production 3D élastique tourne en GPU float32 matrix-free.

### 4.1 Élasticité linéaire

```
−∇·σ = f,   σ = D(ρ̄) ε,   ε = ½(∇u + ∇uᵀ)        →    K(ρ̄) U = F
```

Hypothèses : petits déplacements, petites déformations, matériau isotrope
linéaire, chargement statique. **Sacrifices** :

- **Pas de flambage** : la compliance linéaire ne voit pas la stabilité. Or les
  designs de TO — treillis élancés en compression — sont précisément la géométrie
  la plus exposée au flambage. Un design optimal en compliance peut être
  critique en flambement ; cette vérification est *hors périmètre* du solveur et
  doit être faite a posteriori (analyse aux valeurs propres géométriques, non
  implémentée).
- Pas de non-linéarité géométrique ni matérielle (plasticité), pas de dynamique
  (modes propres, impact), pas de fatigue. La contrainte von Mises (§6.2) est une
  contrainte statique de premier dépassement.

### 4.2 Conduction thermique

```
−∇·(k(ρ̄)∇T) = Q        →    K_t(ρ̄) T = Q
```

Laplacien pondéré par la conductivité interpolée. Validé au plancher machine sur
champ uniforme et par la plaque à gradient linéaire (8.9e-7, rapport Phase 4).
Hypothèses : conduction pure stationnaire, k isotrope, pas de rayonnement, pas de
résistance de contact.

### 4.3 Couplage thermo-élastique — one-way

La température impose une déformation libre `ε_th = α_th (T − T_ref) m`
(m = [1,1,1,0,0,0]ᵀ en Voigt), assemblée en **force thermique équivalente** :

```
F_th = Σ_e E_e(ρ) · α_th · C_e · (T_e − T_ref),      K_e(ρ) U = F_mech + F_th
```

(`ThermoElasticCoupling`, `H8Element::thermalCoupling`). Point structurel : F_th
dépend de ρ *via E_e* — l'oublier dans le gradient est le piège classique, vérifié
par le gate DF (§8.3).

**Sacrifice : couplage unidirectionnel.** T pilote U ; U ne pilote jamais T. Sont
donc négligés : le couplage thermo-élastique inverse (chaleur de déformation),
la dissipation mécanique, toute dépendance des propriétés à T (E(T), k(T)).
Hypothèse valide pour des structures chauffées quasi statiquement ; fausse pour
l'amortissement thermo-élastique ou les couplages forts. Validé par le patch test
de dilatation libre : contrainte nulle sous ΔT uniforme à 7.4e-6 (rapport P4).

### 4.4 Fluide : Stokes, PAS Navier-Stokes — le sacrifice fondateur du bloc fluide

```
−μ∇²u + ∇p = f,     ∇·u = 0
```

(`StokesSolver`). L'équation de quantité de mouvement **omet le terme convectif
ρ_f (u·∇)u** : c'est la limite Re ≪ 1 (écoulement rampant). Conséquences :

- L'opérateur est linéaire et symétrique (modulo stabilisation) : un seul solve
  par itération TO, adjoint = transposé — toute la machinerie du §5 en dépend.
- **Ce qu'on sacrifie** : inertie du fluide, décollements, recirculations
  inertiancées, effets d'entrée, turbulence, pertes de charge quadratiques en
  débit. Le domaine de validité est celui des écoulements très visqueux ou très
  lents (micro-canaux, régime de lubrification). Pour un vrai circuit de
  refroidissement à Reynolds modéré/élevé, les designs restent des *a priori*
  topologiques à re-vérifier sous Navier-Stokes (non implémenté — différé
  documenté, direction « B — Recherche » du handoff Phase 5→6).
- Stationnaire : pas d'instationnarité, pas de battement.

Ce sacrifice est assumé parce qu'il est **cohérent avec l'usage TO** : au stade du
design topologique, on cherche *où* faire passer les canaux ; l'écoulement de
Stokes capture la hiérarchie des résistances hydrauliques, qui est l'information
dont le gradient a besoin.

### 4.5 Brinkman : la frontière fluide-solide comme variable de design

Pour que l'optimiseur puisse *déplacer* la frontière fluide-solide, celle-ci ne
doit pas être un maillage : on plonge le solide dans le domaine fluide comme un
**milieu poreux fictif** (domaine fictif) en ajoutant un terme de réaction au
moment :

```
−μ∇²u + ∇p + α(γ) u = f,       α(γ) = α_max + (α_min − α_max) · γ(1+q)/(γ+q)
```

avec γ=1 fluide (α→α_min=0 : Stokes pur), γ=0 solide (α=α_max : u forcé vers 0),
q=0.1 (interpolation convexe de Borrvall & Petersson 2003, ADR-019). C'est
l'analogue fluide du SIMP : une pénalisation continue qui laisse le gradient
creuser les canaux.

**Sacrifices et calibration quantifiée** :

- **Fuite résiduelle** : α_max est fini, donc le « solide » laisse passer un débit
  parasite. Mesuré (oracle non-fuite, `test_brinkman.cpp` : dalle solide en
  travers d'un canal, critère fuite < 1 %) : **α_max = 1e4 → fuite 0.47 %**,
  première valeur du sweep sous 1 % — d'où le sweet spot retenu (ADR-019), dans la
  fourchette [1e3, 1e5] de la littérature (LL-LIT-004).
- **Compromis conditionnement / conservation de masse** (LL-011, mesuré sur le
  diffuseur BP) : pour un écoulement *traversant*, l'élément Q1-Q1 PSPG ne
  conserve pas strictement la masse à travers un saut de Brinkman fort —
  l'optimiseur peut alors « cacher » l'écoulement (islanding : poches fluides
  déconnectées, flux milieu → 0, Φ artificiellement nul). Remèdes appliqués :
  α_max modéré (~50) pour le traversant, profil de sortie imposé flux-matché
  (comme Borrvall-Petersson), symétrisation. Un élément mixte conservatif
  (Taylor-Hood) serait robuste à α_max élevé — compromis assumé d'ADR-017.
- **Interface diffuse** : la frontière n'est jamais une paroi nette mais une bande
  de largeur ~rayon de filtre où α transite ; le no-slip est approché, pas imposé.

Validation quantitative : écoulement de Darcy-Brinkman 1D, solution analytique en
cosh `u(x) = (G/α)[1 − cosh(κ(x−L/2))/cosh(κL/2)]`, κ=√(α/μ) — erreur relative
1.2e-3, convergence O(h²) (`test_brinkman.cpp`, rapport Phase 5).

### 4.6 Stabilisation PSPG : contourner l'inf-sup (ADR-017)

Stokes discret est un problème de **point-selle** : la paire (espace vitesse,
espace pression) doit satisfaire la condition **inf-sup de Ladyzhenskaya-
Babuška-Brezzi** (LBB) :

```
inf_q sup_v  ∫ q ∇·v / (‖v‖₁ ‖q‖₀)  ≥  β > 0    (uniformément en h)
```

Les éléments **égal-ordre Q1-Q1** (u et p aux mêmes nœuds) la **violent** : il
existe un mode de pression parasite (damier de pression) dans le noyau discret de
Bᵀ, non contrôlé — pression polluée, design absurde en TO (LL-LIT-002). Le choix
canonique serait Taylor-Hood (P2-P1, LBB-stable par construction) ; il est écarté
(ADR-017) car les nœuds milieux d'arête casseraient l'ADN du projet : grille
structurée uniforme, mêmes fonctions de forme partout, matrix-free GPU.

À la place, **stabilisation PSPG / Brezzi-Pitkäranta** : on ajoute au bloc
pression le terme

```
C = ∫ τ ∇q·∇p,        τ = α_stab h²/μ,   α_stab = 1/12
```

(`StokesSolver`), qui pénalise les oscillations de pression à l'échelle de la
maille et restaure la solvabilité (le système devient [[A, Bᵀ],[B, −C]]). Le prix,
documenté : (1) **consistance affaiblie** — le schéma perturbe l'incompressibilité
à O(h²), avec une couche limite de pression O(h) près des bords à ∇p≠0 (propriété
connue, notée en STATUS Phase 5, validée par convergence et non par valeur
ponctuelle) ; (2) la conservation de masse n'est que faible → LL-011 ci-dessus.
Preuve expérimentale que la stabilisation est décisive : α_stab = 1e-7 → pression
4e4 fois plus bruitée sur Poiseuille (STATUS Phase 5).

### 4.7 CHT : advection-diffusion + SUPG (ADR-020)

Le transfert de chaleur conjugué couple conduction (solide+fluide) et advection
(fluide) dans une seule équation sur tout le domaine :

```
−∇·(k(γ)∇T) + u·∇T = Q
```

(`CHTSolver`). Le terme d'advection `C_a = ∫ w (u·∇T)` est **non symétrique** —
première rupture de symétrie de la cascade (solveur direct LU, et transposition
explicite dans l'adjoint, §5.5).

**Le piège de Péclet** : le Galerkin standard devient oscillant dès que le Péclet
d'élément `Pe_e = |u| h/(2k)` dépasse ~1 (le schéma centré ne sait pas traiter un
transport raide). Remède : **SUPG** (Streamline-Upwind Petrov-Galerkin) — on
enrichit la fonction test dans la direction de l'écoulement :

```
S = ∫ τ (u·∇w)(u·∇T),   RHS −∫ τ (u·∇w) Q,
τ = h/(2|u|) · (coth(Pe_e) − 1/Pe_e)          (paramètre optimal classique)
```

Le résidu utilisé néglige le terme de diffusion (second ordre, standard pour des
éléments linéaires — commentaire d'en-tête de `CHTSolver`). Démonstration incluse
dans le test : à Pe élevé, Galerkin oscille, SUPG est propre (`test_cht.cpp`,
oracle 1D exponentiel `T(x) = (exp(Pe·x/L)−1)/(exp(Pe)−1)`, O(h²) à Pe=5).

**Sacrifices** : (1) SUPG n'est pas monotone — pas de principe du maximum discret,
des **under/overshoots locaux de T restent possibles** près des couches raides
(la stabilisation supprime les oscillations globales, pas les dépassements
locaux) ; (2) **l'adjoint de SUPG n'est pas différentié** : les gates adjoints
(§8.3) sont validés à Péclet modéré en Galerkin pur (« NO SUPG » explicite dans
`TripleAdjoint.hpp`), la différentiation de la stabilisation (τ dépend de u) est
un différé documenté (ADR-021). En pratique : le gradient est exact pour
l'opérateur sans SUPG ; l'utiliser avec le forward SUPG à haut Péclet introduirait
une incohérence gradient/objectif non contrôlée — d'où la restriction assumée au
Péclet modéré.

### 4.8 La cascade one-way — et tout ce qu'elle néglige

La physique complète est une **cascade unidirectionnelle** :

```
γ ──► Stokes-Brinkman ──► (u,p) ──► CHT ──► T ──► thermo-élastique ──► U
```

Chaque flèche est un couplage à sens unique. Inventaire explicite des
rétroactions **absentes** :

| Rétroaction négligée | Ce que ça exclut |
|---|---|
| U → géométrie du canal (pas d'ALE) | la déformation de la paroi ne modifie pas l'écoulement : pas de FSI au sens propre. La géométrie fluide ne change qu'à travers γ, au pas d'optimisation — pas au sens forme dans le solve. Exclut aussi l'instabilité de masse ajoutée du FSI partitionné (LL-LIT-003, non pertinent tant qu'on reste stationnaire one-way) |
| T → u (pas de flottabilité) | **pas de convection naturelle** (pas de terme de Boussinesq) : le refroidissement est purement forcé |
| T → μ, k, E (propriétés constantes) | pas de thermodépendance des propriétés |
| u·u (pas d'inertie, §4.4) | pas de turbulence, pas d'intensification convective du transfert (les corrélations type Nusselt(Re) sont hors modèle) |
| instationnarité | tout est stationnaire : pas de transitoires thermiques, pas de cyclage |

La contrepartie de cette austérité : la cascade est **exactement différentiable**,
et son adjoint est validable au chiffre près (§5, §8). C'est l'échange central du
projet : un modèle volontairement réduit, mais dont le gradient est *prouvé*.

---

## 5. La méthode adjointe : le cœur

### 5.1 Le problème du gradient

Il faut `dJ/dρ_e` pour chaque élément (jusqu'à des millions). Par différences
finies : un solve FEM par variable — impraticable. Par tangente linéaire (calculer
dU/dρ_e) : pareil. La **méthode adjointe** obtient le gradient complet pour le
coût d'**un seul solve linéaire supplémentaire par fonction** (objectif ou
contrainte), indépendamment du nombre de variables. C'est le théorème de coût qui
rend la TO possible : coût(gradient) ≈ coût(forward), quel que soit n.

Choix structurel : **adjoint discret** (« differentiate-then-discretize » au
niveau des résidus assemblés). On différencie exactement les opérateurs discrets
que le code assemble — les en-têtes des classes adjointes le disent explicitement
(« the discrete adjoint differentiates EXACTLY the operators used by the
finite-difference oracle », `TripleAdjoint.hpp`). Avantage décisif : le gradient
est exact pour le problème discret, donc vérifiable par différences finies au
plancher d'arrondi près (§8), et cohérent avec ce que l'optimiseur minimise
réellement.

### 5.2 Dérivation lagrangienne générale (mono-bloc)

État U défini implicitement par le résidu `R(ρ, U) = 0`, objectif `J(ρ, U)`.
Lagrangien :

```
L(ρ, U, λ) = J(ρ, U) + λᵀ R(ρ, U)
```

Comme R=0 sur la trajectoire, L ≡ J pour tout λ. Dérivée totale :

```
dJ/dρ = ∂J/∂ρ + λᵀ ∂R/∂ρ + [ ∂J/∂U + λᵀ ∂R/∂U ] · dU/dρ
```

Le terme `dU/dρ` (une matrice n_dof × n_élém — l'objet impayable) disparaît si on
choisit λ solution de l'**équation adjointe** :

```
(∂R/∂U)ᵀ λ = −(∂J/∂U)ᵀ
```

et il reste le **gradient adjoint** :

```
dJ/dρ_e = ∂J/∂ρ_e + λᵀ (∂R/∂ρ_e)
```

Pour un bloc FEM `R = K(ρ)U − F(ρ)` : adjoint `Kᵀλ = −∂J/∂U`, gradient
`dJ/dρ_e = ∂J/∂ρ_e + λᵀ(∂K/∂ρ_e · U − ∂F/∂ρ_e)`. Comme `∂K/∂ρ_e` n'est non nulle
que sur les 24 DOF de l'élément e, le gradient est une somme de produits locaux —
une passe sur les éléments.

### 5.3 Cas auto-adjoint : la compliance

`J = FᵀU`, F indépendant de ρ : `∂J/∂U = F`, et K symétrique donne `Kλ = −F`,
donc **λ = −U** : le champ adjoint est le champ direct changé de signe, aucun
solve supplémentaire. Gradient :

```
dc/dρ_e = −Uᵀ (∂K/∂ρ_e) U = −p ρ_e^{p−1}(E_0−E_min) · u_eᵀ KE0 u_e ≤ 0
```

(`SIMP3D::complianceSensitivity`). Le signe constant (ajouter de la matière ne
peut qu'assouplir) est une propriété spécifique de la compliance — c'est ce qui
permet à l'Optimality Criteria de fonctionner (§7.1) ; aucun autre objectif du
projet ne l'a.

Le même schéma quasi-auto-adjoint réapparaît côté fluide : pour la dissipation
`Φ = ½∫(μ|∇u|² + α(γ)|u|²) = ½ wᵀH w` (H = bloc vitesse de A), l'adjoint vérifie
`λ_u ≈ −u` — le résidu `‖λ_u + u‖/‖u‖` est rapporté comme test de cohérence
(`DissipationAdjoint.hpp`, champ `selfAdjResidual`). « Quasi » car PSPG et les
lifts de Dirichlet brisent l'égalité exacte.

### 5.4 Adjoint deux blocs : thermo-élastique

Deux résidus, un couplage (T → U via F_th) :

```
R_t = K_t(ρ) T − Q = 0
R_e = K_e(ρ) U − F_mech − F_th(ρ, T) = 0
L = J + λ_eᵀ R_e + λ_tᵀ R_t
```

Annulation des dérivées d'état, **dans l'ordre inverse de la cascade** :

```
∂L/∂U = 0 :   K_eᵀ λ_e = −∂J/∂U                        (adjoint élastique)
∂L/∂T = 0 :   K_tᵀ λ_t = Gᵀ λ_e,    G = ∂F_th/∂T       (adjoint thermique)
```

L'adjoint thermique est **piloté par l'adjoint élastique** : le RHS `Gᵀλ_e`
s'assemble par élément comme `E_e α_th C_eᵀ λ_e|_e` (`ThermoElasticAdjoint`).
Gradient — trois termes héréditaires :

```
dJ/dρ_i = ∂J/∂ρ_i
        + λ_eᵀ [ (∂K_e/∂ρ_i) U − ∂F_th/∂ρ_i ]     (raideur + charge thermique)
        + λ_tᵀ (∂K_t/∂ρ_i) T                        (conduction)
```

Le piège historique est le terme `∂F_th/∂ρ_i = dE_i · α_th C_i (T_i − T_ref)` —
la force thermique dépend de la densité via E. Son absence est indétectable à
l'œil sur un design et immédiatement visible en différences finies : c'est
l'archétype de ce que les gates du §8.3 attrapent.

### 5.5 Adjoint triple-couplé : la cascade inverse complète

Trois résidus (conventions v3 : γ=1 fluide, `TripleAdjoint.hpp`) :

```
R_s = A(γ) w − f_s = 0                    w = (u, p)   Stokes-Brinkman
R_t = K_t(γ, u) T − f_t = 0                            CHT (dépend de u !)
R_e = K_e(γ) U − F_mech − F_th(γ, T) = 0               thermo-élastique
```

Lagrangien `L = J + λ_eᵀR_e + λ_tᵀR_t + λ_sᵀR_s`, annulations en sens inverse :

```
1.  ∂L/∂U = 0 :   K_eᵀ λ_e = −∂J/∂U
2.  ∂L/∂T = 0 :   K_tᵀ λ_t = Gᵀ λ_e                    (G = ∂F_th/∂T)
3.  ∂L/∂w = 0 :   Aᵀ λ_s  = −(∂R_t/∂u)ᵀ λ_t
```

Deux termes méritent le détail :

- **La transposition de l'advection** (étape 2) : `K_t` contient
  `C_a(u)_{ab} = ∫ N_a (u·∇N_b)`. Sa transposée échange fonctions test et essai :
  après intégration par parties, l'opérateur adjoint transporte **à contre-
  courant** (u·∇ → −u·∇ modulo termes de bord). Physiquement : l'information
  adjointe (la sensibilité) remonte l'écoulement — la sensibilité d'une
  température aval vit en amont.
- **Le terme ∂R_t/∂u** (étape 3) — le cœur mathématique du projet : le résidu
  thermique dépend de la vitesse parce que le fluide advecte la chaleur. Par
  élément :

  ```
  ∂R_t,a/∂u_{b,c} = ∫ N_a N_b (∂T/∂x_c)
  ```

  (`TripleAdjoint.hpp`, ligne de doc de l'étape 3) : la sensibilité du bilan
  thermique au nœud a par rapport à la composante c de vitesse au nœud b est
  pondérée par le **gradient local de température** — s'il n'y a pas de gradient
  de T, déplacer le fluide ne change rien au thermique, et le couplage
  adjoint s'éteint. Les lignes pression du RHS sont nulles (la pression
  n'advecte rien).

Gradient final — somme des trois blocs plus l'explicite :

```
dJ/dγ_i = ∂J/∂γ_i                                        (si J dépend de γ)
        + λ_eᵀ [ (∂K_e/∂γ_i) U − ∂F_th/∂γ_i ]            avec dE/dγ = −p(1−γ)^{p−1}ΔE
        + λ_tᵀ (∂K_t/∂γ_i) T                              avec ∂K_t/∂γ_i = dk_i L0_i
        + λ_sᵀ (∂A/∂γ_i) w                                avec ∂A/∂γ_i = dα_i M_vel,i
```

Coût total : trois solves adjoints (un par bloc, sur les opérateurs transposés
déjà factorisés ou re-factorisés) — le même ordre de grandeur que le forward,
pour un gradient sur toutes les variables. Le test du gate exige explicitement
que les **trois contributions soient non triviales** (`test_triple_adjoint_fd` :
`sS, sT, sE > 1e-12`) — garde-fou contre un couplage silencieusement mort.

### 5.6 Modularité du semis : un adjoint = une machinerie, un RHS

Toute la cascade inverse est **indépendante de l'objectif** : seul le *semis*
(le RHS du premier solve adjoint) change. Le repo l'exploite systématiquement :

| Objectif / contrainte | Semis | Blocs adjoints | Classe |
|---|---|---|---|
| compliance `J = F_mechᵀU` | `−F_mech` | e ← t ← s | `TripleAdjoint::solve` |
| von Mises p-norm (cascade triple) | `−∂J_σ/∂U` | e ← t ← s | `TripleAdjoint::solveStress` |
| T_max paroi (p-norm de T solide) | `−∂J_T/∂T` | t ← s (pas d'élastique) | `ThermalObjectiveAdjoint` |
| dissipation Φ | `−H w` | s seul (quasi auto-adjoint) | `DissipationAdjoint` |
| von Mises 3D / axi (structurel) | `−∂σ_PN/∂U` | e seul | `ThermoElasticAdjoint::stressPNormGrad`, `AxiStressAdjoint` |

Le gate « vm-triple » (§8.3) est la démonstration de cette modularité : même
cascade inverse que le gate triple-couplé, seul le semis élastique change
(`−∂J_σ/∂U` au lieu de `−F_mech`), la relaxation qp portant sur la solidité
`s = 1−γ`. C'est ce qui permet d'imposer **quatre contraintes simultanées**
(volume, T_max, ΔP/dissipation, von Mises) dans l'optimisation fluide-thermique —
chaque contrainte coûte sa propre remontée adjointe, toutes partagent le forward.
Sur `cooling_jacket_full`, les quatre sont actives à convergence
(`docs/INPUT_LANGUAGE.md` §5).

Détail axisymétrique (`AxiStressAdjoint.hpp`, marqué CRITICAL) : en coordonnées
r-z, la matrice élémentaire dépend de r — il n'y a **pas** de KE0 partagé comme en
cartésien ; chaque `∂K/∂ρ_i` utilise le KE0ax propre à l'élément i. Une erreur ici
passe un test cartésien et casse l'axi — d'où un gate dédié.

---

## 6. Contraintes ponctuelles : agrégation et relaxation

### 6.1 p-norm : une contrainte différentiable au lieu d'un million

Le stress et la température sont des contraintes *locales* : `σ_e ≤ σ_lim` pour
chaque élément — des millions de contraintes, ingérable pour MMA (dont le dual
coûte en m). On agrège par une **p-norm** :

```
σ_PN = ( Σ_e σ_e^P )^{1/P},        P = 8
```

(`StressModel`, P=8 partout dans le repo : `topopt_run.cpp`, gates). Propriétés
exactes — c'est un encadrement, pas une approximation vague :

```
σ_max  ≤  σ_PN  ≤  N^{1/P} · σ_max
```

La p-norm brute **majore** le max (elle est conservative), avec un biais qui
croît comme N^{1/P} — non négligeable : à N = 10⁶ éléments et P=8, le facteur
vaut jusqu'à ~5.6. La variante moyenne (P-mean, `(1/N Σ σ^P)^{1/P}`) **minore**
au contraire le vrai max — un design « faisable » au sens P-mean peut violer la
contrainte locale. Le choix de P est un compromis : P grand → agrégat proche du
max mais gradient concentré sur quelques éléments (raide, mal conditionné) ;
P petit → gradient doux mais biais fort. P=8 est le régime standard
(Le, Norato, Bruns et al. 2010 y consacrent une normalisation adaptative).

Mitigation du biais dans le repo : la borne peut être **relative au design plein**
— `constraints[].max_rel` : `σ_lim = max_rel · σ_PN(ρ=1)` (`topopt_run.cpp`,
`nozzle_profiled.cpp` : `sigmaLim = 1.6·sigmaSolid`). Le biais d'agrégation
N^{1/P} affecte alors les deux membres et s'annule au premier ordre : on contraint
« pas plus de 1.6× l'agrégat du design plein », ce qui est bien posé quel que
soit N. La contrainte est ensuite normalisée `g = σ_PN/σ_lim − 1 ≤ 0` pour MMA.

### 6.2 Relaxation qp du stress : lever la singularité ρ→0

Phénomène (Duysinx & Bendsøe 1998, LL-LIT-001) : dans la TO sous contrainte de
stress, la contrainte *microscopique* pertinente ne s'annule pas quand ρ→0 (elle
tend vers une limite finie non nulle le long des suites minimisantes). Le domaine
faisable possède des **appendices dégénérés** de mesure nulle : l'optimum global
y vit (retirer complètement la matière annule la contrainte), mais aucun chemin
continu à contrainte satisfaite n'y mène — l'optimiseur **refuse de créer du
vide**. C'est le « stress singularity phenomenon ».

Remède : **relaxation qp** (Bruggi 2008). La contrainte porte sur le stress
relaxé :

```
σ_e = ρ_e^q · vm0_e,        q = 0.5  <  p = 3
```

où `vm0_e = √((S0 u_e)ᵀ V (S0 u_e))` est le von Mises **solide** au centroïde,
calculé à module unité (E=1, `H8Element::stressMatrix`) — toute la dépendance
explicite en ρ est dans le facteur ρ^q. Comme q < p, quand ρ→0 le stress relaxé
s'annule (le déplacement u reste borné car E ~ ρ^p décroît plus vite que ρ^q) :
les appendices singuliers sont « ouverts », l'optimiseur peut vider un élément en
traversant une zone faisable. Le prix : près de ρ=0, la contrainte relaxée
sous-estime le stress physique du matériau résiduel — acceptable parce que ces
éléments sont précisément en train de disparaître, et que le design final est
quasi binaire (§3.5).

En v3 (convention fluide), la relaxation porte sur la solidité :
`σ_e = s_e^q · vm0_e`, `s = 1−γ`, avec `ds/dγ = −1` dans le terme explicite du
gradient (`TripleAdjoint::StressParams`, q=0.5, P=8).

### 6.3 T_max : p-norm pondérée par la solidité

La température maximale de paroi est agrégée de la même façon, avec une
pondération qui **restreint la mesure au solide** (`ThermalObjectiveAdjoint`) :

```
J_T = ( Σ_e s_e · T_e^P )^{1/P},     s_e = 1 − γ_e,   T_e = (1/8) Σ_{a∈e} T_a,   P = 8
```

On mesure la température *dans le mur*, pas dans le fluide (le coolant chaud en
sortie est normal ; le mur chaud ne l'est pas). Le poids s_e dépend de γ, donc
J_T a un terme de gradient **explicite** `∂J_T/∂γ_i = (1/P) J_T^{1−P} T_i^P·(−1)`
en plus des deux remontées adjointes (thermique puis Stokes) — les trois
contributions sont exigées non triviales par le gate. Même structure d'agrégation
que le stress : mêmes bornes (§6.1), mêmes compromis sur P.

---

## 7. L'optimiseur : MMA (Method of Moving Asymptotes)

### 7.1 Pourquoi MMA

Pour la seule contrainte de volume avec un objectif à gradient de signe constant
(compliance, §5.3), l'**Optimality Criteria** suffit : mise à jour multiplicative
+ bissection sur le multiplicateur de volume (`SIMP3D::ocUpdate`). Dès qu'il y a
plusieurs contraintes de natures différentes (masse + von Mises + T_max + ΔP) ou
des gradients de signe quelconque, il faut un vrai programme non linéaire —
**MMA** (Svanberg 1987), le standard de la communauté TO, implémenté sous sa
forme `mmasub` classique (`MMAOptimizer`).

### 7.2 L'approximation convexe séparable

À chaque itération k, autour de l'itéré x^k, chaque fonction (objectif f₀ et
contraintes f_i) est remplacée par une approximation **convexe et séparable**
construite sur des **asymptotes mobiles** L_j < x_j < U_j :

```
f_i^(k)(x) = r_i + Σ_j [ p_ij/(U_j − x_j) + q_ij/(x_j − L_j) ]
```

avec (formules exactes du code, `MMAOptimizer::buildSubproblem`) :

```
p_ij = (U_j − x_j)² · [ 1.001·(∂f_i/∂x_j)⁺ + 0.001·(∂f_i/∂x_j)⁻ + reg ]
q_ij = (x_j − L_j)² · [ 0.001·(∂f_i/∂x_j)⁺ + 1.001·(∂f_i/∂x_j)⁻ + reg ]
```

((·)⁺/(·)⁻ = parts positive/négative du gradient). L'approximation interpole la
valeur et le gradient en x^k, est convexe en chaque x_j séparément, et **diverge
aux asymptotes** : elle interdit d'elle-même les pas trop grands. Les termes
croisés 0.001 et la régularisation `reg` la rendent strictement convexe même à
gradient nul. L'intuition mécanique de Svanberg : pour une structure, la réponse
est mieux approximée en variables réciproques (1/x, cf. compliance d'une barre
~1/aire) qu'en variables directes — MMA généralise cette convexification
réciproque avec un point de référence ajustable (l'asymptote).

### 7.3 Asymptotes mobiles : l'adaptation de la confiance

Les asymptotes jouent le rôle de région de confiance auto-adaptative
(`updateAsymptotes`, paramètres de `MMAOptimizer::Params`) :

- Initialisation : `L, U = x ∓ 0.5·(x_max − x_min)` (asyinit = 0.5).
- Si x_j **oscille** ((x_j^k − x_j^{k−1})(x_j^{k−1} − x_j^{k−2}) < 0) : resserrer,
  γ = 0.7 (asydecr) — on amortit.
- Si x_j progresse de façon **monotone** : élargir, γ = 1.2 (asyincr) — on
  accélère.
- Bornes de pas en plus des asymptotes : `α_j = max(x_min, L + 0.1(x−L), x −
  move·range)` avec **move limit** 0.5 (albefa = 0.1) — double garde-fou.

### 7.4 Résolution du sous-problème : le dual

Le sous-problème MMA est convexe séparable → dualité forte, et le **minimiseur
primal est en forme fermée** par variable (formule du code, `primalX`) :

```
x_j(λ) = ( √P_j · L_j + √Q_j · U_j ) / ( √P_j + √Q_j ),   clampé à [α_j, β_j]
P_j = p_0j + Σ_i λ_i p_ij,     Q_j = q_0j + Σ_i λ_i q_ij
```

Il reste à maximiser la fonction duale concave W(λ) sur λ ≥ 0 (m variables — le
nombre de contraintes, pas le nombre de densités) :

- **m = 1** : bissection sur dW/dλ (`solveDual`) — le cas volume seul.
- **m ≥ 2** : **Newton projeté** sur le dual (gradient `dualGrad`, hessienne
  `dualHess` avec la dépendance dx/dλ des variables non clampées) — le cas
  multi-contraintes du cooling jacket (jusqu'à m = 4 actives).
- Toutes les boucles internes sont **plafonnées** (maxDualIter = 200,
  dualTol = 1e-12) — application directe de LL-008 (une bissection non bornée
  + un NaN = boucle infinie silencieuse).

Le sous-problème inclut les variables artificielles standard de Svanberg
(y_i ≥ 0, z ≥ 0 ; a₀=1, a_i=0, c_i=1000, d_i=1) : avec c_i grand mais fini, le
sous-problème reste **toujours faisable** même quand les contraintes courantes
sont violées — l'optimiseur paie une pénalité linéaire au lieu d'échouer, et
revient dans le domaine faisable en quelques itérations.

### 7.5 Garanties, et ce que MMA ne garantit PAS

Ce qui est garanti par la théorie (Svanberg 1987) : chaque sous-problème est
convexe et admet un unique optimum ; si la suite converge, le point limite
vérifie les conditions KKT du problème original.

Ce qui n'est **pas** garanti — sacrifices documentés :

- **MMA classique n'est pas globalement convergent** : rien n'assure la
  décroissance monotone d'une fonction de mérite, la suite peut cycler. La
  version **GCMMA** (Svanberg 2002 : itérations internes conservatives, qui
  garantissent la convergence globale vers un point KKT) n'est **pas
  implémentée** — sacrifice assumé, compensé en pratique par les move limits,
  l'adaptation des asymptotes et les continuations, et surveillé empiriquement
  (décroissance monotone effectivement observée sur les démonstrateurs, ex.
  cooling jacket J = 6.78 → 1.945 monotone, rapport Phase 5).
- Optimum **local** seulement (§1.4) — propriété du problème, pas de MMA.
- La qualité du pas dépend de gradients **exacts** : MMA amplifie les erreurs de
  gradient au lieu de les lisser (l'approximation interpole le gradient fourni).
  D'où la discipline du §8.

### 7.6 Validation

Deux oracles (`test_mma.cpp`) : (A) problème séparable analytique
`min Σ c_j/x_j s.t. moyenne(x) ≤ vfrac` dont l'optimum KKT intérieur est en forme
fermée (`x_j* ∝ √c_j`) — **accord 6.4e-14** (tolérance 1e-3, rapport Phase 4) ;
(B) cross-check MBB 3D contre l'OC — écart de compliance 0.037 % (même init, même
filtre) ; plus un cas m=2 exerçant le chemin Newton projeté.

---

## 8. La discipline de validation

### 8.1 Pourquoi c'est structurel, pas cosmétique

Le danger spécifique de la TO adjointe : **un gradient faux produit quand même un
design**. MMA fait des pas, l'objectif bouge, le champ se binarise, la géométrie
a l'air plausible — et tout est faux, silencieusement (un terme de couplage
oublié, un signe, un ∂F_th/∂ρ manquant). Il n'existe aucun symptôme visuel fiable
(LL-LIT-007). La seule défense est un protocole : **aucun gradient n'est utilisé
par l'optimiseur avant d'avoir passé un gate de différences finies**, et aucun
solveur avant son oracle analytique. Sept gates adjoints ont été franchis ainsi,
chacun étant bloquant pour la suite du projet.

### 8.2 Oracles analytiques (les solveurs physiques)

| Solveur | Oracle | Résultat mesuré |
|---|---|---|
| FEM 3D élastique | patch test tension uniaxiale (champ constant) | erreur < 1e-10 (`test_fem3d`) |
| Thermique | plaque à gradient linéaire | 8.9e-7 (rapport P4) |
| Thermo-élastique | dilatation libre : σ=0 sous ΔT uniforme | 7.4e-6 (rapport P4) |
| von Mises | cas uniaxial analytique | 2.4e-15 (rapport P4) |
| FEM axi Q4 | cylindre épais de Lamé (a=1, b=2, p_i=1, plane strain) | σ_θ 5.3e-5 à nr=40, ordre 2 (ratio 3.84) |
| Stokes Q1-Q1 PSPG | Poiseuille (force : exact ; piloté par pression : u O(h²) 7.4e-3→1.8e-3, p linéaire) | STATUS P5 |
| — inf-sup | α_stab 1e-7 → pression 4e4× plus bruitée (PSPG décisif) | STATUS P5 |
| Brinkman | Darcy-Brinkman 1D, profil cosh | relErr 1.2e-3, O(h²) |
| — non-fuite | dalle solide en travers du canal | α_max=1e4 → 0.47 % (< 1 %) |
| CHT | conduction pure | 4.4e-15 |
| — advection-diffusion | profil exponentiel 1D en Pe | O(h²) (ratio 3.76, Pe=5) ; piège Péclet démontré (Galerkin oscille, SUPG propre) |
| Marching cubes | sphère SDF : aire 4πR², volume 4/3πR³ | 0.06 % (borne test < 3 % à nx=64), watertight |
| MMA | optimum KKT analytique | 6.4e-14 ; vs OC 0.037 % |

### 8.3 Les sept gates adjoints (différences finies)

Protocole : sur un petit cas (grilles ~6³-8³ à BC complètes), comparer le
gradient adjoint aux différences finies centrées élément par élément (échantillon
aléatoire), avec un critère PASS explicite dans le test. Table exacte du repo :

| # | Gate | Test | Critère PASS (repo) | Stencil / ε | Mesuré | Source |
|---|---|---|---|---|---|---|
| 1 | compliance thermo-élastique (2 blocs) | `test_adjoint_fd` | rel < 1e-5 (20 élém.) | centré 2ᵉ ordre, ε=1e-6 | **1.6e-6** | rapport P4 |
| 2 | stress von Mises p-norm 3D | `test_stress_adjoint_fd` | rel < 1e-5 | centré 4ᵉ ordre, ε=1e-4 | **1.6e-7** | rapport P4 |
| 3 | stress axisymétrique | `test_axi_stress_adjoint_fd` | rel < 1e-5 | centré 4ᵉ ordre, ε=1e-4 | **2.7e-9** | rapport P4 |
| 4 | **triple-couplé** Stokes-CHT-élastique | `test_triple_adjoint_fd` | rel < 1e-3 ET 3 termes non triviaux | centré 2ᵉ ordre, ε=1e-6 | **2.1e-7** (abs 7e-9) | ADR-021, STATUS P5 |
| 5 | dissipation visqueuse (TO fluide) | `test_dissipation_adjoint_fd` | rel < 1e-4 ET termes non triviaux | centré, ε=1e-6 | **7e-7** | prompt 5R (bp_reproduction) |
| 6 | température de paroi T_max | `test_tmax_adjoint_fd` | rel < 1e-3 ET 3 termes non triviaux | centré 4ᵉ ordre, ε=1e-6 | **7.5e-8** | handoff P5→6 |
| 7 | von Mises à travers la cascade triple | `test_vm_triple_fd` | rel < 1e-3 ET 3 termes non triviaux | centré 4ᵉ ordre, ε=1e-6 | **8.0e-7** (8.007e-7) | commit du gate / prompt A3 |

Trois remarques de structure :

- Les gates 4, 6, 7 exigent que **chaque bloc de la cascade contribue de façon
  non triviale** au gradient (Σ|terme| > 1e-12 par bloc) : un couplage mort
  passerait sinon un test de précision par vacuité.
- Le gate 7 ne re-dérive rien : même cascade inverse que le gate 4, seul le semis
  change (§5.6) — c'est le test de la *modularité* de l'architecture adjointe.
- Les marges sont de 2 à 4 ordres de grandeur sous les tolérances : les gradients
  sont exacts au plancher d'arrondi près, ce qui est la signature attendue d'un
  adjoint discret correct (§5.1).

### 8.4 La subtilité LL-009 : accord absolu vs relatif

Leçon mesurée en Phase 4, appliquée depuis à tous les gates : sur les éléments à
gradient quasi nul, l'erreur *relative* DF explose **sans qu'aucun bug
n'existe** — la différence finie a un plancher d'arrondi `≈ ε_machine·|J|/ε`
(deux évaluations de J à ~1e-13 de bruit relatif chacune, divisées par un petit
ε). Le diagnostic décisif est le **sens de variation avec ε** :

- l'erreur **décroît** quand ε grandit → plancher d'arrondi pur, l'adjoint est
  bon (sweep observé sur le gate stress : ε 1e-5 → 1.6e-6, 1e-4 → 1.6e-7,
  1e-3 → 3e-8 ; commentaire de `test_stress_adjoint_fd.cpp`) ;
- l'erreur **croît** quand ε grandit → vraie erreur de troncature ou adjoint faux.

Règles retenues : juger d'abord sur l'**accord absolu** (nombre de chiffres
significatifs) des éléments bien conditionnés — 7 à 11 chiffres pour un adjoint
correct ; utiliser un stencil centré d'ordre 4 et un ε adapté ; sur le gate
triple, les termes de couplage fluide/thermique (petits devant l'élastique) ont
été validés par l'accord absolu à 7e-9 (rapport P5).

### 8.5 Validation contre la littérature : Borrvall & Petersson 2003

Le cas canonique de la TO fluide (minimiser la dissipation à volume de fluide
contraint dans un diffuseur) est reproduit (`bp_diffuser`, commit de
reproduction) : l'entonnoir **convergent lisse et symétrique** — la signature du
papier — émerge ; dissipation Φ 648 → 33 (**97 % de mieux que le design
uniforme** au même Brinkman), volume actif à 0.50, design binaire (gris 2.5 %),
sans damier, masse conservée entrée≈milieu≈sortie. Accord *qualitatif* avec BP
2003 ; la comparaison quantitative à ~5 % contre les valeurs publiées reste un
différé documenté. C'est aussi ce cas qui a produit la leçon LL-011 (islanding,
§4.5) — l'exemple type d'une validation littérature qui découvre une limite
réelle de la discrétisation.

---

## 9. Limitations consolidées (le contrat d'honnêteté)

Récapitulatif de tout ce que le solveur **ne fait pas**, avec la référence de
l'endroit où c'est documenté :

**Modélisation physique**
1. **Stokes, pas Navier-Stokes** (§4.4) : Re ≪ 1, pas d'inertie, pas de
   turbulence, pertes de charge linéaires en débit. Les designs fluides sont des
   a priori topologiques pour le régime rampant.
2. **Cascade one-way** (§4.8) : pas de rétroaction géométrie→écoulement au sens
   forme dans le solve, pas de FSI, pas de convection naturelle, pas de
   propriétés thermodépendantes, tout stationnaire.
3. **Élasticité linéaire** (§4.1) : pas de flambage — à vérifier a posteriori sur
   des treillis en compression — ni de plasticité, dynamique, fatigue.
4. **Brinkman à α_max fini** (§4.5) : fuite résiduelle quantifiée (0.47 % à
   α_max=1e4 sur l'oracle dalle), interface diffuse à l'échelle du filtre.

**Discrétisation**
5. **Q1-Q1 PSPG** (§4.6, ADR-017) : conservation de masse faible seulement —
   islanding possible en écoulement traversant à fort Brinkman (LL-011 ; parade :
   α_max ~50, sortie flux-matchée). Couche limite de pression O(h). Taylor-Hood
   serait plus robuste, au prix de la grille structurée.
6. **SUPG non différentié** (§4.7, ADR-021) : gates adjoints validés à Péclet
   modéré sans SUPG ; le haut Péclet exige de différentier la stabilisation.
   SUPG lui-même n'est pas monotone (under/overshoots locaux possibles).
7. **E_min = 1e-4** (§2.4) : plancher imposé par le CG float32 GPU (LL-006) —
   rigidité résiduelle du vide.

**Optimisation**
8. **Optima locaux** (§1.4) : aucune garantie de globalité ; dépendance à
   l'initialisation et aux chemins de continuation, atténuée mais pas éliminée
   par les continuations p/β/α_max.
9. **MMA classique, pas GCMMA** (§7.5) : pas de preuve de convergence globale ;
   robustesse empirique (move limits, asymptotes) vérifiée sur les
   démonstrateurs.
10. **Agrégation p-norm** (§6.1) : biais N^{1/P} vs le max exact — mitigé par la
    calibration `max_rel` relative au design plein ; la faisabilité locale
    stricte élément par élément n'est pas garantie par l'agrégat.
11. **Taille de feature = donnée d'entrée** (§1.3) : le rayon du filtre est un
    choix utilisateur, pas un résultat d'optimisation.
12. **Projection Heaviside nominale** (§3.3) : pas de formulation robuste
    érodé/dilaté (Wang 2011) ; β plafonné à 4 sur le cas stress axi (instabilité
    documentée).

**Périmètre numérique**
13. Les adjoints multiphysiques tournent en **CPU double précision** (ADR-018) ;
    le portage GPU float32 est un différé de production, à re-valider contre le
    chemin CPU.
14. Comparaison Borrvall-Petersson **qualitative** (§8.5) ; la reproduction
    quantitative à ~5 % est un différé.

Aucune de ces limites n'est cachée dans le code : chacune correspond à un ADR,
une LL, un commentaire d'en-tête ou une ligne de rapport de phase.

---

## 10. Références

Les classiques sur lesquels chaque brique s'appuie :

- **Bendsøe & Kikuchi 1988**, *Generating optimal topologies in structural design
  using a homogenization method*, CMAME — l'acte de naissance du domaine ; la
  non-existence et la relaxation par homogénéisation (§1.2-1.3).
- **Bendsøe & Sigmund 2003**, *Topology Optimization: Theory, Methods and
  Applications*, Springer — la référence SIMP/densités ; l'interprétation
  composite et la condition p≥3 (§2.3, d'après Bendsøe & Sigmund 1999, *Material
  interpolation schemes in topology optimization*, Arch. Appl. Mech.).
- **Svanberg 1987**, *The method of moving asymptotes — a new method for
  structural optimization*, IJNME — MMA (§7) ; **Svanberg 2002** (SIAM J. Optim.)
  pour GCMMA, la variante globalement convergente non implémentée.
- **Borrvall & Petersson 2003**, *Topology optimization of fluids in Stokes
  flow*, IJNMF — la TO fluide par Brinkman, l'interpolation α(γ) (§4.5), et le
  cas diffuseur reproduit (§8.5).
- **Lazarov & Sigmund 2011**, *Filters in topology optimization based on
  Helmholtz-type differential equations*, IJNME — le filtre EDP (§3.1).
- **Wang, Lazarov & Sigmund 2011**, *On projection methods, convergence and
  robust formulations in topology optimization*, SMO — la projection de Heaviside
  et sa version robuste (§3.3).
- **Le, Norato, Bruns, Ha & Tortorelli 2010**, *Stress-based topology
  optimization for continua*, SMO — l'agrégation p-norm du stress et sa
  normalisation (§6.1).
- **Duysinx & Bendsøe 1998** (IJNME) et **Bruggi 2008** (SMO) — la singularité de
  stress et les relaxations ε/qp (§6.2).
- **Bourdin 2001**, *Filters in topology optimization*, IJNME — l'existence par
  restriction (§1.3).
- **Andreassen et al. 2011**, *Efficient topology optimization in MATLAB using 88
  lines of code*, SMO — la base MBB canonique des phases 1-3.
- **Aage, Andreassen & Lazarov 2015** (SMO) — la TO grande échelle matrix-free
  qui inspire l'architecture GPU.
- **Dilgen et al. 2018** (CMAME) — la TO multiphysique fluide-thermique adjointe,
  l'état de l'art dont la cascade du §5.5 est l'instanciation.

---

*Complément opérationnel : `docs/INPUT_LANGUAGE.md` (le format ProblemSpec et
l'état d'implémentation v1→v3), `docs/DECISIONS.md` (ADR-017..021),
`archive/orchestration/LESSONS_LEARNED.md` (LL-001..011, LL-LIT-001..012),
`archive/reports/PHASE_*.md` (validations chiffrées par phase).*
