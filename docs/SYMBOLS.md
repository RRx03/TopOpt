# Symbols index — TopOptP2
Last updated: 2026-06-26

## Classes / Structs
- `topopt::Grid3D` — src/core/Grid3D.hpp — grille H8 structurée, numérotation row-major
- `topopt::H8Element` — src/fem/H8Element.hpp — KE0 24×24 + Le/Me 8×8 (Gauss 2×2×2)
- `topopt::FEM3D` — src/fem/FEM3D.hpp — solveur CPU référence (assembly + SimplicialLDLT)
- `topopt::SIMP3D` — src/topopt/SIMP3D.hpp — SIMP + sensibilité compliance + OC bissection
- `topopt::Helmholtz3D` — src/filter/Helmholtz3D.hpp — filtre PDE GPU matrix-free scalaire
- `topopt::gpu::MetalContext` — src/gpu/MetalContext.hpp — device/queue/library/pipeline
- `topopt::gpu::CGSolver3D` — src/gpu/CGSolver3D.hpp — CG matrix-free Jacobi GPU (élasticité)
- `topopt::STLExporter` — src/io/STLExporter.hpp — surface voxels → STL binaire

## Key functions
- `H8Element::stiffness(nu)` — KE0 24×24 unit-modulus (Gauss 2×2×2)
- `H8Element::diffusion()/mass()` — Le/Me pour le filtre Helmholtz
- `CGSolver3D::solve(Emod,F,fixed,U,maxIter,tol)` — résout K(Emod)U=F (matrix-free)
- `CGSolver3D::strainEnergy(ce)` — ce_e = u_e^T KE0 u_e par élément (GPU)
- `Helmholtz3D::apply(xe)` — filtre un champ élément (conserve la moyenne)
- `STLExporter::writeVoxelSurface(path,rhoPhys,grid,thr)` — STL du solide
- `MetalContext::{caps,loadLibrary,makePipeline}()` — gestion device/library/pipelines

## Metal kernels (shaders/fem3d.metal)
- `mf_matvec_elastic`, `mf_diag_elastic`, `mf_strain_energy` — élasticité matrix-free
- `mf_matvec_helmholtz`, `mf_diag_helmholtz`, `helm_scatter`, `helm_gather` — filtre
- `vec_axpy`, `vec_xpby`, `vec_copy`, `vec_dot_partial`, `precond_jacobi`, `zero_fixed`

## Entry points
- `main()` — src/main.cpp — `topopt mbb [nelx nely nelz maxiter]` | `topopt bench [n]`
- `main()` — tests/test_metal_hello.cpp — capacités device + vec_add 1M
