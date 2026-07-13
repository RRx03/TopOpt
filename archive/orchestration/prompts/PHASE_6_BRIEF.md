# PHASE_6_BRIEF.md — Extension (TEMPLATE D'OPTIONS)

> ⚠️ **Ce brief reste un TEMPLATE jusqu'à la clôture de Phase 5.** La direction de
> Phase 6 se décide *après* avoir vu ce que Phase 5 a réellement produit (dette,
> coûts, acquis). Une session dédiée écrira le vrai `PHASE_6_BRIEF.md` selon
> l'option retenue. Ne rien coder ici sans cette décision.

*Détail historique des options : `TopOptP1/TRANSITIONS.md` §Phase 6.*

---

> ✅ **Phase 5 clôturée (2026-07-08)** : cœur scientifique (TO multiphysique
> fluide-structure-thermique par adjoint) validé de bout en bout. Cf.
> `TopOptP5/PHASE_5_REPORT.md`, `handoffs/PHASE_5_TO_6.md`. Recommandation : solder
> les différés « recruteur » (contraintes cooling jacket, vraie tuyère, comparaison
> Borrvall-Petersson) avant de choisir A/B/C.

## OBJECTIF SCIENTIFIQUE (à figer en fin de Phase 5)

Étendre le démonstrateur multiphysique selon **une** des trois directions, en
fonction de l'objectif personnel et de l'état du projet.

## JUSTIFICATION DANS LA ROADMAP

À ce stade, le cœur scientifique (TO adjointe multiphysique) est livré. Phase 6
n'ajoute plus de brique fondamentale obligatoire : elle **valorise** l'acquis vers
un but précis (emploi / thèse / produit). Le choix doit donc être piloté par ce
but, pas par l'envie d'ajouter une feature.

## MÉTHODE DE DÉCISION (session 1 de Phase 6, avant tout code)

1. Lire `TopOptP5/PHASE_5_REPORT.md` : dette restante, coûts, ce qui manque.
2. Lire `orchestration/LESSONS_LEARNED.md` : pièges récurrents influençant le choix.
3. **Poser à l'utilisateur** : horizon de temps restant ? objectif principal
   (recrutement / thèse / entreprise) ? contrainte externe (deadline ISAE, stage) ?
4. **Évaluer d'abord les livrables manquants pour le signal recruteur** (cf.
   `VISION.md` §6 et `TRANSITIONS.md` méta-règles) : README + gallery, doc
   technique, benchmark report, GIF de convergence, validation vs papier publié,
   limitations documentées. **Si > 2 manquent : les produire AVANT toute feature
   Phase 6.** Un projet bien documenté > une feature de plus non validée.
5. Recommander une option motivée, attendre validation, puis écrire le vrai brief.

## LES TROIS OPTIONS

### Option A — Industrialisation *(si objectif : embauche immédiate)*
- Manufacturing constraints : filtre overhang Langelaar (LPBF), taille min feature.
- Robust TO : optimisation sous incertitudes, UQ par polynomial chaos, designs 95e pct.
- Validation expérimentale : impression d'un échantillon optimisé + test (signal fort).
- Documentation utilisateur, exemples industriels packagés.
- Réfs : Langelaar 2016 *Struct. Multidisc. Optim.* 54:603 ;
  Lazarov-Schevenels-Sigmund 2012 *Struct. Multidisc. Optim.* 46:47.

### Option B — Recherche *(si objectif : thèse)*
- AMR vraie (octree adaptatif, p4est ou deal.II).
- Adjoint sur grille adaptative (sujet ouvert).
- Multilevel methods, préconditionnement multigrid couplé.
- Extension Navier-Stokes (RANS) — implique stabilisation SUPG/VMS, turbulence.
- Réfs : Burstedde et al. 2011 (p4est) ; Arndt et al. 2021 (deal.II).

### Option C — Verticalisation *(si objectif : entrepreneuriat)*
- Focus cas d'usage unique : moteur-fusée bipropergol complet.
- Bibliothèque de PhysicsModules couvrant tout le système moteur.
- Pipeline complet : spec → optim → STL → fabrication AM → test.
- Comparaisons head-to-head avec Noyron / nTop sur mêmes specs.

## DRAPEAUX ROUGES POUR PHASE 6

- Coder une feature Phase 6 alors que la **dette Phase 5 n'est pas soldée** → STOP.
- Choisir AMR (B) sans prototype isolé p4est/deal.II préalable → STOP.
- Extension NS/RANS sans stabilisation SUPG/VMS → instabilité garantie → STOP.
- Manufacturing constraints sans cas test d'overhang LPBF connu (Langelaar) → STOP.

## SECTIONS À REMPLIR LORS DE L'INSTANCIATION (option retenue)

À la décision, remplir comme les autres briefs : OBJECTIF SCIENTIFIQUE précis ·
ACQUIS PRÉREQUIS · LIVRABLES ATTENDUS · PSEUDO-CODE · PIÈGES · MÉTHODE DE
VALIDATION · RÉFÉRENCES · DURÉE · DÉCOMPOSITION EN SESSIONS.
