// BC glyphs: arrows (cone + shaft) for loads, support cones / anchor boxes
// for fixed dofs. One color per BC type.

import * as THREE from "three";
import type { BCEntry } from "../spec/ProblemSpec";
import { AXIS, selectorPoints } from "./geometry";

export const COLOR_LOAD = 0xff9f43; // orange — forces
export const COLOR_FIXED = 0x3ddad7; // teal — supports

function axisDir(dof: string): THREE.Vector3 | null {
  const a = AXIS[dof];
  if (a === undefined) return null;
  const e = new THREE.Vector3();
  e.setComponent(a, 1);
  return e;
}

function loadGlyph(p: THREE.Vector3, dir: THREE.Vector3, size: number): THREE.Object3D {
  const arrow = new THREE.ArrowHelper(dir, p, size, COLOR_LOAD, 0.35 * size, 0.18 * size);
  return arrow;
}

const fixedMat = new THREE.MeshBasicMaterial({ color: COLOR_FIXED });

// Support: cone whose apex touches the constrained point, aligned with the
// blocked dof, drawn on the side pointing away from the domain center.
function fixedGlyph(
  p: THREE.Vector3,
  dof: string,
  size: number,
  center: THREE.Vector3,
): THREE.Object3D {
  if (dof === "all") {
    const box = new THREE.Mesh(new THREE.OctahedronGeometry(0.32 * size), fixedMat);
    box.position.copy(p);
    return box;
  }
  const dir = axisDir(dof) ?? new THREE.Vector3(0, 1, 0);
  const side = p.clone().sub(center).dot(dir) >= 0 ? 1 : -1;
  const h = 0.6 * size;
  const cone = new THREE.Mesh(new THREE.ConeGeometry(0.28 * size, h, 12), fixedMat);
  // Cone points along +Y by default (tip at +h/2): orient tip toward p.
  const tipDir = dir.clone().multiplyScalar(-side); // from body to apex
  cone.quaternion.setFromUnitVectors(new THREE.Vector3(0, 1, 0), tipDir);
  cone.position.copy(p).addScaledVector(tipDir, -h / 2);
  return cone;
}

export function buildGlyphs(
  fixed: readonly BCEntry[],
  loads: readonly BCEntry[],
  L: readonly number[],
): THREE.Group {
  const group = new THREE.Group();
  const maxL = Math.max(L[0]!, L[1]!, L[2]!);
  const size = 0.07 * maxL;
  const center = new THREE.Vector3(L[0]! / 2, L[1]! / 2, L[2]! / 2);

  for (const e of fixed)
    for (const p of selectorPoints(e.face, e.edge, L))
      group.add(fixedGlyph(p, e.dof, size, center));

  for (const e of loads) {
    const dir = axisDir(e.dof);
    if (!dir) continue;
    if (e.value < 0) dir.negate();
    for (const p of selectorPoints(e.face, e.edge, L))
      group.add(loadGlyph(p, dir, 1.6 * size));
  }
  return group;
}
