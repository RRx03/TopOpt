# Tasks — TopOptP3 (Phase 3)

## Active
- (Phase 3 clôturée — voir Phase 4 : ../orchestration/prompts/PHASE_4_BRIEF.md)

## Done (2026-06-26/27)
- [x] Squelette P3 depuis base matrix-free P2
- [x] Grid3DMultiLevel (hiérarchie ×2, cell size physique)
- [x] GridTransfer prolongation/restriction densité conservatives (round-trip 2.2e-16)
- [x] HelmholtzFilterPhysical (rayon mm) — mesh independence
- [x] ContinuationPolicy (inherit/restart/custom, struct générique P4-P5)
- [x] MultiGridOptimizer (warm-start coarse→fine)
- [x] Speedup mesuré : 128³ 998→419 s (2.4×, < 10 min)
- [x] Mesh independence : STLs 32/64/128 à rayon physique fixe
- [x] Fix LL-008 (clamp rhoPhys + cap bissection)

## Backlog / différé
- [ ] Préconditionneur multigrid V-cycle (prompt: PHASE_3_REPORT.md §8) — vers 5-10×
- [ ] Benchmarker restart/custom sur cas à features fines (en P4, tuyère)
- [ ] Marching cubes lisse (remplace surface voxels)

## Git log (recent)
- voir `git log` (P3)
