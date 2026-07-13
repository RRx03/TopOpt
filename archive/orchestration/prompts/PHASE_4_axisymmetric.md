# PHASE 4 — Étape 6 : FEM 2D axisymétrique (r,z)

## Objectif
Élément Q4 axisymétrique + solveur CPU, validé contre la solution analytique de
Lamé (cylindre épais sous pression interne). Brique géométrique du cas tuyère.

## Formulation (à implémenter)
- `Grid2DAxi` : grille structurée Q4 sur [a,b]×[0,H] (a=r_inner>0, b=r_outer).
  Nœud (i,j), i sur r (0..nr), j sur z (0..nz). r(i)=a+i·hr, hr=(b−a)/nr, hz=H/nz.
  2 DOF/nœud : (u_r, u_z) → dof 2n, 2n+1. node_id=i+j·(nr+1).
- Élément Q4 axisym, 4 déformations [ε_r, ε_z, ε_θ, γ_rz] :
  ε_r=∂u_r/∂r, ε_z=∂u_z/∂z, **ε_θ=u_r/r**, γ_rz=∂u_r/∂z+∂u_z/∂r.
  B (4×8) : pour nœud a (dofs 2a=u_r, 2a+1=u_z) :
  B(0,2a)=dN_a/dr ; B(1,2a+1)=dN_a/dz ; **B(2,2a)=N_a/r** ; B(3,2a)=dN_a/dz, B(3,2a+1)=dN_a/dr.
- D axisym isotrope (4×4), ordre [r,z,θ,rz] :
  D=E/((1+ν)(1−2ν))·[[1−ν,ν,ν,0],[ν,1−ν,ν,0],[ν,ν,1−ν,0],[0,0,0,(1−2ν)/2]].
- K_e = Σ_gauss w·Bᵀ D B·r_g·detJ (Gauss 2×2 ; r_g=Σ N_a(g) r_a ; le 2π est
  constant, on peut l'omettre de façon cohérente — documente). Stockage Eigen,
  solveur SimplicialLDLT (référence CPU double, comme FEM3D).
- **Singularité r=0** : NON présente ici (a>0). Documenter que pour une géométrie
  touchant l'axe il faudrait nœuds à r=ε>0 ou formulation spéciale.

## BC pression interne (Neumann)
Pression p_i sur la face r=a → forces nodales radiales cohérentes :
sur chaque nœud de la face interne, F_r += p_i · r=a · (hz tributaire) (poids 0.5
aux extrémités z, 1 sinon ; cohérent avec l'intégration en r dθ dz, 2π omis).

## Validation — ORACLE Lamé (cylindre épais, pression interne seule)
a=1, b=2, p_i=1, E=1, ν=0.3. Plane strain : fixer u_z=0 sur z=0 et z=H (tranche).
Constantes : A=p_i a²/(b²−a²), B=p_i a²b²/(b²−a²).
- Contraintes (indépendantes de ν) : σ_r(r)=A−B/r², **σ_θ(r)=A+B/r²**.
  σ_θ(a)=p_i(b²+a²)/(b²−a²)=5/3 pour a=1,b=2.
- Déplacement (plane strain) : u_r(r)=((1+ν)/E)[(1−2ν)A r + B/r].
Test : σ_θ par élément (stress = D·B·u au centroïde, composante θ) et u_r aux
nœuds, comparés à Lamé. FEM sur grille structurée = erreur de discrétisation :
**PASS si erreur relative max < 2 %** à nr=40 (et idéalement décroît si on raffine).

## Livrables
- `src/core/Grid2DAxi.hpp`, `src/fem/AxiQ4Element.{hpp,cpp}` (stiffness + stressMatrix),
  `src/fem/FEM2DAxi.{hpp,cpp}` (assemblage + solve + recouvrement contrainte).
- `tests/test_axisymmetric.cpp` (CPU pur) : Lamé σ_θ et u_r, + convergence nr=20→40.
  Cible Makefile CPU-pure, ajoutée à all/test/test_cpu/clean.
- `make` 0 warning. NE PAS committer. Rapporter erreurs σ_θ et u_r, et la
  convergence sous raffinement.
