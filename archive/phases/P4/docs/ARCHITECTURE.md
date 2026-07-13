# Architecture — TopOptP2 (Phase 2)

## Overview
Fondation GPU Metal pour un futur solveur TO 3D. À ce stade : gestion
device/queue, chargement de la bibliothèque de shaders, et une pipeline
compute de démonstration (addition vectorielle). Le pipeline TO (FEM 3D →
CG → SIMP/OC) sera construit par-dessus dans les sessions suivantes.

## Modules

### MetalContext — `src/gpu/MetalContext.{hpp,cpp}`
- Responsabilité : possède `MTL::Device` + `CommandQueue`, charge un
  `.metallib`, construit des compute pipelines, expose les capacités device.
- Dépendances : metal-cpp (Foundation, Metal).

### metal_impl — `src/gpu/metal_impl.cpp`
- Responsabilité : unique TU définissant `NS/CA/MTL_PRIVATE_IMPLEMENTATION`.
- Doit rester le SEUL endroit où ces macros sont définies (sinon doublons de
  symboles à l'édition de liens).

### Shaders — `shaders/*.metal`
- Responsabilité : kernels compute Metal, compilés en `build/shaders.metallib`.
- Actuel : `vector_add.metal` (kernel `vec_add`).

## Data flow (démo actuelle)
```
buffers host (StorageModeShared)
  → encoder.setBuffer(a,b,c) + setBytes(n)
  → dispatchThreads(vec_add, grid=n, tg≤256)
  → commit + waitUntilCompleted
  → lecture c->contents() → comparaison au calcul CPU
```

## Build flow
```
.metal  --xcrun metal-->  .air  --xcrun metallib-->  build/shaders.metallib
.cpp    --clang++ -std=c++23 -fno-objc-arc -isystem metal-cpp-->  .o
        --link -framework Metal/Foundation/QuartzCore-->  build/test_metal_hello
```

## Threading model
- CPU mono-thread côté host ; parallélisme sur le GPU (threadgroups Metal).
- SIMD width Apple Silicon = 32 → threadgroups multiples de 32.

## External dependencies
- metal-cpp (macOS26/iOS26), header-only, `third_party/metal-cpp`.
- Frameworks système : Metal, Foundation, QuartzCore.
