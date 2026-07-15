# Cahier des charges — Modeleur web de contraintes 3D (TopOpt Studio)

*Draft v0.2 — à itérer. L'outil web est un **producteur/consommateur du contrat
`.topopt.json`** (cf. `INPUT_LANGUAGE.md`) : il n'embarque aucune physique.*

**Priorité immédiate (validée)** : une interface donnant assez de contrôle pour
**utiliser l'algorithme et vérifier ses résultats** (MVP M1–M3). La vision long
terme (§7) guide l'architecture mais ne conditionne pas le MVP.

---

## 1. Vision

Une application web (statique, zéro installation) où l'utilisateur :

1. **modélise** un problème de TO dans une scène 3D — domaine, résolution,
   conditions aux limites par clic sur les faces, matériau, objectif, contraintes ;
2. **exporte** le `.topopt.json` (ou le rejoue : import round-trip) ;
3. **exécute** le solveur en local (`topopt_run problem.topopt.json`) ;
4. **visualise** les résultats (`.vti`, `.stl`) dans le viewer intégré.

```
┌─────────────── TopOpt Studio (navigateur) ───────────────┐
│  Éditeur 3D (three.js)      Viewer résultats (vtk.js)    │
│  scène + picking faces  →   .vti (density, vM, T, u)     │
│  panneaux BC/matériau       coupes, iso-surfaces, cmaps  │
└───────────┬───────────────────────────▲──────────────────┘
            │ .topopt.json (export)     │ .vti/.stl (import)
            ▼                           │
        topopt_run (CLI, local) ────────┘
```

**Positionnement** : ParaView reste l'outil d'analyse avancée ; le viewer intégré
vise la boucle courte auteur→résultat et la démo (lien partageable).

## 2. Périmètre

### MVP (M1–M3)
- Domaine **box 3D** : grid (nelx,nely,nelz), size_mm, aperçu du maillage.
- **Sélection** de faces (x±, y±, z±), arêtes (intersection de 2 faces), coins ;
  surbrillance au survol, code couleur par type de BC.
- **BCs élastiques** : appuis (`fixed` par composante ou `all`), charges (`loads`
  force totale + direction), glyphes 3D (flèches, encastrements).
- **Formulaires** matériau / filtre / optimize (objectif, contraintes avec types
  autorisés selon la physique, optimiseur, max_iter) — défauts = ceux de
  `ProblemSpec` (source de vérité).
- **Validation en continu** : physique choisie ⇒ types de contraintes/objectifs
  admissibles (matrice de compatibilité §4) ; erreurs bloquantes à l'export.
- **Export/Import** `.topopt.json` round-trip sans perte.
- **Viewer vtk.js** : chargement du `.vti` produit, iso-surface à seuil réglable,
  coupes orthogonales, colormaps par champ, chargement `.stl`.

### V2
- Physiques v2/v3 dans l'UI : BCs thermiques (Dirichlet T, sources Q par face ou
  région `axis:lo:hi`), flow (no-slip, slip, datum pression, drive), multi-contraintes.
- Mode **axisymétrique** : édition du profil r_in(z) (r_throat, K, wall) avec
  aperçu 2D (r,z) + révolution 3D live.
- Presets : les 6 exemples du repo chargeables en un clic (galerie).

### V3 / différé
- Wrapper local optionnel (petit serveur HTTP autour de `topopt_run`) pour un
  bouton « Run » sans quitter le navigateur ; suivi de convergence (parse stdout).
- Régions de design/non-design peintes ; multi-matériaux ; import CAD. **Non-buts MVP.**

## 3. Choix techniques

| Sujet | Choix | Justification |
|---|---|---|
| Rendu scène | **three.js** | picking faces trivial (raycaster), écosystème, léger |
| Viewer résultats | **vtk.js** | lit nativement `.vti` (VTK XML ImageData), iso/coupes fournis |
| Langage | **TypeScript** | le schéma ProblemSpec typé = le contrat compilé |
| Build | **Vite** | app statique, dev server instantané, déploiement = fichiers |
| UI panels | à trancher : vanilla + lil-gui (léger) **ou** React (si l'UI grossit) |
| État | un seul objet `ProblemSpec` TS, single source of truth, undo/redo par snapshots |
| Backend | **aucun** en MVP (app 100 % statique, hébergeable GitHub Pages) |

## 4. Le contrat (exigences dures)

- Le type TS `ProblemSpec` **reflète exactement** `src/io/ProblemSpec.hpp`
  (mêmes champs, mêmes défauts). Tout écart = bug. Un test de golden files
  compare l'export UI aux 6 exemples du repo (sémantiquement identiques).
- Matrice de compatibilité (à générer depuis la doc, maintenue à la main en MVP) :

| physics | objectifs | contraintes admissibles |
|---|---|---|
| elastic (3d) | compliance, mass | volume, vonmises |
| thermal+elastic | mass | volume, vonmises |
| fluid+thermal+elastic | compliance | volume, tmax, dissipation, vonmises (combinables) |
| elastic (axi, nozzle) | mass | vonmises (`max_rel`) |

- Conventions affichées à l'écran (pièges connus du projet) : γ=1 fluide en v3
  vs ρ=1 matière ailleurs ; l'axe r=0 hors domaine en axi ; force = totale
  distribuée sur la sélection.

## 5. Jalons & critères d'acceptation

| Jalon | Livrable | Critère d'acceptation |
|---|---|---|
| **M1 — Auteur box** | éditeur 3D + BCs élastiques + export | reconstruire `mbb3d.topopt.json` **entièrement à la souris** ; le solveur donne C=18.5216 identique |
| **M2 — Round-trip & validation** | import + matrice de compat + presets | importer les 6 exemples, les rééditer, les re-exporter sans perte |
| **M3 — Viewer** | vtk.js intégré | charger `cooling_jacket_full.vti`, iso-surface density=0.5, coupe + colormap T |
| **M4 — Physiques v2/v3** | BCs thermiques/flow + multi-contraintes | reconstruire `cooling_jacket_full.topopt.json` à la souris, 4 contraintes |
| **M5 — Axi** | éditeur de profil + révolution | reconstruire `nozzle_profiled.topopt.json`, PNG/STL identiques |

Chaque jalon = démo enregistrable. M1–M3 ≈ MVP présentable.

## 6. Risques

- **Dérive du contrat** : le schéma évolue côté C++ sans l'UI → golden tests M2
  en CI + version de schéma dans `meta.version`.
- **Volumétrie .vti** : grilles fines (128³ ≈ 8 Mo/champ) — vtk.js tient, mais
  prévoir le chargement champ par champ.
- **Picking ambigu** (faces internes, arêtes) : limiter le MVP aux 6 faces + arêtes
  franches, comme les sélecteurs actuels de `BCResolver`.
- **Sur-ingénierie UI** : pas de framework tant que lil-gui suffit (décision §3 à
  confirmer au premier prototype).

## 7. Vision long terme — interfaces couplées (conception par flux)

*Le cas d'usage cible (exemple fondateur) : concevoir une tuyère **entre deux
disques d'interface**. Le disque amont porte le flux mesuré en sortie de chambre
de combustion (profils T(r), u(r) connus) ; le disque aval impose le flux voulu
(vitesse axiale cible, profil plat, composante radiale nulle, température max…).
L'algorithme optimise la matière entre les deux — y compris la **distance H**
entre les disques, variable dans un domaine borné. Puis : passe de robustesse
(renforcer les zones sensibles au prix de masse), matériaux par zone 3D, export
et test.*

Traduction en concepts d'outil, avec l'état réel du solveur (honnêteté oblige) :

| Concept | Objet UI | Côté solveur | Statut |
|---|---|---|---|
| **Disque d'interface** : BC riche portée par une surface (profils T(r), u(r), direction) | objet 3D positionnable portant des champs paramétrés (uniforme, parabolique, tabulé/CSV) | Dirichlet **inhomogène** par profil sur une face/région | ❌ nouveaux : le solveur actuel condense en Dirichlet homogène (T=0, u=0) + drive volumique ; extension du contrat (`bc.flow[].profile`, `bc.thermal[].profile`) + adaptation des adjoints (les gates existants restent valides, le RHS change) |
| **Contrainte de sortie de flux** (vitesse cible, radiale nulle, profil plat) | panneau « objectif de flux » sur le disque aval | nouvelles fonctionnelles J (écart au profil cible sur une section) + leurs adjoints | ❌ **nouveaux gates DF** (machinerie cascade réutilisable, semis différent — même schéma que le gate vm-triple) |
| **Géométrie variable (H ∈ [H_min, H_max])** | poignée/slider sur l'objet, domaine borné | variable de **forme** couplée à la TO : gradient par adjoint de forme ou boucle externe (DF sur H, peu coûteux pour 1-3 paramètres) | ❌ nouveau ; la boucle externe DF est le chemin pragmatique |
| **Robustesse** (masse ↑ pour renforcer les zones sensibles) | slider post-résultat « robustesse » | re-run avec σ_lim abaissé (`max_rel` ↓) ou érosion/dilatation (projection Heaviside décalée η±Δ — approche robuste classique) | 🟡 assemblage : `max_rel` existe ; l'érosion/dilatation est un ajout modéré |
| **Matériaux par zone** | volumes 3D peints/placés (boîtes, cylindres) | champ matériau par élément (E0, k, α_th par zone) | 🟡 extension du contrat (`material.zones[]`) + interpolation par zone ; pas de nouveau gate (les adjoints sont déjà par élément) |
| **Boucle résultat → test** | viewer + export STL + rapport | déjà couvert (VTI/STL/marching cubes) | ✅ |

Principe directeur : **chaque concept entre dans le contrat `.topopt.json` d'abord**
(schéma versionné), l'UI et le solveur suivent. Tout nouveau gradient passe un
gate DF avant usage (règle inchangée). L'exemple fondateur (tuyère entre deux
disques) devient le démonstrateur de la V3+.

## 8. Arborescence prévue

```
web/
├── index.html
├── src/
│   ├── spec/        # types ProblemSpec + défauts + validation + compat
│   ├── editor/      # scène three.js, picking, glyphes BC
│   ├── panels/      # formulaires (matériau, optimize, output)
│   ├── viewer/      # vtk.js (.vti/.stl)
│   └── presets/     # les 6 exemples embarqués
├── tests/           # golden round-trip vs ../examples/*.topopt.json
└── vite.config.ts
```
