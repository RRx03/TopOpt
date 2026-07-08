# TopOpt — Langage d'entrée (ProblemSpec)

*Le format de définition d'un problème de topology optimization, découplé du code.
C'est le **contrat** entre le modélisateur 3D (qui le produit), le solveur (qui le
consomme) et le moteur de rendu (qui affiche les champs de sortie qu'il référence).*

Format : **JSON** (dépendance `nlohmann/json` déjà vendorisée). Extension : `.topopt.json`.

---

## 1. Principe

Un `ProblemSpec` décrit **tout** ce dont le solveur a besoin, sans une ligne de C++ :
domaine/maillage, matériau(x), physiques actives, conditions aux limites, objectif,
contraintes, paramètres d'optimisation, et sorties. Le solveur lit ce fichier,
construit le problème, optimise, et écrit les résultats aux formats demandés.

Conception **incrémentale et rétro-compatible** : un champ absent prend un défaut ;
on commence par le structurel (v1), puis on étend au thermo-élastique (v2) et au
fluide-thermique (v3) sans casser les fichiers existants.

---

## 2. Schéma (vue d'ensemble)

```jsonc
{
  "meta":   { "name": "mbb3d", "version": 1, "dim": "3d" },   // dim: "3d" | "axi"

  "domain": {
    "grid":    [60, 20, 20],        // nelx, nely, nelz (nelz=1 pour axi/2D)
    "size_mm": [60.0, 20.0, 20.0],  // taille physique (pour le filtre en mm)
    "geometry": "box"               // "box" | "nozzle" (+ params ci-dessous)
    // "nozzle": { "r_throat": 0.6, "r_out": 1.6, "height": 4.0, "K": 0.35 }
  },

  "material": {
    "E0": 1.0, "Emin": 1e-4, "nu": 0.3, "penal": 3.0,   // élasticité + SIMP
    "k_solid": 1.0, "k_fluid": 0.3,                       // conduction (v2+)
    "alpha_th": 1e-3, "Tref": 0.0,                        // dilatation (v2+)
    "mu": 1.0, "brinkman_max": 1e2, "brinkman_q": 0.1     // fluide (v3+)
  },

  "physics": ["elastic"],   // sous-ensemble de ["elastic","thermal","fluid"]
                            // le couplage est déduit (fluid→thermal→elastic)

  "bc": {
    // Conditions par FACE ou par région. dof: "x"|"y"|"z"|"all"|"T"|"ur"|"uz"
    "fixed":   [ { "face": "x-", "dof": "x" },
                 { "node": "corner:x+,y-,z-", "dof": "y" } ],
    "loads":   [ { "edge": "x-,y+", "dof": "y", "value": -1.0 } ],   // force
    "pressure":[ { "face": "inner", "value": 80.0 } ],               // BC Neumann
    "thermal": [ { "face": "z-", "T": 0.0 }, { "region": "throat", "Q": 10.0 } ],
    "flow":    [ { "face": "z-", "inlet_velocity": [0,0,1] },
                 { "face": "z+", "outlet": true } ]
  },

  "filter":  { "radius_mm": 1.5, "heaviside": { "beta": [1,2,4,8], "eta": 0.5 } },

  "optimize": {
    "objective":  "mass",     // "compliance" | "mass" | "dissipation" | "tmax"
    "constraints": [
      { "type": "volume",   "max": 0.5 },
      { "type": "vonmises", "max": 1.0 },     // p-norm, relaxée
      { "type": "tmax",     "max": 5.0 }
    ],
    "optimizer": "mma",       // "mma" | "oc"
    "max_iter": 80,
    "penal_continuation": true
  },

  "output": {
    "dir": "output/mbb3d",
    "formats": ["vti", "stl"],           // ParaView + géométrie
    "fields":  ["density","vonmises","temperature","velocity","displacement"],
    "stl_iso": 0.5, "stl_method": "marching_cubes"   // "voxel" | "marching_cubes"
  }
}
```

---

## 3. Sémantique des champs

### 3.1 `meta`
- `dim` : `"3d"` (hexaèdre H8) ou `"axi"` (Q4 axisymétrique r,z ; `grid[2]` ignoré).
- `version` : version du schéma pour la rétro-compatibilité.

### 3.2 `domain`
- `grid` : résolution en éléments. `size_mm` : dimensions physiques (le filtre et les
  contraintes en mm en dépendent). `geometry` : `"box"` (rectangulaire) ou `"nozzle"`
  (alésage profilé convergent-divergent, cf. `nozzle_profiled`).

### 3.3 `material`
Regroupe tous les paramètres physiques. Les blocs non actifs (`physics`) ignorent
leurs paramètres. **Convention densité** : `ρ=1`=matière/fluide, `ρ=0`=vide/solide
(selon le contexte physique ; documenté par objectif).

### 3.4 `physics`
Liste des blocs actifs. Le couplage est **déduit** de la cascade one-way validée :
`fluid → thermal → elastic`. Ex. `["fluid","thermal","elastic"]` active la cascade
triple ; `["elastic"]` = structurel pur.

### 3.5 `bc` — conditions aux limites
Référencées par **face** (`x-`,`x+`,`y-`,`y+`,`z-`,`z+`), **arête**, **nœud** ou
**région** nommée (`inner`, `throat`, …). Le loader traduit ces sélecteurs en DOF
fixés / vecteurs de charge concrets — c'est **exactement** ce que le modélisateur 3D
générera (l'utilisateur clique une face, choisit un type de contrainte).
- `fixed` : Dirichlet homogène (appui). `loads` : forces. `pressure` : Neumann.
- `thermal` : Dirichlet T ou source Q. `flow` : entrée/sortie fluide.

### 3.6 `filter` / `optimize` / `output`
Filtre (rayon mm + continuation Heaviside), objectif+contraintes (les 6 gradients
validés), MMA, et sorties (VTK pour ParaView, STL marching-cubes pour la géométrie —
les formats que le moteur de rendu lira).

---

## 4. Rôle dans la vision d'outil

```
   Modélisateur 3D ──(écrit)──► ProblemSpec (.topopt.json) ──(lu par)──► Solveur
        ▲ l'utilisateur définit        LE CONTRAT                    (optimise)
        │ géométrie/BCs/contraintes                                       │
        │ dans une scène 3D                                               ▼
        └───────────────── Moteur de rendu 3D ◄──(champs vti/stl)── résultats
```

Le `ProblemSpec` est le **point de découplage** : le solveur ne connaît que ce
format ; le modélisateur et le rendu sont des outils indépendants autour de lui.

---

## 5. Feuille de route d'implémentation
- **v1 (structurel)** : `meta/domain/material/bc(fixed,loads)/filter/optimize
  (compliance|mass + volume)/output` → reproduit MBB depuis JSON.
- **v2 (thermo-élastique)** : `physics:["thermal","elastic"]`, `bc.thermal`,
  contraintes `vonmises`/`tmax`.
- **v3 (fluide-thermique)** : `physics:["fluid","thermal","elastic"]`, `bc.flow`,
  objectif `dissipation`, contrainte `dP` → cooling jacket depuis JSON.
