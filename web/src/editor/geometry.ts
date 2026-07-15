// Box-domain geometry helpers: face frames, edge segments, glyph anchor
// points. The domain is [0,Lx] x [0,Ly] x [0,Lz] (corner at the origin), so
// face/edge selectors map directly to the C++ BCResolver semantics.

import * as THREE from "three";
import type { Face } from "../state";

export const AXIS: Record<string, number> = { x: 0, y: 1, z: 2 };

export function faceAxis(face: Face): number {
  return AXIS[face[0]!]!;
}
export function faceSign(face: Face): number {
  return face[1] === "+" ? 1 : -1;
}

export interface FaceFrame {
  center: THREE.Vector3;
  normal: THREE.Vector3;
  u: THREE.Vector3; // in-plane tangent
  v: THREE.Vector3;
  su: number; // physical extents along u, v
  sv: number;
}

function unit(axis: number): THREE.Vector3 {
  const e = new THREE.Vector3();
  e.setComponent(axis, 1);
  return e;
}

export function faceFrame(face: Face, L: readonly number[]): FaceFrame {
  const a = faceAxis(face);
  const s = faceSign(face);
  const ua = (a + 1) % 3;
  const va = (a + 2) % 3;
  const center = new THREE.Vector3(L[0]! / 2, L[1]! / 2, L[2]! / 2);
  center.setComponent(a, s > 0 ? L[a]! : 0);
  return {
    center,
    normal: unit(a).multiplyScalar(s),
    u: unit(ua),
    v: unit(va),
    su: L[ua]!,
    sv: L[va]!,
  };
}

// Segment [p0, p1] = intersection of two adjacent faces.
export function edgeSegment(
  a: Face,
  b: Face,
  L: readonly number[],
): [THREE.Vector3, THREE.Vector3] {
  const free = 3 - faceAxis(a) - faceAxis(b);
  const p0 = new THREE.Vector3();
  for (const f of [a, b])
    p0.setComponent(faceAxis(f), faceSign(f) > 0 ? L[faceAxis(f)]! : 0);
  const p1 = p0.clone();
  p1.setComponent(free, L[free]!);
  return [p0, p1];
}

// Anchor points where BC glyphs are drawn for a face / edge selector.
export function facePoints(face: Face, L: readonly number[]): THREE.Vector3[] {
  const f = faceFrame(face, L);
  const fr = [-0.3, 0, 0.3];
  const pts: THREE.Vector3[] = [];
  for (const iu of fr)
    for (const iv of fr)
      pts.push(
        f.center
          .clone()
          .addScaledVector(f.u, iu * f.su)
          .addScaledVector(f.v, iv * f.sv),
      );
  return pts;
}

export function edgePoints(
  a: Face,
  b: Face,
  L: readonly number[],
  n = 5,
): THREE.Vector3[] {
  const [p0, p1] = edgeSegment(a, b, L);
  const pts: THREE.Vector3[] = [];
  for (let i = 0; i < n; i++)
    pts.push(p0.clone().lerp(p1, (i + 0.5) / n));
  return pts;
}

// Anchor points for a raw selector string pair (face or "f1,f2" edge).
export function selectorPoints(
  face: string,
  edge: string,
  L: readonly number[],
): THREE.Vector3[] {
  if (face) return facePoints(face as Face, L);
  if (edge) {
    const [a, b] = edge.split(",") as [Face, Face];
    if (a && b) return edgePoints(a, b, L);
  }
  return [];
}
