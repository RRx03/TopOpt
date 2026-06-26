# TopOpt — Phase 2 (3D + Metal GPU)

## Purpose
Solveur de topology optimization 3D sur GPU via Metal. Algorithme TO
identique à Phase 1 (SIMP + OC + filtre Helmholtz + adjoint analytique
compliance), porté en 3D (hexaèdres H8) avec compute sur GPU Apple Silicon.
Phase 1 (2D CPU, validée) : ../TopOptP1.

## Stack
- C++23, clang, macOS Apple Silicon
- metal-cpp (header-only, macOS26/iOS26) vendorisé dans third_party/metal-cpp
- Shaders compute Metal (.metal → build/shaders.metallib)
- Frameworks : Metal, Foundation, QuartzCore
- Build : Makefile two-phase (shaders → metallib, puis C++ + link)

## Architecture (brief)
Session 1 : fondation Metal seule.
- gpu/MetalContext : device, queue, chargement library, capacités
- shaders/ : kernels compute → build/shaders.metallib
- tests/test_metal_hello : vector-add GPU vs CPU
À venir : assembly FEM 3D, SpMV CSR, CG préconditionné Jacobi sur GPU
(cf. ../TopOptP1/TRANSITIONS.md, section Phase 2).
→ Détails : `docs/ARCHITECTURE.md`

## Directory map
- `src/gpu/` : contexte Metal + (futur) host code des kernels
- `shaders/` : kernels .metal
- `tests/` : mains de test autonomes
- `third_party/metal-cpp/` : metal-cpp Apple vendorisé
- `build/` : objets, .air, .metallib, binaires

## Commands
- Build : `make`
- Test  : `make test`   (lance build/test_metal_hello)
- Run   : `make run`
- Clean : `make clean`

## Read first every session
1. Ce fichier
2. `STATUS.md`
3. `../TopOptP1/TRANSITIONS.md` (roadmap des phases + checkpoints)

## Read on demand
- `docs/ARCHITECTURE.md` — modification structurelle
- `docs/SYMBOLS.md` — localiser un symbole
- `docs/DECISIONS.md` — comprendre un choix passé
- `TASKS.md` — prioriser

## Domain context
Objectif long terme : TO multiphysique fluide-structure-thermique. Phase 2 =
3D structurel sur GPU, précision float à ce stade (choix float/double différé).
Cible 128³ ≈ 6M DOF, CG+Jacobi, < 10 min/solve.

## Project-specific rules
- Continuité Phase 1 : ajouts seulement, pas de refactor du 2D (../TopOptP1).
- metal-cpp : `-fno-objc-arc` sur les TU Metal ; `*_PRIVATE_IMPLEMENTATION`
  dans un seul .cpp (`src/gpu/metal_impl.cpp`) ; headers vendorisés en
  `-isystem` pour garder `-Wall -Wextra -Wpedantic` à zéro warning.
- Pas de framework graphique (compute pur pour l'instant).
- Valider les checkpoints `TRANSITIONS.md` avant de passer à la suite.

## Gotchas
- `newDefaultLibrary()` ne marche qu'en .app bundle ; CLI → `newLibrary(path)`
  avec "build/shaders.metallib".
- `StorageModeShared` sur Apple Silicon (mémoire unifiée) : pas de copie CPU↔GPU.
- `dispatchThreads()` (grille non-uniforme) : Apple Silicon ; threadgroup
  ≤ `maxTotalThreadsPerThreadgroup`, multiple de 32.
- M4 Max → GPU family Apple9, ~55.7 GB recommended working set.
