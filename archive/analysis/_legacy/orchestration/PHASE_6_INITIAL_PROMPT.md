# PHASE 6 — Prompt initial : Direction à définir

## Instruction à Claude au démarrage de Phase 6

---

## ÉTAPE 0 — Lire ces fichiers AVANT TOUT (ordre obligatoire)

1. `../orchestration/MASTER_CLAUDE.md` — règles communes, format de réponse
2. `../orchestration/LESSONS_LEARNED.md` — erreurs accumulées à éviter
3. `../TopOptP1/TRANSITIONS.md` — section "Phase 6 et au-delà"
4. `../TopOptP5/CLAUDE.md` — état du code en sortie de Phase 5
5. `../TopOptP5/PHASE_5_REPORT.md` — rapport Phase 5, acquis validés
6. Ce fichier — options et méthode de décision

---

## Contexte

Phase 5 a produit un solveur TO multiphysique fluide-structure-thermique complet.
Phase 6 est le point de divergence : le choix dépend de l'état du projet à ce
moment (avancement, délai ISAE-SUPAERO, objectifs recrutement/recherche).

**Cette session commence par une décision explicite, pas par du code.**

---

## Objectif Phase 6 — À décider en début de session

Trois directions possibles, documentées dans `TopOptP1/TRANSITIONS.md` :

### Option A — Industrialisation
*Recommandée si objectif : recrutement ingénierie / stage R&D*
- Manufacturing constraints : filtre Langelaar (overhang LPBF)
- Robust TO : optimisation sous incertitudes (matériaux, charges)
- Validation expérimentale : si échantillon imprimable disponible
- Documentation utilisateur complète, gallery de designs

### Option B — Recherche
*Recommandée si objectif : thèse / publication*
- AMR (octree adaptatif, p4est ou deal.II)
- Adjoint sur grille adaptative (sujet ouvert)
- Extension Navier-Stokes (RANS) avec stabilisation
- Couplage avec différentiable physics (JAX-FEM)

### Option C — Verticalisation propulsion
*Recommandée si objectif : entrepreneuriat / démo industrielle*
- Focus : moteur-fusée bipropergol complet
- Pipeline spec → optim → STL → fabrication AM → test
- Comparaison quantitative à Noyron / nTop sur mêmes specs
- Bibliothèque de PhysicsModules réutilisables

---

## Méthode de décision (session 1, avant tout code)

1. **Lire `TRANSITIONS.md` section Phase 6** : trois options détaillées
2. **Lire `PHASE_5_REPORT.md`** : dette technique restante, ce qui manque
3. **Lire `LESSONS_LEARNED.md`** : pièges récurrents influençant le choix
4. **Poser ces questions à l'utilisateur avant de proposer quoi que ce soit** :
   - Quel est ton horizon de temps sur ce projet (combien de semaines restantes) ?
   - Quel est ton objectif principal (recrutement, thèse, démo perso) ?
   - Y a-t-il une contrainte externe (deadline ISAE, stage, présentation) ?
5. **Proposer une recommandation motivée** (Option A/B/C ou hybride)
6. **Attendre validation explicite avant de coder**

---

## Points à évaluer avant de décider

### Dette technique en entrée de Phase 6

À lire dans `PHASE_5_REPORT.md` :
- [ ] Mesh independence démontrée en Phase 3 tient-elle avec la physique Phase 5 ?
- [ ] Adjoint triple-couplé Phase 5 : matching DF à quelle tolérance ?
- [ ] Temps par itération TO sur cooling jacket (128³) : acceptable ?
- [ ] Tests CI : coverage actuel, tests cassés acceptés ?
- [ ] Warnings de compilation restants ?

### Livrables Phase 5 qui manquent pour le recruteur

D'après `TRANSITIONS.md` méta-règles :
- [ ] README avec gallery d'images (avant/après, comparaisons résolution)
- [ ] Documentation technique par phase
- [ ] Benchmark report : temps par cas, scaling vs résolution
- [ ] Vidéo/GIF de convergence
- [ ] Validation numérique contre papier publié (Borrvall-Petersson)
- [ ] Limitations honnêtement documentées

**Si plus de 2 items cochés manquent : proposer de les produire AVANT de coder
Phase 6.** Un projet bien documenté vaut mieux qu'un projet avec une feature
de plus non validée.

---

## Drapeaux rouges pour Phase 6

- [ ] Coder une feature Phase 6 alors que la dette Phase 5 n'est pas soldée →
      STOP, rembourser la dette d'abord
- [ ] Choisir AMR (Option B) sans avoir lu d'abord l'architecture p4est ou
      deal.II → STOP, démarrer par un prototype isolé
- [ ] Extension NS/RANS sans stabilisation SUPG/VMS →
      STOP, Stokes → NS sans stabilisation = instabilité garantie
- [ ] Manufacturing constraints codées sans cas test d'overhang LPBF connu →
      STOP, valider d'abord avec Langelaar 2016 cas test

---

## Références pour la décision Phase 6

### Option A
- Lazarov-Schevenels-Sigmund 2012, *Struct. Multidisc. Optim.* 46:47 (robust TO)
- Langelaar 2016, *Struct. Multidisc. Optim.* 54:603 (AM overhang filter)

### Option B
- Burstedde et al. 2011 — p4est (AMR parallèle)
- Arndt et al. 2021 — deal.II (library FEM adaptative)
- Cockburn-Karniadakis-Shu 2000 — discontinuous Galerkin (si extension NS)

### Option C
- Yates 1995 — thermo-structural analysis de tuyères (référence propulsion)
- Leap71 (public demos) — comparaison qualitative target
