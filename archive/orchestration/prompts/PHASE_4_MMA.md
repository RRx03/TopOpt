# PHASE 4 — Étape 5 : MMA (Method of Moving Asymptotes, Svanberg 1987)

## Objectif
Implémenter un optimiseur **MMA** réutilisable (remplace l'OC pour le
multi-contraintes), et le valider sur (A) un problème séparable à optimum
analytique connu, et (B) le MBB compliance croisé contre l'OC existant.

## Contexte TopOptP4
- `SIMP3D` (OC actuel, mono-contrainte volume) — sert de référence croisée.
- Les gradients (compliance, stress p-norm) sont validés par DF (gates verts).
- MMA doit être générique : f0 + m contraintes, bornes, gradients fournis.

## Algorithme (Svanberg 1987 — implémente la version standard `mmasub`)
Sous-problème séparable convexe à l'itération k, asymptotes L_j<x_j<U_j :
- Asymptotes : k≤1 → L=x−s0·(xmax−xmin), U=x+s0·(xmax−xmin), s0=0.5 ;
  k≥2 → facteur γ=0.7 si (x^k−x^{k-1})(x^{k-1}−x^{k-2})<0 (oscille, resserre),
  1.2 si >0 (monotone, élargit), 1.0 sinon ; bornes sur les distances d'asymptote.
- Bornes du sous-problème : α_j=max(xmin, L+0.1(x−L), x−move·(xmax−xmin)),
  β_j=min(xmax, U−0.1(U−x), x+move·(xmax−xmin)).
- Approx : f_i ≈ r_i + Σ_j [ p_ij/(U_j−x_j) + q_ij/(x_j−L_j) ], avec
  p_ij=(U_j−x_j)^2 (1.001·max(∂f_i/∂x_j,0)+0.001·max(−∂f_i/∂x_j,0)+1e-5/(xmax−xmin)),
  q_ij=(x_j−L_j)^2 (0.001·max(∂f_i/∂x_j,0)+1.001·max(−∂f_i/∂x_j,0)+1e-5/(xmax−xmin)).
- Sous-problème MMA standard avec variables (x,y,z), params a0=1, a_i=0, c_i=1000,
  d_i=1. **Résolution** : méthode duale OU primal-dual interior point (mmasub).
  Pour m petit (1-3), le dual est recommandé : x_j(λ) en forme close
  `x_j=(√P_j·L_j+√Q_j·U_j)/(√P_j+√Q_j)` clampé [α_j,β_j] (P_j=p0j+Σλ_i p_ij,
  Q_j=q0j+Σλ_i q_ij), puis maximiser la fonction duale concave sur λ≥0
  (bisection si m=1, Newton/IP sinon).

## Validation — ORACLE A (analytique, crisp)
Problème : min f0=Σ_j c_j/x_j  s.t. f1=(Σ_j x_j)/n − vfrac ≤ 0,  x∈[xmin,xmax].
KKT (intérieur) : x_j* = sqrt(c_j·n/λ), λ tel que (Σx_j*)/n=vfrac → x_j*∝√c_j
normalisé au volume (clamp bornes). Choisir n=20, c_j variés, vfrac=0.5.
**PASS si ||x_MMA − x*||_inf / ||x*||_inf < 1e-3** après convergence.

## Validation — ORACLE B (cross-check OC)
MBB 3D compliance (1 contrainte volume), petit cas (ex. 24×8×8). Lancer OC
(SIMP3D existant) et MMA depuis le même init, mêmes p/filtre. **PASS si la
compliance finale MMA est à ±5 % de celle de l'OC** (même bassin), volume tenu.

## Livrables
- `src/topopt/MMAOptimizer.{hpp,cpp}` : API genre
  `MMAResult step(x, f0, df0, fvals[m], dfdx[m][n], xmin, xmax)` avec état interne
  (asymptotes, historique x^{k-1}, x^{k-2}).
- `tests/test_mma.cpp` (CPU pur) : oracle A (analytique) + oracle B (vs OC).
  Ajouté au Makefile (all/test/test_cpu/clean), calqué sur les cibles CPU-pures.
- `make` 0 warning. NE PAS committer (le parent vérifie et committe).
- Rapporter : erreur oracle A, compliance MMA vs OC (oracle B), nb d'itérations,
  et le mode de résolution du sous-problème (dual/IP).

## Garde-fous
- LL-008 : clamp x avant tout pow/division ; borner toute boucle interne (dual,
  bissection, IP) par un nombre max d'itérations.
- Ne maquille pas un échec : si un oracle ne passe pas, rapporte les chiffres.
