# Symbols index — TopOptP4
Last updated: 2026-06-28

## Classes / Structs (Phase 5)
- `topopt::StokesSolver` — src/physics/StokesSolver.hpp — Stokes Q1-Q1 PSPG + Brinkman α(γ)u (saddle-point, Eigen direct)
- `topopt::CHTSolver` — src/physics/CHTSolver.hpp — advection-diffusion (conjugate heat transfer) + SUPG, non-symétrique

## Classes / Structs (Phase 4)
- `topopt::gpu::ThermalSolver` — src/physics/ThermalSolver.hpp — conduction GPU matrix-free (−div(k∇T)=q)
- `topopt::ThermoElasticCoupling` — src/physics/ThermoElasticCoupling.hpp — F_th = E_e·α·Cth·(T_e−Tref)
- `topopt::StressModel` — src/topopt/StressModel.hpp — von Mises + qp-relaxation ρ^q + p-norm
- `topopt::ThermoElasticAdjoint` — src/adjoint/ThermoElasticAdjoint.hpp — adjoint 2 blocs (compliance + stress p-norm), validé DF
- `topopt::MMAOptimizer` — src/topopt/MMAOptimizer.hpp — MMA (Svanberg 1987), sous-problème dual, multi-contraintes
- `topopt::Grid2DAxi` — src/core/Grid2DAxi.hpp — grille Q4 axisymétrique (r,z), 2 DOF/nœud
- `topopt::AxiQ4Element` — src/fem/AxiQ4Element.hpp — élément Q4 axisym (ε_θ=u_r/r, D 4×4)
- `topopt::FEM2DAxi` — src/fem/FEM2DAxi.hpp — solveur axisym CPU (LDLT) + pression interne + contrainte
- `H8Element::thermalCoupling/stressMatrix/vonMisesForm` — Cth 24×8, S0 6×24, V 6×6


## Classes / Structs (hérités Phase 2)
- `topopt::Grid3D` — src/core/Grid3D.hpp — grille H8 structurée, numérotation row-major
- `topopt::H8Element` — src/fem/H8Element.hpp — KE0 24×24 + Le/Me 8×8 (Gauss 2×2×2)
- `topopt::FEM3D` — src/fem/FEM3D.hpp — solveur CPU référence (assembly + SimplicialLDLT)
- `topopt::SIMP3D` — src/topopt/SIMP3D.hpp — SIMP + sensibilité + OC (clamp rhoPhys, LL-008)
- `topopt::Helmholtz3D` — src/filter/Helmholtz3D.hpp — filtre PDE GPU matrix-free scalaire
- `topopt::gpu::MetalContext` — src/gpu/MetalContext.hpp — device/queue/library/pipeline
- `topopt::gpu::CGSolver3D` — src/gpu/CGSolver3D.hpp — CG matrix-free Jacobi GPU (élasticité)
- `topopt::STLExporter` — src/io/STLExporter.hpp — surface voxels → STL binaire
- `topopt::VTKExporter` — src/io/VTKExporter.hpp — export .vti (champs cellule) pour ParaView

## Classes / Structs (Phase 3)
- `topopt::Grid3DMultiLevel` — src/core/Grid3DMultiLevel.hpp — hiérarchie ×2, cell size mm/niveau
- `topopt::GridTransfer` — src/topopt/GridTransfer.hpp — prolongation/restriction densité conservatives
- `topopt::ContinuationPolicy` — src/topopt/ContinuationPolicy.hpp — inherit/restart/custom (+ ContinuationParams)
- `topopt::MultiGridOptimizer` — src/topopt/MultiGridOptimizer.hpp — loop coarse→fine warm-start
- `topopt::HelmholtzFilterPhysical` — src/filter/HelmholtzFilterPhysical.hpp — filtre rayon mm

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

## Key functions (Phase 3)
- `GridTransfer::prolongateDensity/restrictDensity` — transfert densité conservatif
- `MultiGridOptimizer::run(bc)` — optimisation multi-grid warm-start
- `ContinuationPolicy::at(level,nLevels,localIter)` — résout les params de continuation
- `Grid3DMultiLevel::cellSize(l)` — taille de cellule physique (mm) au niveau l

## Entry points
- `main()` — src/main.cpp — `topopt mbb [...] | mg [nFine nLevels itPerLevel inherit|restart] | bench [n]`
- `main()` — tests/test_metal_hello.cpp — capacités device + vec_add 1M
- `main()` — tests/test_multigrid.cpp — round-trip + conservation volume + hiérarchie
