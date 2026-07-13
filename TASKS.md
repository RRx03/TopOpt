# Tasks — TopOptP4 (Phase 4)

## Done (2026-06-27/29)
- [x] ThermalSolver (GPU) + test (8.9e-7)
- [x] ThermoElasticCoupling + test dilatation libre (7.4e-6)
- [x] StressModel (von Mises + qp-relax + p-norm) + test
- [x] ThermoElasticAdjoint (compliance) — GATE DF 1.6e-6
- [x] Sensibilité stress p-norm — GATE DF 1.6e-7
- [x] MMAOptimizer (Svanberg dual) — analytique 6.4e-14, OC 0.037 %
- [x] FEM axisym (Grid2DAxi/AxiQ4Element/FEM2DAxi) — Lamé ordre 2
- [x] AxiStressAdjoint — GATE DF 2.7e-9
- [x] nozzle_axi : tuyère axisym structurelle, épaississement au col 3.11×

## Backlog / différé (Phase 4+ ou début Phase 5)
- [ ] Thermique axisym + couplage + adjoint thermo-axi (+ gate DF)
- [ ] Démo 3D thermo-élastique complète (mass-min sous von Mises+T_max, MMA)
- [ ] Portage GPU des adjoints (matrix-free float32, re-validation)

## Git log (recent) — voir `git log` (P4)
