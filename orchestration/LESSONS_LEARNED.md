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

### LL-006 : Emin=1e-9 incompatible avec CG itératif float32 (Phase 2, 2026-06-26)
- **Symptôme** : la boucle TO 3D GPU diverge — compliance oscille follement, valeurs
  négatives, CG plafonne à maxiter sans converger (relres jamais atteinte)
- **Cause** : SIMP `E=Emin+ρ^p(E0−Emin)` avec Emin=1e-9 (valeur Phase 1, solveur
  DIRECT) rend K quasi-singulière dans les zones vides → conditionnement ~1e9 →
  CG Jacobi en float32 incapable de converger
- **Conséquence** : design aberrant, optimisation inutilisable
- **Leçon** : pour un solveur **itératif float32**, borner le contraste de rigidité.
  Emin = 1e-4·E0 (ratio 1e4) converge proprement (compliance 230→18.5, monotone).
  Les solveurs directs tolèrent 1e-9 ; pas les itératifs. (Phase 3 multigrid
  permettra de rebaisser Emin.)
- **Vérification** : compliance monotone décroissante + CG iters << maxiter

### LL-007 : Filtre Helmholtz conservatif → ne pas re-filtrer dans la bissection OC (Phase 2, 2026-06-26)
- **Symptôme** : à 128³, l'OC appelle le filtre ~15× par itération (boucle de
  bissection sur le volume) → coût dominé par un filtre itératif coûteux
- **Cause** : portage naïf de l'OC Phase 1 (filtre direct, re-filtrage gratuit)
- **Conséquence** : itération TO ~30% plus lente que nécessaire à grande échelle
- **Leçon** : le filtre Helmholtz **conserve la moyenne** (sum(ρ̃)=sum(ρ)), donc la
  contrainte de volume se vérifie sur `rho.sum()` directement ; filtrer **une seule
  fois** à la fin de l'OC. Vérifié : volume tenu à 0.3000, temps 73s→51.7s (60×20×20)
- **Vérification** : volume final == cible ; sum(filter(uniforme)) == sum(uniforme)

## Catégorie : Erreurs de Metal/GPU (suite)
*(À enrichir au-delà)*

---

## Catégorie : Erreurs de FEM

### LL-008 : pow(rhoPhys négatif, p non entier) → NaN → boucle OC infinie (Phase 3, 2026-06-27)
- **Symptôme** : la loop multi-grid se fige (100% CPU, aucune progression) pendant
  la continuation, à p≈1.6 ; un run 32³ qui devrait durer 30 s tourne des heures
- **Cause** : le filtre Helmholtz (lisseur PDE) peut produire des `rhoPhys`
  légèrement < 0 (undershoot, surtout à petit rayon). `complianceSensitivity`
  calcule `rhoPhys^(p-1)` → `pow(négatif, 0.6)` = **NaN**. Dans la bissection OC,
  `NaN > target` est toujours faux → `l1` reste à 0, `l2` décroît, et le critère
  `(l2−l1)/(l1+l2)` vaut **1 en permanence → boucle infinie**
- **Pourquoi invisible en Phase 2** : le `main` P2 utilisait p=3 **fixe** (exposant
  entier ; `pow(négatif, 2)` est défini). C'est la **continuation** de Phase 3
  (p non entier) qui révèle le bug
- **Leçon** : (1) clamper `rhoPhys` à [0,1] avant tout `pow` à exposant fractionnaire ;
  (2) **toujours borner** une bissection (cap d'itérations) — un bracket qui ne
  rétrécit pas à cause d'un NaN est une boucle infinie silencieuse
- **Vérification** : la config qui figeait (`mg 32 3 20`) termine en 30 s, C décroît,
  volume tenu

*(À enrichir)*

---

## Catégorie : Erreurs de TO
*(À enrichir)*

## Catégorie : Build / outillage

### LL-010 : Makefile sans dépendances headers → .o stale → crash ABI (Phase 5, 2026-07-01)
- **Symptôme** : après modification d'un header (ex. taille d'une classe/struct
  changée), un `.o` de test non recompilé garde l'ancienne ABI → mismatch →
  SIGTRAP / exit 133 à l'exécution (pas une erreur de compilation)
- **Cause** : les Makefiles du projet ne trackent pas les dépendances headers
  (`$(OBJ)/%.o: $(SRC)/%.cpp` sans `-MMD -MP`), donc `make` ne recompile pas un
  `.o` quand un header inclus change
- **Leçon** : après toute modification de header structurel, **`make clean` puis
  rebuild** (ou ajouter `-MMD -MP` + `-include $(DEPS)` au Makefile). En cas de
  crash inexpliqué à l'exécution après un changement de header, suspecter un `.o`
  stale AVANT de chercher un bug logique
- **Vérification** : `make clean && make test_cpu` reproduit proprement

---

## Catégorie : Erreurs de validation

### LL-009 : Plancher d'arrondi en validation par différences finies (Phase 4, 2026-06-28)
- **Symptôme** : un gradient adjoint CORRECT plafonne à ~1e-5 en erreur relative DF
  sur quelques éléments, alors que l'accord absolu est ~1e-9 partout
- **Cause** : la DF a un plancher d'arrondi `≈ machine_eps · |J| / ε`. Sur les
  entrées à gradient quasi-nul, l'erreur *relative* (= abs/|grad_petit|) explose
  même si l'adjoint est exact. Ce n'est PAS un bug d'adjoint
- **Diagnostic décisif** : faire varier ε. Si l'erreur **décroît** quand ε
  **grandit** → arrondi pur (l'adjoint est bon). Si elle **augmente** → vraie
  erreur de troncature ou adjoint faux. (Sweep observé : ε 1e-6→1.6e-5,
  1e-4→1.6e-7, 1e-3→3e-8 = signature d'arrondi)
- **Leçon** : (1) juger un gate DF d'abord sur l'**accord absolu / nb de chiffres
  significatifs** des éléments bien conditionnés (preuve indépendante du stencil),
  pas seulement sur le max d'erreur relative ; (2) utiliser un stencil central
  d'ordre supérieur (4ᵉ) et un ε adapté ; (3) un adjoint correct concorde à
  7-11 chiffres sur les éléments à gradient non négligeable — c'est ça la preuve
- **Vérification** : sweep en ε + inspection par élément (table adjoint vs DF)

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
