# TopOpt — Guide d'utilisation

*Guide praticien complet : installer, écrire un `.topopt.json`, lancer le solveur
(CLI ou Studio), visualiser (ParaView ou viewer intégré), et régler chaque
paramètre en connaissance de cause. La source de vérité des champs et défauts est
`src/io/ProblemSpec.hpp` ; les comportements décrits ici sont ceux de
`src/apps/topopt_run.cpp` et `src/io/BCResolver.hpp`. Rien d'autre ne fait foi.*

Sommaire :
1. [Démarrage](#1-démarrage)
2. [Les quatre dispatches du solveur](#2-les-quatre-dispatches-du-solveur)
3. [Conventions pièges](#3-conventions-pièges--à-lire-avant-tout-run)
4. [Référence JSON complète](#4-référence-json-complète)
5. [Sélecteurs de conditions aux limites](#5-sélecteurs-de-conditions-aux-limites)
6. [Les six exemples commentés](#6-les-six-exemples-commentés)
7. [Visualiser dans ParaView](#7-visualiser-dans-paraview)
8. [TopOpt Studio en détail](#8-topopt-studio-en-détail)
9. [Points futurs à modifier](#9-points-futurs-à-modifier)

---

## 1. Démarrage

### 1.1 `setup.sh` — bootstrap d'un clone frais

```sh
./setup.sh          # vérifie l'environnement, npm install (Studio), make -j (solveur)
./setup.sh --test   # idem, puis la suite de validation CPU complète (make test_cpu)
```

Le script est idempotent. Ordre des étapes (important en cas d'échec partiel) :

1. **Vérifications** : macOS/Apple Silicon (warning sinon), `clang` + `make`
   (erreur fatale sinon : `xcode-select --install`), Node.js ≥ 20 + npm.
2. **Studio d'abord** (`cd web && npm install`) — un échec du build solveur ne
   bloque jamais l'UI.
3. **Préflight Metal Toolchain** : les CLT/Xcode récents livrent le compilateur
   `metal` comme composant séparé. Si `xcrun -sdk macosx metal --version` échoue,
   le script tente `xcodebuild -downloadComponent MetalToolchain`. Si cela échoue
   aussi (Xcode complet parfois requis), **le build du solveur est sauté** avec
   un warning explicite : le Studio reste 100 % fonctionnel pour l'authoring,
   l'import/export, le viewer et les **runs distants** (panneau Run → Distant).
   Pour réparer plus tard : installer Xcode, `xcodebuild -downloadComponent
   MetalToolchain`, puis relancer `./setup.sh`.
4. **Build** : `make -j` (binaires + `shaders.metallib`), puis `make test_cpu`
   si `--test` (17 binaires : oracles analytiques + 7 gates adjoints DF).

### 1.2 CLI directe

```sh
./build/topopt_run <problem.topopt.json>
```

- Sortie dans `output.dir` du spec (créé si absent) : `<dir>/<meta.name>.vti`
  (+ `.stl`, `.png` selon dispatch et `output.formats`).
- Codes de retour : `0` succès ; `2` usage (argument manquant) ; `1` erreur
  (JSON illisible, contrainte manquante, sélecteur non supporté, pas de device
  Metal…) ; `3` combinaison `dim`/`physics` non supportée.
- La progression s'imprime sur stdout (colonnes par dispatch, cf. §2) et se
  termine par un bloc résumé avec le statut de chaque contrainte
  (`ACTIVE` / `slack` / `VIOLATED`).

### 1.3 Studio : auteur → run (local/distant) → viewer

```sh
cd web && npm run dev            # l'UI : http://localhost:5173
node server/run-server.mjs       # le serveur de run (local, port 8787)
# machine distante (repo cloné + ./setup.sh) :
node server/run-server.mjs --host 0.0.0.0
```

Boucle complète sans quitter le navigateur : éditer le problème dans l'onglet
**editor**, cliquer **Run** (cible Local ou Distant), suivre le log de
convergence en direct (SSE), puis charger les artefacts produits d'un clic dans
l'onglet **results**. Détails au §8.

### 1.4 Le workflow complet en 5 minutes (mbb3d)

```sh
./setup.sh                                      # une fois
./build/topopt_run examples/mbb3d.topopt.json   # ~1 min sur M-series
#   topopt_run 'mbb3d': 60x20x20 (24000 elems), obj=compliance vol<=0.30, 60 iter
#     it   1 | C=63.7... | vol=0.3000 | change=0.2000
#     ...
#   done: C=18.5216 vol=0.3000 -> output/mbb3d/mbb3d.{vti}
open output/mbb3d/mbb3d.vti                     # ParaView (ou glisser dans le Studio)
```

Valeur d'ancrage du projet : **C = 18.5216, vol = 0.3000** (poutre MBB 3D,
reproduite à l'identique par l'export Studio — critère d'acceptation M1).
Dans ParaView : filtre *Cell Data to Point Data* puis *Contour* sur `density`
à 0.5 → la poutre treillis apparaît.

---

## 2. Les quatre dispatches du solveur

`topopt_run` choisit la branche d'exécution sur `meta.dim` + `physics`
(cf. `main()` de `src/apps/topopt_run.cpp`) :

| Dispatch | Déclencheur | Matériel | Objectif effectif | Optimiseur effectif | Champs exportés (.vti) |
|---|---|---|---|---|---|
| **v1 structurel** | `dim:"3d"`, `physics:["elastic"]` | GPU Metal (CG matrix-free, float) | compliance min sous volume | OC (SIMP3D) | `density` |
| **v2 thermo-élastique** | `dim:"3d"`, `physics` ⊇ {thermal, elastic}, sans fluid | CPU double | masse min sous von Mises | MMA | `density`, `vonMises`, `temperature`, `displacement` |
| **v3 fluide-thermique** | `dim:"3d"`, `physics` ⊇ {fluid, thermal, elastic} | CPU double | compliance min sous m ≥ 1 contraintes | MMA | `density`, `speed`, `temperature`, `displacement` |
| **axi structurel** | `dim:"axi"`, `physics:["elastic"]` | CPU double | masse min sous von Mises | MMA | `density` (+ `.png` coupe, `.stl` révolu) |

Toute autre combinaison → erreur explicite, code 3. `dim:"axi"` n'accepte que
`["elastic"]`.

Colonnes de convergence imprimées :
- v1 : `it | C | vol | change` (toutes les 10 it ; **arrêt anticipé** si
  `change ≤ 0.01`).
- v2 : `it mass sigmaPN beta g1 gray` (toutes les 5 it), puis
  `best feasible design: mass=… (beta=…)`.
- v3 : `it J J/Jref beta fluid g:volume g:tmax … gray`, puis un résumé
  par contrainte avec l'« active set ».
- axi : `it mass sigma_PN beta g gray`, plus le diagnostic
  `filled wall fraction: throat/ends`.

---

## 3. Conventions pièges — à lire avant tout run

> **⚠️ PIÈGE N°1 — la convention densité s'inverse en v3.**
> En v1/v2/axi : `ρ = 1` = **matière**, `ρ = 0` = vide. En v3 (fluide,
> pénalisation de Brinkman) : `γ = 1` = **fluide** (canal), `γ = 0` = solide.
> Le champ `density` du `.vti` v3 est γ : une iso-surface à 0.5 y montre les
> **canaux de refroidissement**, pas la matière. La contrainte `volume` en v3
> borne la **fraction de fluide**, pas la fraction de matière.

> **⚠️ PIÈGE N°2 — en axisymétrique, l'axe r = 0 n'est jamais maillé.**
> Le domaine est un anneau `[a, b]` (ou la bande de tuyère
> `r_in(z) ≤ r ≤ r_in(z)+wall`), avec `a > 0` obligatoire. Sur le PNG de coupe
> exporté, le bord gauche de la demi-vue est la **paroi interne r = r_in**,
> **PAS** l'axe de révolution — l'axe est le trait gris central à r = 0, hors
> domaine ; le noir entre l'axe et la matière est l'alésage.

> **⚠️ PIÈGE N°3 — une force est TOTALE, répartie sur la sélection.**
> `bc.loads[].value` est la force **totale** sur l'ensemble des nœuds
> sélectionnés, divisée équitablement (`BCResolver::loadVector`) — convention
> MBB. Doubler la résolution ne change pas la charge totale ; sélectionner une
> face au lieu d'une arête répartit la même force sur plus de nœuds. Idem pour
> les sources thermiques `Q` (`thermalSource`).

> **⚠️ PIÈGE N°4 — Dirichlet thermique homogène (T = 0) uniquement.**
> L'adjoint deux-blocs condense les nœuds Dirichlet à T = 0 : une entrée
> `{"dof":"T", "value": 50}` est acceptée mais la **valeur non nulle est
> ignorée** (documenté dans `BCResolver::thermalFixedNodes`). Pour un écart de
> température, jouer sur les sources `Q` et `Tref`. Les profils T(r) imposés
> sont un point futur (§9).

> **⚠️ PIÈGE N°5 — calibrer les bornes multi-contraintes (leçon
> `cooling_jacket_full`).**
> Des bornes `tmax` et `vonmises` serrées **simultanément** peuvent être
> physiquement inatteignables : les canaux qui refroidissent amincissent les
> ligaments porteurs et font monter la p-norm de contrainte (J_σ libre 0.1247
> monte à 0.216 sous tmax=13 ; `vonmises ≤ 0.16` devient alors impossible —
> vérifié par runs d'essai, cf. le `_comment` de l'exemple). **Méthode** :
> (1) run de sonde avec bornes très lâches (seule `volume` active) ; (2) lire
> les valeurs libres dans le résumé (`constraint … val=…`) ; (3) serrer chaque
> borne de ~25 % sous sa valeur libre, une à la fois, en re-sondant après
> chaque ajout. Une contrainte `VIOLATED` au résumé final = borne inatteignable.

---

## 4. Référence JSON complète

Un fichier `.topopt.json` est un objet à 8 sections : `meta`, `domain`,
`material`, `physics`, `bc`, `filter`, `optimize`, `output`. **Tout champ absent
prend son défaut** (`ProblemSpec.hpp`) — le schéma est rétro/avant-compatible ;
les champs inconnus sont ignorés (les clés `_comment` des exemples servent de
documentation embarquée).

### 4.1 `meta`

| Champ | Type | Défaut | Rôle | Effets / pièges |
|---|---|---|---|---|
| `name` | string | `"problem"` | Nom du problème ; base des fichiers de sortie (`<dir>/<name>.vti`…) | Via le serveur de run : doit matcher `[A-Za-z0-9._-]+` (sinon 400). |
| `dim` | string | `"3d"` | `"3d"` (hexa H8) ou `"axi"` (Q4 r-z) | Sélectionne le dispatch (§2). En axi, `grid[2]` est ignoré. |
| `version` | int | `1` | Version du schéma (rétro-compatibilité) | Purement déclaratif aujourd'hui ; incrémenter si le contrat évolue. |

### 4.2 `domain`

| Champ | Type | Défaut | Rôle | Effets / pièges |
|---|---|---|---|---|
| `grid` | [int,int,int] | `[1,1,1]` | Résolution en éléments : `[nelx,nely,nelz]` (3D) ou `[nr,nz,ignoré]` (axi) | ↑ = plus de détails et un coût qui monte vite (v1 GPU : ~O(n) par it ; v2/v3 CPU direct : bien plus raide). Le défaut `[1,1,1]` est inutilisable : toujours le fournir. |
| `size_mm` | [double×3] | `[1,1,1]` | Dimensions physiques. 3D : le rayon de filtre est converti en cellules via **`size_mm[0]/grid[0]`** (taille de cellule en x). Axi : `size_mm[1]` = H (hauteur axiale) ; `geometry:"box"` → `size_mm[0]` = a (rayon interne), `size_mm[2]` = b (rayon externe) | Piège 3D : seule la cellule **x** sert à convertir le filtre — garder des cellules cubiques (`size_mm` proportionnel à `grid`) pour un filtre isotrope. Axi : il faut `0 < a < b`, H > 0. |
| `geometry` | string | `"box"` | `"box"` (parallélépipède / anneau) ou `"nozzle"` (bande convergent-divergent, **axi seulement**) | En axi + `"nozzle"`, `size_mm[0]`/`[2]` sont ignorés (la bande vient de `nozzle`). Autre valeur en axi → erreur. En 3D, seul `"box"` a un sens. |
| `nozzle.r_throat` | double | `0.6` | Rayon interne au col (z = H/2) : `r_in(z) = r_throat + K·(z−H/2)²` | ↑ = alésage plus large partout ; en axi c'est aussi le a effectif. |
| `nozzle.K` | double | `0.20` | Courbure de la parabole du profil | ↑ = évasement plus marqué aux extrémités (col plus « pincé » relativement). `K=0` → cylindre. |
| `nozzle.wall` | double | `0.70` | Épaisseur radiale de la bande design | ↑ = plus de matière disponible → masse optimale relative ↓, mais domaine plus grand. C'est le b−a effectif. |

### 4.3 `material`

Tous les paramètres physiques sont ici ; les blocs de physique inactifs ignorent
les leurs. Unités adimensionnées (E0 = 1 de référence dans tous les exemples).

| Champ | Type | Défaut | Rôle | Effet d'un changement | Plage usuelle / pièges |
|---|---|---|---|---|---|
| `E0` | double | `1.0` | Module d'Young du solide plein | Échelle linéaire de la raideur ; C et σ scalent avec. | Garder 1.0 et adimensionner les charges. |
| `Emin` | double | `1e-4` | Module résiduel du vide (SIMP : `E = Emin + ρᵖ(E0−Emin)`) | ↑ = vide plus raide → moins de contraste, designs plus flous ; ↓ trop (1e-9) = systèmes mal conditionnés (CG lent, solves raides). | 1e-4…1e-6 de E0. |
| `nu` | double | `0.3` | Coefficient de Poisson (élasticité, modèles de von Mises) | Effet de second ordre sur la topologie ; change σ_vM. | 0.2–0.4. |
| `penal` | double | `3.0` | Exposant SIMP p (et exposant q de la conductivité en v2 : `mat.q = penal`) | ↑ = pénalise le gris → designs plus binaires mais problème plus non-convexe (minima locaux, oscillations) ; ↓ (→1) = problème convexe mais designs gris inutilisables. | 3 est le standard. La **continuation en p** n'est pas câblée dans `topopt_run` (cf. §4.9) : c'est la continuation **Heaviside β** qui joue ce rôle ici. |
| `k_solid` | double | `1.0` | Conductivité thermique du solide (v2 : matière ; v3 : γ=0) | ↑ = chaleur mieux évacuée par conduction → T ↓, contrainte `tmax` moins tendue, thermo-dilatation mieux répartie. | — |
| `k_fluid` | double | `0.3` | Conductivité du fluide (v3, γ=1 ; la chaleur y est aussi advectée) | ↑ = les canaux conduisent plus (en plus de l'advection). Le ratio `k_solid/k_fluid` pilote où la chaleur passe. | v3 uniquement ; ignoré en v2. |
| `alpha_th` | double | `1e-3` | Coefficient de dilatation thermique | ↑ = charges thermiques ↑ → σ thermo-mécanique ↑ ; à 0 le couplage T→U disparaît. | Le levier direct du poids « thermique » dans σ_vM. |
| `Tref` | double | `0.0` | Température de référence (déformation thermique ∝ T−Tref) | Décale le champ de contrainte thermique. | Laisser 0 avec Dirichlet T=0 (piège n°4). |
| `mu` | double | `1.0` | Viscosité dynamique (Stokes, v3) | ↑ = écoulement plus lent à drive égal → advection ↓ (T ↑), dissipation Φ ↑. | — |
| `brinkman_max` | double | `1e2` | Traînée de Brinkman maximale α_max sur les cellules solides (γ=0) | ↑ = solide plus « étanche » (moins de fuite de fluide à travers la matière) mais système de Stokes plus raide et gradients plus abrupts ; ↓ = fuite (l'écoulement traverse le solide, résultats non physiques). | Les exemples v3 utilisent **500** ; la calibration historique donne un sweet spot vers 1e4 (fuite 0.47 %) sur des cas plus fins — augmenter prudemment. |
| `brinkman_q` | double | `0.1` | Paramètre de convexité de l'interpolation RAMP de α(γ) | ↓ = interpolation plus abrupte (transition fluide/solide raide, plus dure à optimiser) ; ↑ = plus lisse (plus de gris à l'interface). | 0.01–1. |

### 4.4 `physics`

| Champ | Type | Défaut | Rôle |
|---|---|---|---|
| `physics` | [string] | `["elastic"]` | Sous-ensemble de `{"elastic","thermal","fluid"}`. Le couplage est **déduit** de la cascade one-way validée `fluid → thermal → elastic` ; la liste sélectionne le dispatch (§2). |

Combinaisons valides : `["elastic"]` (v1 ou axi), `["thermal","elastic"]` (v2),
`["fluid","thermal","elastic"]` (v3). `["fluid"]` seul ou `["thermal"]` seul ne
sont pas dispatché·s.

### 4.5 `bc` — vue d'ensemble (syntaxe détaillée au §5)

Cinq listes : `fixed` (Dirichlet mécanique homogène), `loads` (forces),
`pressure` (pression d'alésage, **axi uniquement** — ignorée par les dispatches
3D), `thermal` (Dirichlet T=0 et sources Q), `flow` (parois/slip/datum de
pression + drive). Chaque entrée porte **un** sélecteur (`face` | `edge` |
`node` | `region`), un `dof`, et une valeur (`value`, ou alias `T`/`Q` — le
parseur lit `value`, sinon `T`, sinon `Q`).

Le champ `body_force` de `ProblemSpec` ([double×3], défaut `[0,0,0]`) n'est pas
une clé JSON directe : il est rempli par une entrée `{"drive":[fx,fy,fz]}` dans
`bc.flow` — la force volumique qui pousse le fluide (Stokes). Si plusieurs
entrées `drive` existent, la dernière gagne. ↑ drive = écoulement plus rapide →
advection ↑ (refroidissement ↑) mais dissipation Φ ↑.

### 4.6 `filter`

| Champ | Type | Défaut | Rôle | Effet d'un changement |
|---|---|---|---|---|
| `radius_mm` | double | `1.5` | Rayon du filtre de densité **en mm** (indépendant du maillage). v1 : filtre Helmholtz GPU ; v2/v3 : filtre chapeau linéaire ; conversion en cellules par `size_mm[0]/grid[0]` (3D) ou par la cellule axiale `hz` (axi) | **Trop petit (< ~1.5 cellule)** : damiers (checkerboard) et dépendance au maillage — le design change quand on raffine. **Trop grand** : les détails plus fins que le rayon sont impossibles (membres épais, perte de finesse), plus de gris aux frontières. Règle : ≥ 1.5× la taille de cellule, ≈ la taille du plus petit membre voulu. C'est aussi une contrainte de fabrication implicite (épaisseur mini). |
| `heaviside.beta` | [double] | `[]` (pas de projection) | **Continuation** de la projection Heaviside (Wang-Lazarov-Sigmund) : la liste est étalée uniformément sur `max_iter` (`stages` paliers de `max(1, max_iter/stages)` itérations ; ex. 60 it et `[1,2,4,8,16]` → 12 it par palier) | β ↑ = projection plus raide → moins de gris (fraction grise ↓), design plus binaire. Passer directement à β élevé sans continuation = blocage dans un minimum local ; c'est **pour ça** qu'on donne une liste croissante (doublement classique). Liste vide : β=1 en v2/v3/axi (projection quasi neutre) ; en v1 il n'y a **pas** de projection du tout. |
| `heaviside.eta` | double | `0.5` | Seuil de la projection (le niveau de ρ̃ projeté vers 0.5) | ↓ = « dilate » (plus de matière au seuil), ↑ = « érode ». 0.5 = neutre. Décaler η±Δ est la base de l'approche robuste érosion/dilatation (§9). |

### 4.7 `optimize`

| Champ | Type | Défaut | Rôle | Effets / pièges |
|---|---|---|---|---|
| `objective` | string | `"compliance"` | Fonction objectif | v1 : compliance (la valeur est affichée mais la branche minimise toujours la compliance sous volume). v2 : masse (imposé par la branche). v3 : compliance (imposé). axi : **doit** être `"mass"` (erreur sinon). |
| `constraints` | [objet] | `[]` | Liste `{type, max, max_rel}` (défauts par entrée : `max=0`, `max_rel=0`) | Voir tableau ci-dessous. |
| `optimizer` | string | `"mma"` | Déclaratif : l'optimiseur effectif est fixé par le dispatch (v1 = OC, v2/v3/axi = MMA), la valeur n'est pas branchée. | Le garder cohérent (`"oc"` dans mbb3d) pour la lisibilité et le Studio. |
| `max_iter` | int | `60` | Nombre d'itérations d'optimisation | ↑ = designs plus convergés/binaires (et paliers β plus longs) ; coût linéaire. v1 s'arrête aussi sur `change ≤ 0.01`. v2/v3/axi font toujours `max_iter` itérations. Trop court = continuation β tronquée → gris élevé. |
| `penal_continuation` | bool | `false` | Prévu pour la continuation de l'exposant SIMP | **Parsé mais non consommé par `topopt_run`** (p reste constant) ; la continuation effective est celle de β. Champ réservé (§9). |

**Les 4 types de contraintes** (normalisées `g = val/max − 1 ≤ 0`) :

| `type` | Grandeur bornée | Dispatches | Sens et effets |
|---|---|---|---|
| `volume` | v1 : fraction de **matière** `mean(ρ)` ; v3 : fraction de **fluide** `mean(γ)` (piège n°1) | v1 (défaut 0.5 si absente), v3 (défaut 0.5 si aucune contrainte) | ↓ = structure plus légère / moins de canaux ; c'est aussi la valeur de **seed** du champ initial en v1/v3. |
| `vonmises` | p-norm agrégée de σ_vM (relaxation qp : q=0.5, P=8, codés en dur) | v2 (via `max`), v3 (via `max`, cascade triple re-résolue à chaque it — coût ×2 accepté), axi (via `max_rel` **prioritaire** sur `max`) | ↓ = structure plus massive (marges de contrainte). σ_PN sous-estime le vrai max ponctuel (le résumé v2 imprime les deux : ex. σ_PN 0.12 vs vrai max 0.32) — calibrer sur σ_PN, pas sur le max vrai. |
| `tmax` | p-norm de la température de paroi (P=8) | v3 | ↓ = force plus de canaux près des sources de chaleur ; se paie en σ (piège n°5) et en dissipation. |
| `dissipation` | Puissance de pompage Φ = ½ wᵀHw | v3 | ↓ = canaux plus courts/larges (moins de pertes de charge) ; se paie en refroidissement. |

**`max` vs `max_rel`** : `max` est une borne **absolue** ; `max_rel` est
relative à la valeur du **design plein** (σ_lim = max_rel · σ_PN(ρ=1)) et
prioritaire quand > 0. `max_rel` n'est branché **que dans le dispatch axi**
(les branches v2/v3 lisent `max` et v3 exige `max > 0` pour chaque contrainte,
erreur sinon). Intérêt de `max_rel` : la borne suit automatiquement le
chargement et le maillage — `max_rel: 1.6` = « accepter 60 % de plus que le
design plein », garanti actif.

**Paramètres internes non exposés dans le JSON** (utile pour lire les logs et
savoir quoi modifier dans le code) :

| Paramètre | Valeur | Où | Pourquoi |
|---|---|---|---|
| move limit OC | 0.2 | v1 (`SIMP3D`) | Standard MBB. |
| move limit MMA | 0.05 (v2), 0.2 (v3), 0.12 (axi) | `topopt_run.cpp` | La contrainte de stress p-norm est raide et non convexe : petits pas en v2/axi pour ne pas sauter la falaise. |
| Seed ρ initial | volfrac (v1), 0.5 (v2), borne volume (v3), 0.6 (axi) | idem | Démarrer près de la zone active. |
| CG GPU | 4000 it max, tol 1e-4 (float) | v1 | Matrix-free H8 + Jacobi. |
| Relaxation stress | qRelax=0.5, P agrégation=8 | v2/v3/axi | Convention des gates DF. |
| feasTol « best design » | 2 % | v2 | MMA oscille près de la frontière active : on exporte la **meilleure itération faisable** (masse min à g ≤ +0.02), pas la dernière. |
| Profil de pression axi | bump=3, sig=0.18·H | axi | `value` = pic au col ; p0 = value/(1+bump) = value/4. |
| Stabilisation Stokes | PSPG α=1/12, α_min=0 | v3 | Q1-Q1 inf-sup. |

### 4.8 `output`

| Champ | Type | Défaut | Rôle | Effets / pièges |
|---|---|---|---|---|
| `dir` | string | `"output"` | Dossier de sortie (créé récursivement) | Via le serveur de run, il est **réécrit** vers un dossier horodaté `output/<name>-<timestamp>/` (le spec archivé à côté fait foi). |
| `formats` | [string] | `["vti"]` | `"vti"` (VTK ImageData, champs aux cellules) et/ou `"stl"` | v1 : les deux honorés. v2/v3 : `.vti` **toujours** écrit, `stl` ignoré. axi : `.png` toujours écrit ; `vti`/`stl` si demandés (densité révolue voxelisée 72²×nz). |
| `fields` | [string] | `["density"]` | Liste de champs souhaités | **Déclaratif** : chaque dispatch écrit sa liste fixe (§2), le champ n'est pas consommé. Sert au Studio/contrat. |
| `stl_iso` | double | `0.5` | Iso-valeur de densité de la surface STL | ↓ = surface plus « généreuse » (englobe du gris), ↑ = plus maigre. 0.5 = frontière naturelle après projection. |
| `stl_method` | string | `"marching_cubes"` | `"marching_cubes"` (surface lisse) ou `"voxel"` (faces de voxels) | Branché en **v1 uniquement** ; l'axi utilise toujours la surface voxel sur la densité révolue. |

### 4.9 Champs parsés mais non (encore) consommés — honnêteté du contrat

`meta.version`, `optimize.optimizer`, `optimize.penal_continuation`,
`output.fields` sont lus et stockés mais ne changent pas l'exécution
actuelle. Ils font partie du contrat pour les évolutions (§9) et pour le
Studio ; les renseigner correctement reste la bonne pratique. De même
`bc.pressure` n'est actif qu'en axi, et une entrée `flow` sans `dof` autre
que `drive` est ignorée par `stokesFixedDofs`.

---

## 5. Sélecteurs de conditions aux limites

### 5.1 Syntaxe des sélecteurs (`BCResolver`)

Une entrée de BC porte **exactement un** sélecteur :

| Sélecteur | Syntaxe | Sélection |
|---|---|---|
| `face` | `"x-"`,`"x+"`,`"y-"`,`"y+"`,`"z-"`,`"z+"` | Tous les nœuds de la face du domaine. |
| `edge` | deux faces séparées par une virgule, ex. `"x-,y+"` | L'arête intersection des deux faces. |
| `node` | `"corner:x+,y-,z-"` (le préfixe `corner:` est optionnel et retiré) | Le coin intersection des trois faces. |
| `region` | `"axis:lo:hi"` avec axis ∈ {x,y,z}, indices de **nœuds** inclusifs, ex. `"z:8:12"` | Bande volumique intérieure (inaccessible aux sélecteurs de face) — typiquement une source de chaleur à mi-hauteur. |

### 5.2 Quels types dans quel bloc

**`bc.fixed`** — Dirichlet mécanique homogène (u = 0) :

| Champ | Valeurs | Exemple |
|---|---|---|
| `dof` | `"x"`, `"y"`, `"z"` (une composante) ou `"all"` (encastrement) | `{ "face": "x-", "dof": "all" }` — encastrement de la face x− |
| | | `{ "face": "z-", "dof": "z" }` — symétrie/appui plan |
| | | `{ "node": "corner:x+,y-,z-", "dof": "y" }` — anti-rotation ponctuelle |

**`bc.loads`** — forces (Neumann mécanique) :

| Champ | Sens | Exemple |
|---|---|---|
| `dof` | direction de la force (`"x"`/`"y"`/`"z"`, ou `"all"` = même valeur sur les 3) | `{ "edge": "x-,y+", "dof": "y", "value": -1.0 }` — force totale −1 en y sur l'arête (MBB) |
| `value` | force **TOTALE** sur la sélection (piège n°3) | `{ "face": "z+", "dof": "z", "value": 1.69 }` — traction répartie |

**`bc.pressure`** — pression d'alésage (**axi uniquement**) :

| Champ | Sens | Exemple |
|---|---|---|
| `face` | `"inner"` (alias `"r-"`) — seule valeur supportée | `{ "face": "inner", "value": 4.0 }` |
| `value` | pression **au pic** (col) ; profil gaussien codé : `p(z) = p0·(1 + 3·exp(−½((z−H/2)/0.18H)²))`, p0 = value/4 | ↑ = tout le champ de σ scale ; avec `max_rel` la borne suit. |

**`bc.thermal`** — thermique (v2/v3) :

| Champ | Sens | Exemple |
|---|---|---|
| `dof: "T"` | Dirichlet **T = 0** (la valeur est ignorée, piège n°4) | `{ "face": "z-", "dof": "T", "value": 0.0 }` — puits froid |
| tout autre `dof` (convention : `"Q"`) | source de chaleur, `value`/`Q` = puissance **TOTALE** répartie sur la sélection | `{ "region": "z:8:12", "dof": "Q", "value": 3000.0 }` — bande chauffée à mi-hauteur |

**`bc.flow`** — Stokes-Brinkman (v3 ; 4 DOF/nœud : u_x,u_y,u_z,p ; tout est
homogène u=0 / p=0) :

| `dof` | Sens | Exemple |
|---|---|---|
| `"wall"` / `"noslip"` | paroi : les 3 composantes de vitesse fixées à 0 | `{ "face": "x-", "dof": "wall" }` |
| `"slip"` | glissement : seule la composante **normale à la face** est fixée (nécessite un sélecteur `face`) | `{ "face": "y-", "dof": "slip" }` — plan de symétrie |
| `"pressure"` / `"p"` / `"pdatum"` | épingle le datum de pression (p = 0) sur le(s) nœud(s) — indispensable pour rendre le système défini | `{ "node": "corner:x-,y-,z-", "dof": "pressure" }` |
| *(entrée sans `dof`)* `"drive": [fx,fy,fz]` | force volumique qui pousse le fluide (remplit `body_force`) | `{ "drive": [0, 0, 30] }` — poussée axiale du coolant |

Note : il n'y a **pas** d'entrée/sortie à vitesse imposée (`inlet_velocity`
n'est pas parsé) — l'écoulement traversant est modélisé par le `drive`
volumique + faces slip. Les profils d'entrée sont un point futur (§9).

### 5.3 Cas axisymétrique — sélecteurs restreints

Le dispatch axi résout ses BCs directement (pas de BCResolver 2D) et n'accepte
**que** : `fixed` avec `face: "z-"`/`"z+"` et `dof: "z"` (tranche plane-strain,
u_z = 0 ; la raideur circonférentielle rend K définie positive sans épingle
radiale), et `pressure` avec `face: "inner"`. Tout autre sélecteur → erreur
explicite listant les formes supportées. Il faut au moins un `fixed` et une
`pressure` avec `value > 0`.

---

## 6. Les six exemples commentés

Tous dans `examples/` ; les chiffres ci-dessous sont ceux du code actuel
(runs de vérification du 2026-07-15, déterministes).

### 6.1 `mbb3d.topopt.json` — v1 structurel, la référence

- **Démontre** : la boucle minimale compliance/volume sur GPU ; la demi-poutre
  MBB (symétrie x−, appui z−, anti-rotation sur l'arête x+,y−, force −1 en y
  sur l'arête haute gauche).
- **Attendu** : `done: C=18.5216 vol=0.3000` en 60 it (arrêt sur max_iter,
  change plafonné au move 0.2). C'est la valeur d'ancrage du Studio (M1).
- **À expérimenter** : `volume.max` 0.3→0.5 (C ↓, poutre plus massive) ;
  `filter.radius_mm` 1.5→0.8 (apparition de membres fins, risque damier) ou →3
  (topologie simplifiée) ; `grid` ×2 avec `size_mm` ×2 (indépendance au
  maillage grâce au rayon en mm).

### 6.2 `bracket_thermo.topopt.json` — v2 thermo-élastique

- **Démontre** : masse min sous von Mises avec charge thermo-mécanique
  combinée : encastrement x−, force −0.05 en z sur x+, puits T=0 sur x−,
  source Q=40 sur x+ ; `alpha_th` monté à 1e-2 pour que le thermique compte ;
  continuation β [1,2,4,8] sur 120 it (30 it/palier).
- **Attendu** : `best feasible design: mass=0.1725 (beta=8)` ; résumé
  `mass=0.1725 sigma_PN=1.1995e-01 sigma_lim=1.2e-01 g1=-0.0004
  (CONSTRAINT ACTIVE)`, vrai max σ_vM ≈ 0.32, T max ≈ 381. Noter l'oscillation
  MMA en fin de run (it 120 momentanément infaisable) — c'est précisément
  pourquoi la branche exporte la meilleure itération faisable, pas la dernière.
- **À expérimenter** : `vonmises.max` 0.12→0.15 (masse ↓) ; `alpha_th` →0
  (problème purement mécanique : comparer la topologie) ; `Q` ×2 (le thermique
  domine, la matière se réorganise vers le puits froid).

### 6.3 `cooling_jacket.topopt.json` — v3 cascade triple, contrainte volume seule

- **Démontre** : la cascade Stokes-Brinkman → CHT → thermo-élastique sur un
  manchon 12×12×20 : parois x±, symétries slip y±, datum de pression au coin,
  drive axial [0,0,30], puits T=0 aux deux bouts, bande chauffée `z:8:12`
  (Q=3000), encastrement z−, traction z+ ; compliance min sous fluide ≤ 0.40.
- **Attendu** : `J: first=1.1690e+01 final=2.7171e+00 (ratio 0.232)`,
  contrainte volume `ACTIVE` (fraction fluide 0.397), gris 0.070, et le
  diagnostic `fluid density: throat=0.623 ends=0.240 ratio=2.60 -> MORE FLUID
  AT THROAT` : l'optimiseur concentre les canaux là où la chaleur arrive (le
  chiffre « canaux au col 2.6× » de `INPUT_LANGUAGE.md`).
- **À expérimenter** : `drive` 30→60 (advection ↑, T ↓, Φ ↑) ;
  `brinkman_max` 500→2000 (moins de fuite, solve plus raide) ; déplacer la
  bande `z:8:12`.

### 6.4 `cooling_jacket_multi.topopt.json` — v3, 3 contraintes

- **Démontre** : la combinaison volume + `tmax` + `dissipation` (MMA dual
  Newton m≥2). Le `_comment` du fichier documente la calibration : valeurs
  libres sondées J_T = 17.54 et Φ = 5.81e4 sur le design volume-seul ;
  `tmax: 13.0` (~26 % sous la valeur libre) et `dissipation: 4.0e4` (~31 %
  sous) pour que les deux finissent **ACTIVE**.
- **Attendu** : résumé avec les trois contraintes dans l'active set (statuts
  `ACTIVE`), plus de canaux autour de la bande chauffée que dans 6.3.
- **À expérimenter** : serrer `tmax` à 11 et constater la montée de Φ (le
  refroidissement se paie en pompage) — c'est l'exercice de calibration du
  piège n°5 en conditions réelles.

### 6.5 `cooling_jacket_full.topopt.json` — v3, 4 contraintes simultanées

- **Démontre** : le cas complet volume + tmax + dissipation + vonmises, avec le
  conflit physique tmax/σ documenté dans le `_comment` : J_σ libre = 0.1247,
  mais sous tmax=13 il monte à 0.216 et `vonmises ≤ 0.16` devient inatteignable
  (vérifié par runs d'essai). Le compromis faisable retenu : `tmax: 16.0`
  (relâché ~9 % sous la valeur libre), `dissipation: 4.0e4`, `vonmises: 0.15`.
- **Attendu** : **les quatre contraintes ACTIVE** à convergence (résumé
  `active set: {volume, tmax, dissipation, vonmises}`) — l'état documenté dans
  `INPUT_LANGUAGE.md` §5. Chaque itération coûte ~2 cascades triples (l'adjoint
  vonmises re-résout la cascade) + les adjoints tmax et dissipation.
- **À expérimenter** : rien à serrer sans re-sonder ; c'est le cas d'école de
  la méthode probe→lire→serrer 25 %.

### 6.6 `nozzle_profiled.topopt.json` — axi, tuyère profilée

- **Démontre** : le dispatch axisymétrique body-fitted : bande design
  convergent-divergent (`r_throat 0.6, K 0.20, wall 0.70`, H=4, grille 24×80),
  plane-strain z± (`dof:"z"`), pression d'alésage profilée piquée au col
  (pic 4.0 → p0=1.0), masse min sous `vonmises max_rel: 1.6` (borne relative
  au design plein — seule branche où `max_rel` est actif).
- **Attendu** : `done: mass=0.2381 sigma_PN=23.0225 g=-0.0000 gray=0.073`,
  `filled wall fraction: throat=0.457 ends=0.188 ratio=2.42 -> THICKER AT
  THROAT` — la paroi s'épaissit au col où la pression pique. Sorties : PNG de
  coupe pleine largeur (piège n°2 pour le lire !), `.vti` et `.stl` révolus
  (72²×80 voxels).
- **À expérimenter** : `max_rel` 1.6→1.3 (masse ↑, marges ↑) ; `K` 0.20→0.35
  (col plus marqué → renforcement plus concentré) ; `geometry:"box"` avec
  `size_mm:[0.6,4.0,1.3]` (anneau cylindrique : retrouve le cas Lamé).

---

## 7. Visualiser dans ParaView

Le `.vti` est un VTK XML ImageData dont les champs sont **aux cellules**
(1 valeur par élément) ; `displacement`, `temperature`, `speed` sont des champs
nodaux moyennés par cellule à l'export.

**Ouvrir** : File → Open → `output/<name>/<name>.vti` → Apply. Choisir le champ
dans la barre de colormap ; représentation *Surface* pour un aperçu.

**Iso-surface de la structure (ou des canaux en v3)** :
1. Le champ étant aux cellules, appliquer d'abord *Filters → Cell Data to
   Point Data* (sinon *Contour* n'est pas disponible sur le champ).
2. *Filters → Contour*, champ `density`, valeur **0.5** → Apply.
3. v1/v2/axi : c'est la **matière**. v3 : ce sont les **canaux** (γ=1 fluide,
   piège n°1). `stl_iso` joue le même rôle pour l'export STL.

**Coupes** : *Filters → Slice*, plan au choix (ex. normal Z à mi-hauteur),
colorier par `temperature`, `vonMises`, `speed` ou `displacement` ; le bouton
*Rescale to Data Range* ajuste la colormap. Empiler plusieurs Slices (X, Y, Z)
donne la lecture volumique rapide.

**Le combo canal + température (cooling jacket)** — la vue signature v3 :
iso-surface `density = 0.5` (les canaux, opacité ~1) **+** une Slice à
mi-hauteur coloriée par `temperature` (colormap type Cool to Warm). On voit
d'un coup d'œil les canaux se densifier autour de la bande chauffée et le
champ de température qu'ils imposent.

**STL** : File → Open → `<name>.stl` (géométrie seule, sans champs) — utile
pour vérifier la surface marching-cubes exportée (v1) ou la paroi révolue
(axi) avant fabrication/CAO.

**Équivalent dans le viewer intégré du Studio** (onglet *results*) : liste des
champs (coupes + échelle), iso-surface avec case on/off, champ dédié et slider
de seuil, trois coupes orthogonales X/Y/Z avec sliders de position, colormaps
viridis/coolwarm + barre d'échelle min/max, chargement `.stl`. À l'ouverture
d'un `.vti`, la vue par défaut est exactement le combo ci-dessus : iso
`density` à 0.5 + coupe à mi-hauteur coloriée par la température (critère
d'acceptation M3). Pas de Cell Data to Point Data à faire : le viewer s'en
charge. ParaView reste l'outil d'analyse avancée (seuils multiples, calculs,
animations) ; le viewer vise la boucle courte auteur→résultat.

---

## 8. TopOpt Studio en détail

Application statique (three.js + vtk.js + lil-gui, Vite, TypeScript). Le type
TS `ProblemSpec` reflète exactement `src/io/ProblemSpec.hpp` (mêmes champs,
mêmes défauts) — tout écart est un bug (golden tests contre les 6 exemples).

### 8.1 Onglets et panneaux

- **editor** : la scène 3D (boîte du domaine, aperçu maillage) + les panneaux :
  - *Sélection* : cliquer une face (x±, y±, z±), une arête (2 faces), un coin ;
    surbrillance au survol, code couleur par type de BC, glyphes 3D (flèches de
    charge, encastrements).
  - *BCs élastiques* : `fixed` (par composante ou `all`) et `loads` (force
    totale + direction) sur la sélection courante. Les BCs thermiques/flow
    importées sont affichées **en lecture seule** (édition UI = V2, §9).
  - *Panneaux propriétés* (lil-gui) : `meta`, `domain`, `material`, `filter`,
    `optimize` (objectifs/contraintes filtrés par la matrice de compatibilité
    physique — un type non admissible pour la physique choisie n'est pas
    proposé), `output`.
  - *Export/Import* : aperçu JSON live, export `.topopt.json`, import
    round-trip sans perte, **galerie de presets** = les 6 exemples du repo
    embarqués, chargeables en un clic.
- **results** : le viewer (§7, dernier paragraphe). On peut aussi glisser-déposer
  un `.vti`/`.stl` n'importe où dans la fenêtre.

### 8.2 Panneau Run

- **Cibles** : *Local* (`127.0.0.1:8787`) et *Distant* (IP + port éditables dans
  l'UI, **mémorisés dans le localStorage du navigateur** — config personnelle,
  jamais dans le repo). `VITE_REMOTE_HOST`/port dans `web/.env.local`
  (gitignoré) pré-remplissent le champ. Chaque cible a sa pastille de santé
  (`GET /api/health`) : vert = serveur + solveur OK, orange = serveur OK mais
  solveur absent, rouge = injoignable.
- **Dossier de sortie** : champ `outputDir` (défaut `output`), transmis au
  serveur qui crée un dossier horodaté `output/<name>-YYYYMMDD-HHMMSS/`
  (suffixe -2, -3… si collision), y **archive le spec exact du run** à côté des
  artefacts, et le garde dans le repo (gitignoré — `git status` reste propre).
- **Run** : POST du spec ; le log de convergence arrive en direct (SSE, le
  serveur lance le solveur sous un pty pour que les lignes streament), avec
  compteur it/max_iter. **Annuler** envoie un SIGTERM au groupe de processus.
- **Artefacts** : à la fin du job, la liste des fichiers produits apparaît ;
  un clic charge le `.vti`/`.stl` directement dans *results* — y compris pour
  un run **distant** (l'artefact est servi par
  `GET /api/jobs/:id/artifacts/<fichier>`).
- **Un seul run à la fois** (le solveur sature la machine) ; les jobs sont en
  mémoire du serveur (perdus à son redémarrage).

### 8.3 Messages d'erreur courants et remèdes

| Symptôme | Cause | Remède |
|---|---|---|
| Pastille rouge, tooltip `injoignable — lancer: node server/run-server.mjs` | serveur non démarré (ou mauvais port) | lancer le serveur ; pour la cible distante : `node server/run-server.mjs --host 0.0.0.0` sur la machine distante. |
| `cible Distant : renseigner l'IP du serveur (champ à côté de « Distant »)` | champ IP vide | saisir IP:port (Tailscale ou LAN) ; persiste ensuite. |
| `serveur injoignable sur http://…` au moment du run | serveur tombé entre le health check et le POST, ou pare-feu | relancer le serveur, vérifier le port (8787 par défaut). |
| HTTP **503** `build/topopt_run introuvable — lancer ./setup.sh sur cette machine` | solveur non compilé sur la machine du serveur (ex. Metal Toolchain manquant, §1.1) | `./setup.sh` sur **cette** machine, ou utiliser une cible distante qui a le binaire. |
| HTTP **409** `un run est déjà en cours (le solveur sature la machine)` | un job tourne déjà (un seul à la fois) | attendre la fin ou *annuler* le job en cours. |
| HTTP 400 `meta.name must be a filesystem-safe identifier` | `meta.name` avec espaces/caractères spéciaux | se limiter à `[A-Za-z0-9._-]`. |
| HTTP 400 `outputDir must stay inside the repo` | chemin de sortie hors du repo | chemin relatif au repo (ex. `output` ou `output/essais`). |
| Job `failed` avec un message `axi: …`/`fluid-thermal: …` dans le log | erreur de spec côté solveur (sélecteur non supporté, contrainte manquante, `max ≤ 0`…) | lire la ligne stderr — les messages listent les formes supportées (§4, §5). |

---

## 9. Points futurs à modifier

Liste honnête, tirée de `INPUT_LANGUAGE.md` (extensions différées), de
`WEB_MODELER_SPEC.md` §7 et de la roadmap du README — rien de tout cela n'est
implémenté aujourd'hui :

1. **Édition UI des BCs thermiques et flow** (Studio V2) : Dirichlet T,
   sources Q par face ou région `axis:lo:hi`, no-slip/slip/datum/drive à la
   souris ; aujourd'hui affichées en lecture seule à l'import.
2. **Éditeur axisymétrique** (Studio V2) : édition du profil r_in(z)
   (`r_throat`, `K`, `wall`) avec aperçu 2D (r,z) et révolution 3D live.
3. **Dirichlet inhomogènes / profils** : le solveur condense en Dirichlet
   homogène (T=0, u=0) + drive volumique ; les « disques d'interface » (profils
   T(r), u(r) paramétrés — uniforme, parabolique, tabulé CSV) demandent une
   extension du contrat (`bc.flow[].profile`, `bc.thermal[].profile`) et
   l'adaptation du RHS des adjoints (les gates existants restent valides).
4. **Fonctionnelles de flux cible** : contraintes de sortie (vitesse axiale
   cible, composante radiale nulle, profil plat sur une section) = nouvelles
   fonctionnelles J avec **nouveaux gates DF** (machinerie cascade réutilisable,
   même schéma que le gate vm-triple).
5. **Géométrie variable (H ∈ [H_min, H_max])** : distance entre interfaces
   comme variable de forme couplée à la TO — adjoint de forme ou boucle externe
   DF (pragmatique pour 1–3 paramètres).
6. **Robustesse** : passe « renforcer au prix de masse » — re-run avec σ_lim
   abaissé (`max_rel` ↓, déjà disponible en axi) ou érosion/dilatation par
   projections Heaviside décalées η±Δ (ajout modéré).
7. **Matériaux par zone** : volumes 3D peints/placés → `material.zones[]`
   (E0, k, α_th par zone), interpolation par élément ; pas de nouveau gate (les
   adjoints sont déjà par élément).
8. **Portage GPU des adjoints multiphysiques** : v2/v3/axi sont en CPU double
   par choix (correction d'abord) ; le portage est de la perf/échelle, pas une
   capacité nouvelle.
9. Côté contrat, brancher les champs réservés du §4.9 (`penal_continuation`,
   `optimizer`, `output.fields`) quand les branches correspondantes existeront,
   et incrémenter `meta.version` à chaque évolution du schéma (golden tests
   Studio en garde-fou).
