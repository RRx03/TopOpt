# TopOpt — Théorie et fonctionnement de l'algorithme

*Document de référence expliquant la méthode et la théorie derrière le solveur de
topology optimization multiphysique TopOpt. Public : ingénieur/étudiant avancé
(FEM, calcul scientifique). Objectif : comprendre **comment** ça marche et
**pourquoi** ça marche, sans reproduire une thèse.*

---

## 0. En une phrase

TopOpt trouve **où mettre la matière** dans un volume donné pour optimiser une
performance (rigidité, refroidissement, masse) sous contraintes, en laissant un
**algorithme de gradient** découvrir la topologie — la matière, les trous, les
canaux — au lieu de la dessiner à la main. La difficulté, et l'intérêt, est de
calculer ce gradient à travers une **physique couplée fluide-structure-thermique**
via la **méthode adjointe**.

---

## 1. Le problème d'optimisation topologique

### 1.1 Variable de design : la densité

On discrétise le domaine en éléments finis. À chaque élément `e` on associe une
**densité** `ρ_e ∈ [0, 1]` : `ρ=1` = matière (ou fluide, selon le contexte),
`ρ=0` = vide (ou solide). C'est la **méthode des densités** (par opposition au
level-set) : le champ `ρ` est la variable de design continue que l'optimiseur ajuste.

Le problème générique :
```
min_ρ   J(ρ, champs physiques)          (objectif : compliance, masse, dissipation…)
s.t.    R(ρ, champs) = 0                 (les EDP physiques : équilibre, chaleur, Stokes)
        g_k(ρ, champs) ≤ 0               (contraintes : volume, von Mises, T_max, ΔP)
        0 ≤ ρ_e ≤ 1
```

### 1.2 Interpolation du matériau : SIMP

Une densité intermédiaire `ρ=0.5` n'a pas de sens physique (« demi-matière »). Pour
pousser le design vers du **binaire** 0/1, on pénalise les densités intermédiaires
par la loi **SIMP** (Solid Isotropic Material with Penalization) :
```
E(ρ) = E_min + ρ^p (E_0 − E_min)
```
Le module de Young interpolé croît en `ρ^p` (p = 3 typiquement) : à `ρ=0.5` et p=3,
on n'a que `0.125·E_0` de rigidité pour `0.5` de masse → les densités grises sont
**inefficaces**, l'optimiseur les élimine. `E_min` (≈ `1e-4·E_0`) est un plancher
qui évite une matrice singulière dans le vide (crucial pour les solveurs itératifs,
cf. §7).

**Continuation de p** : démarrer directement à p=3 crée trop de minima locaux. On
augmente p progressivement (1 → 2 → 3), ce qui explore d'abord la topologie en
régime « mou » puis durcit. En multi-grille, ce choix se configure par niveau
(hériter p vs redémarrer) selon la pièce.

### 1.3 Filtrage : indépendance au maillage et anti-damier

Sans précaution, deux pathologies apparaissent :
- **Damier (checkerboard)** : alternance `ρ=0/ρ=1` cellule par cellule, artefact
  numérique de la discrétisation.
- **Dépendance au maillage** : un maillage plus fin produit des détails plus fins
  → le design dépend de la résolution, ce qui est inacceptable.

Solution : un **filtre de densité** qui impose une **taille minimale de feature**.
On utilise le filtre **de Helmholtz** (Lazarov-Sigmund 2011) : la densité filtrée
`ρ̃` est la solution d'une EDP de diffusion
```
−r² ∇²ρ̃ + ρ̃ = ρ
```
où `r` est le rayon (relié à la taille mini). Point clé de la maturité du solveur :
`r` est exprimé en **unités physiques (mm)**, pas en cellules — ainsi la taille de
feature est fixée physiquement et **le design converge quand on raffine le maillage**
(mesh independence, démontrée en Phase 3).

### 1.4 Projection Heaviside : binarisation nette

Le filtre lisse mais « floute » (introduit du gris). Pour récupérer un design net
0/1, on applique une **projection Heaviside régularisée** (Wang-Lazarov-Sigmund
2011) sur la densité filtrée :
```
ρ̄ = [tanh(βη) + tanh(β(ρ̃ − η))] / [tanh(βη) + tanh(β(1 − η))]
```
`η=0.5` est le seuil, `β` la netteté. Avec **continuation de β** (1 → 2 → … → 16),
la projection passe d'une rampe douce à un quasi-échelon → design binaire. Dans le
démonstrateur cooling jacket, la fraction « grise » tombe de 100 % à ~5 %.

La chaîne complète de design est donc : `ρ` (variable MMA) → filtre → `ρ̃` →
Heaviside → `ρ̄` (utilisée par la physique). Les gradients se propagent en sens
inverse par la règle de chaîne (`dJ/dρ = filterᵀ · Heavisideᵀ · dJ/dρ̄`).

---

## 2. La physique (éléments finis)

Chaque « bloc » physique est un solveur FEM sur la même grille structurée. Tous
partagent l'élément trilinéaire (hexaèdre H8 en 3D, Q4 en 2D axisymétrique).

### 2.1 Élasticité linéaire
`−∇·σ = f`, `σ = D(ρ) ε`, `ε = ½(∇u + ∇uᵀ)`. Discrétisé : `K(ρ) U = F`, avec
`K = Σ_e E_e KE0` (KE0 = matrice de rigidité élémentaire à module unité). En 3D :
hexaèdre H8, 24 DOF/élément. Solveur : direct (Cholesky `LDLᵀ`) en 2D/petit, ou
**CG matrix-free** sur GPU en 3D grande échelle (cf. §7).

### 2.2 Conduction thermique
`−∇·(k(ρ)∇T) = q` : Laplacien pondéré par la conductivité. `K_t(ρ) T = Q`.
Matrice élémentaire = Laplacien H8. Validé par patch test (champ uniforme) et
gradient linéaire analytique.

### 2.3 Couplage thermo-élastique
La température crée une **dilatation** `ε_th = α(T − T_ref)`, donc une contrainte
thermique. On l'assemble comme une **force thermique équivalente** :
`F_th = Σ_e E_e α C_e (T_e − T_ref)`. L'équilibre devient `K_e U = F_méca + F_th`.
Couplage **unidirectionnel** (one-way) : T influence U, pas l'inverse. Validé par
le patch test de dilatation libre (contrainte nulle sous ΔT uniforme).

### 2.4 Contrainte de stress (von Mises)
La contrainte de von Mises par élément `σ_vM = √(sᵀ V s)` (s = contrainte au
centroïde, V la forme quadratique de von Mises). Deux difficultés :
- **Singularité de stress** : quand `ρ→0`, `σ/ρ → ∞` artificiellement →
  l'optimiseur refuse de créer du vide. On **relaxe** (qp-approach, Bruggi 2008) :
  `σ̃_e = ρ_e^q σ_vM` → la contrainte relaxée s'annule dans le vide, la singularité
  disparaît.
- **Contrainte ponctuelle** : von Mises est une contrainte *par élément* (des
  millions). On **agrège** par une p-norm : `σ_PN = (Σ σ_e^P)^{1/P} ≈ max σ_e`
  (P = 8), une seule contrainte différentiable qui approxime le maximum.

### 2.5 Fluide : Stokes incompressible + Brinkman
Écoulement visqueux (bas Reynolds) : `−μ∇²u + ∇p = f`, `∇·u = 0`. Système
**point-selle** (saddle-point, indéfini).
- **Éléments Q1-Q1 stabilisés PSPG** : vitesse et pression au même ordre (mêmes
  nœuds), cohérent avec la grille structurée. Q1-Q1 viole la condition **inf-sup**
  (Babuška-Brezzi) → mode de pression parasite (damier de pression). On ajoute une
  **stabilisation PSPG/Brezzi-Pitkäranta** `∫ τ ∇q·∇p` qui tue ce mode. Validé sur
  Poiseuille (profil parabolique) et confirmé que la stabilisation est décisive.
- **Brinkman penalization** : pour que la **frontière fluide-solide devienne une
  variable de design**, on ajoute un terme `α(ρ)u` au moment. Dans le solide
  (`ρ→0`) `α→α_max` énorme → `u→0` (pas d'écoulement) ; dans le fluide `α→0` →
  Stokes normal. C'est ce qui laisse l'optimiseur **creuser les canaux**. Validé
  sur l'écoulement de Darcy-Brinkman (profil analytique en cosh).

### 2.6 Transfert de chaleur conjugué (CHT)
Le fluide **transporte** la chaleur (advection) tandis que le solide la **conduit** :
`−∇·(k(ρ)∇T) + u·∇T = Q`. L'advection dominante crée des oscillations → stabilisation
**SUPG** (l'analogue thermique du PSPG). Validé sur l'advection-diffusion 1D (profil
exponentiel, fonction du nombre de Péclet), avec démonstration du piège : sans SUPG
le champ oscille, avec SUPG il est propre.

---

## 3. La méthode adjointe : le cœur

### 3.1 Le problème
On veut `dJ/dρ_e` pour **chaque** élément (des millions), afin de faire une descente
de gradient. Par différences finies, il faudrait *un solve FEM par variable* →
impraticable. La **méthode adjointe** calcule le gradient **complet** en **un seul
solve supplémentaire** (l'adjoint), indépendamment du nombre de variables.

### 3.2 Principe (cas mono-physique)
Objectif `J(ρ, U)` avec la contrainte `R(ρ,U) = K(ρ)U − F = 0`. On forme le
lagrangien `L = J + λᵀR`. En choisissant `λ` (le **champ adjoint**) tel que le terme
en `dU/dρ` s'annule, on obtient :
```
Adjoint :   Kᵀ λ = −∂J/∂U
Gradient :  dJ/dρ_e = ∂J/∂ρ_e + λᵀ (∂K/∂ρ_e) U
```
Le gradient ne coûte qu'**un solve adjoint** (`Kᵀλ = …`) puis un produit local par
élément. Pour la compliance `J = FᵀU`, le problème est **auto-adjoint** (`λ = −U`),
gradient quasi-gratuit.

### 3.3 Adjoint multi-bloc (thermo-élastique)
Avec deux physiques couplées (thermique → structure), l'adjoint devient une
**cascade inverse** : l'adjoint élastique `λ_e` pilote l'adjoint thermique `λ_t`
via le terme de couplage. Le gradient somme les contributions des deux blocs. Le
piège classique est d'oublier le terme `∂F_th/∂ρ` (la dépendance de la force
thermique à la densité) — sa présence est vérifiée par différences finies.

### 3.4 Adjoint triple-couplé (le point le plus dur)
La cascade multiphysique complète est **one-way** :
```
ρ ──► Stokes-Brinkman ──► (u,p) ──► CHT ──► T ──► thermo-élastique ──► U
```
L'adjoint la remonte à l'envers :
```
1.  K_eᵀ λ_e = −∂J/∂U                     (adjoint élastique)
2.  K_tᵀ λ_t = Gᵀ λ_e                      (adjoint thermique, piloté par λ_e ;
                                             l'advection se transpose : u·∇ → −u·∇)
3.  Aᵀ λ_s = −(∂R_t/∂u)ᵀ λ_t               (adjoint Stokes, piloté par λ_t via le
                                             couplage advection-vitesse)
Gradient : dJ/dρ = λ_eᵀ(…) + λ_tᵀ(…) + λ_sᵀ(…)   (somme des trois blocs)
```
Le terme le plus délicat est `∂R_t/∂u` : la dépendance du résidu thermique à la
vitesse (car le fluide advecte la chaleur), qui **couple l'adjoint thermique à
l'adjoint fluide**. C'est le cœur mathématique du projet, et le point où une erreur
passe le plus facilement inaperçue — d'où la validation systématique (§5).

### 3.5 Objectifs multiples
Chaque objectif/contrainte a **son propre adjoint** (même machinerie, RHS
différent) : compliance (`J=FᵀU`), stress p-norm, **dissipation visqueuse**
(`Φ = ½∫(μ|∇u|² + α|u|²)`, pour la TO fluide), **température de paroi max** (`T_max`,
p-norm de T dans le solide). Tous validés indépendamment.

---

## 4. L'optimiseur : MMA

Une fois le gradient disponible, on met à jour `ρ`. Pour une seule contrainte
(volume), l'**Optimality Criteria** (OC) par bissection suffit. Pour le
**multi-contraintes** (masse + von Mises + T_max + ΔP), on utilise la **Method of
Moving Asymptotes** (MMA, Svanberg 1987) :

À chaque itération, MMA construit une **approximation convexe séparable** de
l'objectif et des contraintes, à l'aide d'**asymptotes mobiles** `L_j < ρ_j < U_j`
(resserrées si la variable oscille, élargies si elle progresse de façon monotone).
Le sous-problème convexe séparable qui en résulte se résout efficacement par sa
**fonction duale** (bissection pour 1 contrainte, Newton pour plusieurs). MMA est
robuste et gère naturellement plusieurs contraintes — d'où son statut de standard
en TO. Validé contre l'optimum analytique d'un problème séparable (accord à ~1e-14).

---

## 5. La discipline de validation (ce qui fait la rigueur)

C'est le point qui distingue un solveur *fiable* d'une démo douteuse — et le signal
recruteur le plus fort. Chaque brique est validée contre un **oracle indépendant**
avant d'être utilisée :

### 5.1 Oracles analytiques (les solveurs physiques)
- **Patch test** FEM : champ uniforme reproduit à la précision machine.
- **Poutre / cylindre de Lamé** : flèche / contraintes analytiques (< quelques %).
- **Poiseuille** (Stokes), **Darcy-Brinkman** (cosh), **advection-diffusion**
  (exponentiel) : solutions analytiques quantitatives.

### 5.2 Différences finies (les gradients adjoints) — les « gates »
Un adjoint faux peut produire un design qui *semble* converger mais est aberrant.
**Règle absolue** : avant tout usage, chaque gradient adjoint est comparé aux
**différences finies centrées** sur un petit cas, élément par élément. Tolérance
`< 1e-3` (souvent atteinte à `1e-6`–`1e-9`). Sept gates adjoints ont été franchis :
compliance, stress 3D, stress axisymétrique, **triple-couplé**, dissipation, T_max.

Subtilité capturée en cours de route (LL-009) : sur les éléments à gradient
quasi-nul, l'erreur *relative* DF explose par simple arrondi ; on juge alors sur
l'**accord absolu** (nombre de chiffres significatifs), qui est indépendant du
stencil. Le sens de variation de l'erreur avec le pas `ε` distingue l'arrondi (décroît)
d'un vrai bug (croît).

### 5.3 Validation contre la littérature
Reproduction qualitative du **diffuseur de Borrvall-Petersson 2003** (TO fluide
canonique) : le canal convergent lisse attendu émerge, dissipation 97 % meilleure
que le design uniforme.

---

## 6. Vue d'ensemble : le pipeline complet

```
   ┌─────────────────────────────── boucle d'optimisation ───────────────────────────────┐
   │                                                                                       │
   ρ (design) ─► filtre (mm) ─► Heaviside(β) ─► ρ̄                                          │
   │                                            │                                          │
   │                            ┌───────────────┴───────────────┐                          │
   │                            ▼                               ▼                          │
   │                  cascade physique                    contraintes                      │
   │        Stokes-Brinkman ─► u ─► CHT ─► T ─► élasto ─► U      volume, von Mises, T_max   │
   │                            │                                                          │
   │                            ▼                                                          │
   │                     objectif J + gradient ∇J  ◄── ADJOINT triple-couplé (validé DF)   │
   │                            │                                                          │
   │                            ▼                                                          │
   │                     MMA (multi-contraintes) ──────────► nouveau ρ ────────────────────┘
   │
   └─► convergence ─► design final ─► export STL (marching cubes) / VTK (ParaView)
```

---

## 7. Passage à l'échelle : GPU Apple Silicon

En 3D à haute résolution (128³ ≈ 6 M DOF), le solveur linéaire domine. Choix
retenus (Phases 2-3) :
- **Matrix-free** : la matrice `K` n'est **jamais assemblée** ; le produit `K·u`
  est recalculé à la volée par élément. À 128³, cela coûte ~150 MB au lieu de
  2-4 GB pour une matrice creuse assemblée, et évite les écritures concurrentes
  (atomics).
- **CG préconditionné Jacobi** sur GPU Metal (mémoire unifiée Apple Silicon), en
  précision `float32`. C'est pourquoi `E_min` doit rester borné (`1e-4`) : un
  contraste de rigidité trop violent rend le CG float32 non convergent (leçon LL-006).
- **Multi-grille warm-start** (Phase 3) : optimiser d'abord sur grille grossière
  (rapide), puis interpoler et raffiner → le design est quasi-convergé dès les
  niveaux grossiers, l'optimisation 128³ passe sous les 10 minutes.

*Note d'état : les oracles de validation (adjoints, gates) tournent en CPU double
précision pour la propreté numérique ; le portage GPU float32 des adjoints
multiphysiques est un travail de production ultérieur, à revalider contre le chemin
CPU.*

---

## 8. Limitations honnêtement documentées

- **Stokes seul** (pas de Navier-Stokes) : valide pour les écoulements lents/visqueux.
- **Q1-Q1 PSPG** : élément égal-ordre, ne conserve pas *strictement* la masse à
  travers un saut de Brinkman fort → α_max modéré pour les écoulements traversants
  (LL-011). Taylor-Hood serait plus robuste au prix de la simplicité structurée.
- **Adjoint SUPG** exclu des gates (validés à Péclet modéré sans SUPG) : à
  différentier pour la production haute-Péclet.
- **Démonstrateurs** sur géométries cartésiennes simplifiées ; la vraie géométrie
  de tuyère (alésage profilé convergent-divergent) est un raffinement.

---

## 9. Références clés
- Bendsøe & Sigmund 2003, *Topology Optimization* (SIMP, densités).
- Andreassen et al. 2011 (88-line, base MBB).
- Lazarov & Sigmund 2011 (filtre Helmholtz).
- Wang, Lazarov & Sigmund 2011 (projection Heaviside).
- Svanberg 1987 (MMA).
- Duysinx & Bendsøe 1998 ; Bruggi 2008 (stress, relaxation).
- Le, Norato, Bruns 2010 (p-norm stress).
- Borrvall & Petersson 2003 (TO fluide, Brinkman).
- Dilgen et al. 2018 (TO multiphysique fluide-thermique adjointe).
- Aage, Andreassen, Lazarov 2015 (large-scale, matrix-free, multigrid).

---

## 10. Pour aller plus loin (prompts d'approfondissement)
Chaque phase a un rapport détaillé (`TopOptP*/PHASE_*_REPORT.md`) avec les choix,
les validations chiffrées et des **prompts prêts à l'emploi** pour approfondir les
points différés (contraintes cooling jacket complètes, vraie géométrie de tuyère,
adjoint SUPG, portage GPU). La cartographie des phases : `orchestration/ROADMAP.md`.
