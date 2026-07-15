# TopOpt — Multiphysics Topology Optimization on Apple Silicon

A fluid–structure–thermal **topology optimization** solver written from scratch in
**C++20 / Metal**, driven by a JSON problem-description language and rendered in
ParaView. Designed, implemented and validated end-to-end: every adjoint gradient
is finite-difference-verified before use, every physics solver is checked against
an analytical solution.

```
write problem.topopt.json  →  ./build/topopt_run problem.topopt.json  →  ParaView
```

## What it does

| Dispatch | Physics | Objective / constraints | Example |
|---|---|---|---|
| structural (GPU) | 3D elasticity, matrix-free CG on Metal | compliance + volume | `examples/mbb3d.topopt.json` |
| thermo-elastic | conduction → elasticity (coupled adjoint) | mass s.t. von Mises | `examples/bracket_thermo.topopt.json` |
| fluid-thermal | Stokes-Brinkman → CHT → elasticity (**triple-coupled adjoint**) | compliance s.t. up to **4 simultaneous constraints** (volume, T_max, ΔP, von Mises) | `examples/cooling_jacket_full.topopt.json` |
| axisymmetric | Q4 r-z, body-fitted convergent-divergent bore | mass s.t. von Mises | `examples/nozzle_profiled.topopt.json` |

Highlight: the cooling-jacket demonstrator optimizes coolant channels inside a
nozzle wall — the optimizer discovers channel networks concentrated at the throat
(where the heat load peaks) while keeping the wall load-bearing, with all four
constraints active at convergence.

## Method

- **Density method (SIMP)** with penalization continuation, Helmholtz/density
  filtering (radius in mm, mesh-independent), Heaviside projection (β continuation)
  → quasi-binary designs (grey fraction < 0.1).
- **Discrete adjoints** for every objective/constraint. The hardest one is the
  triple-coupled cascade: design γ → Stokes-Brinkman flow (u,p) → advected
  temperature T (SUPG) → thermo-elastic displacement U; its adjoint runs the
  cascade backwards (λ_e → λ_t → λ_s), including the advection coupling term
  ∂R_t/∂u that links the thermal adjoint to the flow adjoint.
- **MMA optimizer** (Svanberg): dual bisection (m=1) and projected Newton (m≥2),
  validated against an analytical optimum and an independent projected-gradient
  reference.
- **GPU**: 3D elasticity is matrix-free (element-by-element H8) with Jacobi-CG
  and multigrid warm-start, in Metal compute — no assembled stiffness matrix.

## Validation discipline

**Seven adjoint gates** — each gradient is compared element-wise to 4th-order
central finite differences on a small case before it is ever used (tolerance
1e-3, achieved 1e-6…1e-9):

| Adjoint gate | max FD rel. error |
|---|---|
| thermo-elastic compliance (2 blocks) | 1.6e-6 |
| von Mises p-norm, 3D | 1.6e-7 |
| von Mises, axisymmetric | 2.7e-9 |
| **triple-coupled** (Stokes–CHT–elastic) | **2.1e-7** |
| viscous dissipation (fluid TO) | 7.2e-7 |
| wall temperature T_max | 7.5e-8 |
| von Mises through the triple cascade | 8.0e-7 |

Physics solvers are checked against analytical oracles: FEM patch test, Lamé
thick cylinder, Poiseuille, Darcy-Brinkman (cosh profile), advection-diffusion
(exponential boundary layer). Literature check: the Borrvall-Petersson (2003)
diffuser is reproduced. `make test_cpu` runs the whole suite (17 binaries).

## Build & run

Requires macOS on Apple Silicon (Metal) + Xcode command-line tools. All
third-party deps (Eigen, metal-cpp, nlohmann/json) are vendored — no setup.

```sh
make -j            # solver + tests + shaders.metallib
make test_cpu      # full validation suite (oracles + 7 FD gates)
./build/topopt_run examples/cooling_jacket_full.topopt.json
# → output/cooling_jacket_full/cooling_jacket_full.vti  (open in ParaView)
```

Outputs: `.vti` volumes (density, von Mises, temperature, velocity,
displacement) for ParaView, and marching-cubes `.stl` geometry.

## Documentation

- [`docs/THEORY.md`](docs/THEORY.md) — the full method: SIMP, filtering,
  adjoints (mono/multi/triple-coupled), MMA, validation, honest limitations.
- [`docs/INPUT_LANGUAGE.md`](docs/INPUT_LANGUAGE.md) — the `.topopt.json`
  schema: domain, materials, physics, boundary-condition selectors, objectives,
  constraints, outputs.
- [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) — modules and data flow.
- `archive/` — full project history: per-phase snapshots (P1 2D CPU → P5
  multiphysics), phase reports, and the orchestration specs used to develop and
  gate each feature.

## TopOpt Studio (web UI)

`web/` hosts **TopOpt Studio** — a static web app (three.js + vtk.js, no
backend) closing the author→solve→inspect loop:

```sh
cd web && npm install && npm run dev
```

- **Author**: 3D box editor, click faces/edges to place supports and loads,
  parameter panels, live JSON preview, export `.topopt.json`.
- **Import**: lossless round-trip of any spec (advanced fluid/thermal BCs shown
  read-only), embedded gallery of the repo examples, compatibility validation.
- **Results**: load the solver's `.vti`/`.stl` — iso-surfaces, orthogonal
  slices, colormaps (e.g. coolant-channel iso + temperature slice).

Acceptance is anchored to the solver: the Studio-exported MBB spec reproduces
C=18.5216 exactly. Spec and long-term vision (coupled interface constraints,
variable geometry, robustness, per-zone materials):
[`docs/WEB_MODELER_SPEC.md`](docs/WEB_MODELER_SPEC.md).

## Roadmap

- Studio V2: in-UI editing of thermal/flow BCs, axisymmetric profile editor.
- Coupled interface constraints (inlet/outlet flow disks, variable spacing) —
  new FD-gated flow-target functionals; see spec §7.
- GPU port of the multiphysics adjoints (currently CPU double precision by
  design: correctness first).
