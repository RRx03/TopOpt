# Status — 2026-06-27

## Current focus
**Phase 4 (NIVEAU CRITIQUE), session 1 faite** : solveur thermique stationnaire seul.

## Last session (2026-06-27)
- Squelette P4 depuis base multi-grid Phase 3 (build 0 warning).
- `ThermalSolver` : conduction `−div(k∇T)=q`, matrix-free GPU scalaire (Laplacien
  H8 `H8Element::diffusion()`, CG Jacobi). Kernels `shaders/thermal.metal`.
- `test_thermal` : plaque gradient linéaire T(L) à 8.9e-7 (patch exact float),
  linéarité exacte. Régression suite héritée : OK. 0 warning.

## Next up (Phase 4, ordre du brief)
2. Couplage thermo-élastique faible : ε_th=α(T−T_ref) → F_thermal, validation 3D.
3. von Mises + p-norm + **ε-relaxation** (LL-LIT-001).
4. **Adjoint 2 blocs + validation DF 10×10** (LL-LIT-007) — GATE BLOQUANT.
5. **MMA** (remplace OC).
6. Géométrie 2D axisymétrique (singularité r=0).
7. Cas tuyère 2D axi (pression + flux thermique) ; épaississement au col.

## Blockers / vigilance
- Adjoint multi-bloc : NE PAS avancer sans validation DF à 1e-5.
- Stress singularity : ε-relaxation dès la 1ère version des contraintes.
- LL-008 (déjà rencontré P3) : clamper densité avant pow, borner bissections.

## Don't touch
- `../TopOptP1`, `../TopOptP2`, `../TopOptP3` (figés).
- `third_party/*` (symlinks vers ../shared/third_party).
