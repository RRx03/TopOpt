# Symbols index — TopOptP2
Last updated: 2026-06-15

## Classes / Structs
- `DeviceCaps` — src/gpu/MetalContext.hpp:16 — nom, GPU family, working set, unified mem
- `MetalContext` — src/gpu/MetalContext.hpp:25 — possède device/queue/library, construit les pipelines

## Key functions
- `MetalContext::caps()` — src/gpu/MetalContext.cpp:33 — interroge les capacités device
- `MetalContext::loadLibrary()` — src/gpu/MetalContext.cpp:43 — charge un .metallib depuis le disque
- `MetalContext::makePipeline()` — src/gpu/MetalContext.cpp:60 — compute pipeline pour un kernel
- `vec_add()` — shaders/vector_add.metal:5 — kernel compute c = a + b

## Entry points
- `main()` — tests/test_metal_hello.cpp:15 — capacités device + validation vec add 1 M
