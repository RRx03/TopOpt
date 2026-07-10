# GATE A2 — von Mises p-norm à travers la cascade triple (γ→(u,p)→T→U)

## Objectif
NOUVEAU gradient adjoint : J_σ = p-norm du von Mises relaxé du squelette solide,
comme fonction de γ à travers la cascade triple complète Stokes-Brinkman → CHT →
thermo-élastique. Validation DF OBLIGATOIRE avant tout usage (LL-007, tol < 1e-3).
CPU double (Eigen). Le gradient von Mises existant (ThermoElasticAdjoint::stressPNormGrad)
traverse le chemin bi-bloc CONDUCTION avec ρ=1 matière — il ne convient PAS ici
(cascade CHT, γ=1 fluide) : c'est la raison d'être de ce gate.

## Définition de l'objectif
- Convention v3 : γ=1 fluide, γ=0 solide. Solidité s_e = 1 − γ_e.
- Forward = EXACTEMENT celui de TripleAdjoint::solve (mêmes discrétisations,
  mêmes interpolations E(γ), α(γ), k(γ), F_th) — réutilise ses briques.
- σ_vm : von Mises au centre de chaque élément depuis U (déplacement de la cascade),
  contrainte "solide" σ0_e, relaxée qp : σ_e = s_e^q · σ0_e (q ~ 0.5, même modèle
  que StressModel du chemin bi-bloc si réutilisable — vérifier sa convention et
  l'adapter à s=1−γ).
- J_σ = (Σ_e σ_e^P)^{1/P}, P=8.

## Gradient (cascade inverse complète)
dJ_σ/dγ_i = terme explicite (relaxation ds^q/dγ = −q·s^{q−1}, et ∂σ0/∂γ via E(γ)
si σ0 dépend de E — expliciter le choix et le documenter)
  + λ_e^T [ (dK_e/dγ_i) U − dF_th/dγ_i ]      (K_e λ_e = −∂J_σ/∂U)
  + λ_t^T (dK_t/dγ_i) T                        (K_t^T λ_t = G^T λ_e − ∂R_t^T/∂T… selon structure TripleAdjoint)
  + λ_s^T (dA/dγ_i) w                          (adjoint Stokes semé par ∂R_t/∂u — le terme piège)
La STRUCTURE de la cascade inverse est celle de TripleAdjoint::solve (validée) ;
seul le SEMIS change : ∂J/∂U = grad p-norm von Mises au lieu de Fmech.

## Implémentation
- NE PAS modifier TripleAdjoint::solve ni aucun test existant. Deux options :
  (a) nouvelle méthode `TripleAdjoint::solveStress(gamma, StressParams)` factorée
  sur les briques privées existantes (préférée si propre), ou (b) classe sœur
  réutilisant les mêmes helpers. Choisir ce qui garde le gate triple existant vert.
- Fichier de test : tests/test_vm_triple_fd.cpp. Grille 6×6×6 (comme
  test_triple_adjoint_fd), γ intérieur ∈ (0.3, 0.7) (éviter bornes), DF centrale
  4e ordre, eps ~1e-6, TOL max rel err < 1e-3.
- Le test doit vérifier que les TROIS contributions (Stokes, thermique, élastique)
  sont NON-TRIVIALES (normes rapportées) — sinon le gate ne prouve rien sur les
  termes de couplage (dont ∂R_t/∂u).
- Câbler dans le Makefile (cible test_cpu).

## Livrables
- solveStress (ou équivalent) + test_vm_triple_fd. make 0 warning.
- make test_cpu : TOUS verts, y compris le nouveau ET les 6 anciens gates inchangés.
- NE PAS committer. Rapporter : erreur DF max, normes des 3 contributions, choix
  d'implémentation (a/b), tout écart de convention rencontré.

## Garde-fous
LL-007/008/009/010. Oracle en CPU double. Si la DF ne passe pas < 1e-3, NE PAS
maquiller (pas de tolérance élargie, pas de terme supprimé) : rapporter l'échec
avec le diagnostic (terme par terme vs DF partielle).
