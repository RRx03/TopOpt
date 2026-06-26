# LESSONS_LEARNED.md — Erreurs et pièges accumulés

Document vivant. **Mis à jour à la fin de chaque session de travail.**
**À CONSULTER en début de session avant toute action.**

Format d'entrée :
```
### LL-XXX : Titre (Phase N)
- Symptôme :
- Cause :
- Conséquence :
- Leçon :
- Vérification :
```

---

## Catégorie : Erreurs de spécification du prompt

### LL-001 : Confusion MBB vs Cantilever (Phase 1)
- **Symptôme** : prompt initial Phase 1 décrivait "MBB beam" avec BCs cantilever
- **Cause** : confusion sémantique dans la spec utilisateur
- **Conséquence** : Claude Code a détecté la contradiction, mais aurait pu
  coder le mauvais cas
- **Leçon** : préciser EXPLICITEMENT les conditions limites et la géométrie
  dans chaque prompt initial, ne JAMAIS s'appuyer sur le seul nom du cas test
- **Vérification** : avant de coder un cas canonique, lire la spec des BCs
  et confirmer avec l'utilisateur si ambigu

### LL-002 : Références de fichiers de handoff inexistantes (Phase 2, 2026-06-15)
- **Symptôme** : un prompt référence un document (`PHASE_1_TO_PHASE_2_HANDOFF.md`,
  puis `PHASE_2_TO_PHASE_3_HANDOFF.md`) qui n'existe pas sur le disque
- **Cause** : nom de fichier supposé/annoncé mais jamais créé
- **Conséquence** : risque de blocage ou de partir sur de mauvaises hypothèses
- **Leçon** : vérifier l'existence des fichiers de référence AVANT de s'appuyer
  dessus ; si absent, identifier le vrai doc (ici `TopOptP1/TRANSITIONS.md`) et
  le signaler à l'utilisateur, ne rien inventer
- **Vérification** : `ls` des fichiers de référence en début de session

### LL-003 : Dérive documentation ↔ code (Phase 1/2, 2026-06-26)
- **Symptôme** : les docs annonçaient des choix non implémentés (SimplicialLLT vs
  LDLT réel, ρ_min=1e-3 vs Emin=1e-9, continuation de p, ~15 modules séparés vs
  fusionnés, 3 fichiers de test vs 1) — cf. divergences D1–D9 de
  `analysis/CODE_ANALYSIS.md`
- **Cause** : documents rédigés comme intention initiale, jamais réalignés sur le
  code après implémentation
- **Conséquence** : un nouveau prompt s'appuyant sur les docs aurait codé/validé
  sur des hypothèses fausses
- **Leçon** : **le code fait foi**. Toute convention écrite doit refléter
  l'implémentation. Réaligner la doc après chaque phase, pas l'inverse
- **Vérification** : en début de phase, comparer les conventions du MASTER_CLAUDE
  au code réel (grep/lecture ciblée) ; signaler tout écart

### LL-004 : Compliance non invariante au maillage (Phase 1, 2026-06-15)
*(Proposée dans `TopOptP1/PHASE_1_REPORT.md` §8, intégrée ici — renumérotée
LL-004 car LL-002/003 étaient déjà pris.)*
- **Symptôme** : valeur de compliance de référence d'un papier appliquée à une
  autre résolution (compliance MBB ≈200 attendue à 200×100, obtenu 84)
- **Cause** : domaine défini en unités-élément → grandit avec le maillage ;
  c ∼ L³/H³, non invariante
- **Conséquence** : faux "échec" de validation (ou faux succès)
- **Leçon** : une compliance de référence n'est valable QU'À la résolution et la
  géométrie du papier. Reproduire le cas canonique sur SA grille avant de comparer
- **Vérification** : à 60×20 (grille d'Andreassen) → 229.9, cohérent

### LL-005 : Build clang — deps tierces en -isystem et collisions de noms (Phase 1, 2026-06-15)
*(Proposée dans `TopOptP1/PHASE_1_REPORT.md` §8, intégrée ici, renumérotée LL-005.)*
- **Symptôme** : warnings Eigen sous `-Wpedantic` ; collision binaire `build/topopt`
  vs dossier d'objets `build/topopt/`
- **Cause** : `-I` au lieu de `-isystem` ; binaire et dossier d'objets homonymes
- **Conséquence** : "0 warning" impossible ; erreur de link EISDIR
- **Leçon** : deps tierces en `-isystem` ; objets sous `build/obj/`, binaires ailleurs
- **Vérification** : `make clean && make 2>&1 | grep -c warning` == 0

---

## Catégorie : Erreurs de Metal/GPU
*(À enrichir en Phase 2 et au-delà)*

---

## Catégorie : Erreurs de FEM
*(À enrichir)*

---

## Catégorie : Erreurs de TO
*(À enrichir)*

---

## Catégorie : Erreurs de validation
*(À enrichir)*

---

## Catégorie : Pièges connus de la littérature

### LL-LIT-001 : Stress singularity en stress-constrained TO (Phase 4)
- **Description** : quand ρ → 0, σ_vM/ρ → ∞ artificiellement
- **Référence** : Duysinx-Bendsøe 1998
- **Solution** : ε-relaxation ou qp-approach (Bruggi 2008)
- **À implémenter** : dès la première version de Phase 4

### LL-LIT-002 : Inf-sup condition Stokes (Phase 5)
- **Description** : choix d'éléments fluide essentiel, sinon oscillations
  de pression et design absurde
- **Solution** : Taylor-Hood (P2-P1) ou Q1-Q1 stabilisé PSPG

### LL-LIT-003 : Added-mass instability FSI (Phase 5+)
- **Description** : couplage partitionné FSI inconditionnellement instable
  pour rapports de densité fluide/structure proches de 1
- **Référence** : Causin-Gerbeau-Nobile 2005
- **Solution** : Aitken adaptative ou IQN-ILS quasi-Newton
- **Pertinent** : uniquement si on étend au couplage transitoire

### LL-LIT-004 : Brinkman penalization mal calibrée (Phase 5)
- **Description** : α_max trop faible = fuite fluide dans solide ; trop grand
  = conditionnement K explose
- **Sweet spot** : α_max entre 1e3 et 1e5 selon viscosité
- **Mitigation** : continuation progressive de α_max

### LL-LIT-005 : Checkerboarding sans filtre (Phase 1+)
- **Description** : design en damiers ρ=0/ρ=1 alterné si filtre absent ou
  trop petit
- **Solution** : Helmholtz filter avec r ≥ 2 cellules minimum
- **Référence** : Sigmund 1997, Bourdin 2001

### LL-LIT-006 : Mesh dependence sans filtre physique (Phase 3+)
- **Description** : design final dépend de la résolution de grille si le
  rayon r est exprimé en cellules (pas en unités physiques)
- **Solution** : filtre Helmholtz avec rayon r en unités PHYSIQUES (mm),
  pas en cellules
- **Référence** : Lazarov-Sigmund 2011

### LL-LIT-007 : Validation gradient adjoint (Phase 4+)
- **Description** : un adjoint multi-bloc faux peut produire des designs qui
  "semblent" converger mais sont en réalité aberrants
- **Solution OBLIGATOIRE** : validation par différences finies sur sub-cas
  10×10 avant tout run en grande taille
- **Tolérance** : matching adjoint vs DF à 1e-5 près minimum

### LL-LIT-008 : Race conditions assembly GPU (Phase 2+)
- **Description** : nœuds partagés entre éléments adjacents → deux threads
  écrivent simultanément sur le même DOF dans K
- **Solution** : `atomicAdd` obligatoire sur les entrées K partagées,
  OU stratégie de graph-coloring des éléments
- **Conséquence si oublié** : K mal assemblée silencieusement, solveur CG
  diverge ou converge vers mauvaise solution

### LL-LIT-009 : Précision float32 CG sur GPU (Phase 2+)
- **Description** : résidu CG peut stagner > 1e-5 sur bruit numérique float32
  sans avoir vraiment convergé
- **Vérification** : comparer résidu float32 vs double sur petit cas ; si stagnation
  avant 1e-6, bug de préconditionnement ou précision insuffisante
- **Solution** : Jacobi OK pour Phase 2, multigrid Phase 3 améliore

### LL-LIT-010 : Conservation du volume entre niveaux multigrid (Phase 3)
- **Description** : interpolation naïve ρ_fin(x) = ρ_grossier(x) ne conserve
  pas le volume, dérive silencieuse de la contrainte volumique
- **Solution** : interpolation conservative (moyenne pondérée par volume)
- **Vérification** : tester avant/après chaque transition de niveau, tolérance 0.01%

### LL-LIT-011 : Asymétrie K_couplée thermo-élastique (Phase 4)
- **Description** : le couplage one-way T → σ rend la matrice globale
  non-symétrique ; CG ne marche plus
- **Solution** : passer à BiCGStab ou GMRES pour le solveur Phase 4
- **Référence** : Pedersen-Pedersen 2010

### LL-LIT-012 : Non-dimensionnalisation Stokes CHT (Phase 5)
- **Description** : mélange Pa, m/s, K, MPa → matrices mal conditionnées
  (cond > 1e15) → solveurs qui divergent silencieusement
- **Solution** : non-dimensionnaliser AVANT assemblage, documenter les
  échelles de référence dans le CLAUDE.md Phase 5
