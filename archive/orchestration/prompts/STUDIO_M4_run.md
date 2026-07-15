# STUDIO M4 — run local/distant depuis l'UI + setup.sh + hygiène repo

## Objectif
Fermer la boucle SANS quitter le navigateur : lancer `topopt_run` depuis le
Studio (sur la machine locale OU sur une machine distante du même repo, ex.
Mac Studio via Tailscale), suivre la convergence en direct, charger le
résultat dans le viewer en un clic. Plus : `setup.sh` de bootstrap d'un clone
frais, et garantie qu'aucun run ne salit jamais le git. Lis d'abord
docs/WEB_MODELER_SPEC.md, web/src/ (state, panels, viewer), et la sortie
console de `./build/topopt_run examples/mbb3d.topopt.json` (format des lignes
d'itération à parser).

## 1. Serveur de run — `server/run-server.mjs`
Node pur (AUCUNE dépendance npm, modules natifs http/fs/child_process
uniquement), un seul fichier + éventuel module utilitaire.
- `node server/run-server.mjs [--host 0.0.0.0] [--port 8787]` ; défauts :
  127.0.0.1:8787 (le mode distant = même serveur lancé avec --host 0.0.0.0
  sur la machine distante).
- API (CORS ouvert — nécessaire pour vite:5173 et l'accès distant) :
  - `GET /api/health` → { ok, version (git describe/hash court), solver:
    existence de build/topopt_run }.
  - `POST /api/run` body = le .topopt.json (+ champ optionnel `outputDir`) →
    crée un job : réécrit `output.dir` du spec vers
    `<outputDir | output>/<name>-<horodatage>/`, écrit le spec dans ce dossier
    (traçabilité : le spec exact du run est archivé avec ses résultats),
    spawn `build/topopt_run`, répond { jobId }.
  - `GET /api/jobs/:id/log` → **SSE** : lignes stdout/stderr en direct + événement
    final { exitCode, artifacts: [fichiers produits .vti/.stl/.png…] }.
  - `GET /api/jobs/:id` → état (running/done/failed, exitCode, artifacts).
  - `GET /api/jobs/:id/artifacts/<fichier>` → sert le fichier (Content-Type
    correct pour .vti/.stl). AUCUN accès hors du dossier du job (protection
    path traversal — normalise et vérifie le préfixe).
- Un seul run à la fois (le solveur sature le CPU) : 409 si occupé + info du
  job en cours. Jobs en mémoire (pas de persistance, ce n'est pas un service).
- Si build/topopt_run absent → 503 avec message « lancer ./setup.sh ».

## 2. UI — panneau « Run » dans le Studio
- Section Run (onglet éditeur) : cible **Local** (localhost:8787) et, SI
  configurée, **Distant** ; champ « dossier de sortie » (défaut : `output`,
  relatif au repo de la machine qui exécute) ; bouton Run (désactivé si
  export bloqué par la validation M2).
- **Cible distante** : visible UNIQUEMENT si `VITE_REMOTE_HOST` est défini
  (fichier `web/.env.local`, gitignoré — config perso, invisible pour les
  autres utilisateurs du repo). Champ hôte pré-rempli avec la valeur de
  VITE_REMOTE_HOST, éditable ; port idem (VITE_REMOTE_PORT, défaut 8787).
  Fournis `web/.env.example` committable documentant ces variables
  (exemple : VITE_REMOTE_HOST=100.82.100.44).
- Pendant le run : log de convergence en direct (SSE), les lignes d'itération
  parsées en compteur it/max_iter + dernière valeur objectif ; bouton annuler
  (DELETE /api/jobs/:id → kill).
- À la fin : liste des artefacts avec taille ; clic sur un .vti/.stl → fetch
  depuis le serveur → **chargé directement dans l'onglet Résultats** (réutilise
  les parseurs M3 — c'est le chemin magique local ET distant : pas de dossier
  à fouiller). Les fichiers restent aussi dans le dossier de sortie de la
  machine exécutante.
- Health-check au chargement du panneau : pastille verte/rouge par cible.

## 3. `setup.sh` (racine du repo)
Bootstrap d'un clone frais, idempotent, `set -euo pipefail`, messages clairs :
1. Vérifie macOS + Apple Silicon (warning sinon), clang/make (sinon :
   `xcode-select --install`), node ≥ 20 + npm (sinon : message brew/nodejs.org).
2. `make -j` (solveur + shaders). Option `--test` : enchaîne `make test_cpu`.
3. `cd web && npm install`.
4. Récapitulatif : commandes pour lancer l'UI (`cd web && npm run dev`), le
   serveur de run (`node server/run-server.mjs`), le mode distant
   (`--host 0.0.0.0` + .env.local côté client), et un exemple CLI direct.
Ajoute aussi des scripts npm dans web/package.json : `"server"` (lance le
serveur local) et documente dans le README (§ TopOpt Studio) : setup.sh,
run depuis l'UI, mode distant Tailscale (générique : « une machine distante
ayant cloné le repo », l'IP n'est qu'un exemple dans .env.example).

## 4. Hygiène git (vérification demandée)
- Rien de généré ne doit JAMAIS apparaître dans `git status` : vérifie que
  `output/`, `build/`, `web/dist/`, `web/node_modules/`, `*.env.local` sont
  ignorés (racine et/ou web/.gitignore) ; ajoute `.env*.local` à
  web/.gitignore.
- `output/.gitkeep` tracké (avec négation `!output/.gitkeep` dans le
  .gitignore racine) pour que le dossier vide existe dans un clone frais.
- Test final : run complet via le serveur → `git status --short` DOIT être
  vide (hors tes propres fichiers de chantier non commités).

## Vérifications de fin (toutes obligatoires)
- `npx tsc --noEmit` 0 erreur ; `npm run build` 0 erreur ; `npm run test`
  tous verts (M1-M3 inclus).
- Test d'intégration réel : serveur lancé, POST du spec mbb3d (petit, ~1 min),
  SSE reçu, artefacts listés, GET du .vti OK, git status vide. Automatise-le
  en script node (server/tests ou web/tests, à ta main) qui skippe proprement
  si build/topopt_run absent.
- `bash -n setup.sh` + exécution réelle de setup.sh (idempotent sur repo déjà
  construit — il doit passer vite et sans erreur).
- AUCUN commit. Rien modifié hors : server/, web/, setup.sh, .gitignore,
  output/.gitkeep, README.md (§ Studio).

## Rapport final
Fichiers, API du serveur, sorties des vérifications (tests, intégration,
setup.sh), captures du flux UI (description), écarts.
