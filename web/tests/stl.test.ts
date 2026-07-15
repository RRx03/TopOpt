// STL parser tests: synthetic binary + ASCII coverage, and the real
// marching-cubes output of the mbb3d run when present (output/ artifacts are
// not versioned and only the cooling-jacket solver run is sanctioned by M3,
// so the real-file check is skipped on a fresh clone).

import { existsSync, readFileSync, statSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { describe, expect, it } from "vitest";

import { parseStl } from "../src/viewer/stlParser";

function binaryStl(triangles: number[][][]): ArrayBuffer {
  const buf = new ArrayBuffer(84 + 50 * triangles.length);
  const view = new DataView(buf);
  view.setUint32(80, triangles.length, true);
  let off = 84;
  for (const tri of triangles) {
    off += 12; // normal left at zero -> parser recomputes it
    for (const v of tri) {
      view.setFloat32(off, v[0]!, true);
      view.setFloat32(off + 4, v[1]!, true);
      view.setFloat32(off + 8, v[2]!, true);
      off += 12;
    }
    off += 2;
  }
  return buf;
}

describe("parseStl (binary, synthetic)", () => {
  it("reads triangle count and vertices, recomputes degenerate normals", () => {
    const mesh = parseStl(
      binaryStl([
        [
          [0, 0, 0],
          [1, 0, 0],
          [0, 1, 0],
        ],
        [
          [0, 0, 1],
          [1, 0, 1],
          [0, 1, 1],
        ],
      ]),
    );
    expect(mesh.triangleCount).toBe(2);
    expect(mesh.positions.length).toBe(18);
    expect(mesh.normals.length).toBe(18);
    // CCW triangle in the z=0 plane -> +z normal, replicated on 3 vertices.
    expect([...mesh.normals.slice(0, 9)]).toEqual([0, 0, 1, 0, 0, 1, 0, 0, 1]);
    expect(mesh.positions[3]).toBe(1);
  });

  it("rejects garbage", () => {
    expect(() => parseStl(new ArrayBuffer(10))).toThrow(/not an STL/);
  });
});

describe("parseStl (ASCII)", () => {
  it("parses facets", () => {
    const text = `solid demo
  facet normal 0 0 1
    outer loop
      vertex 0 0 0
      vertex 1 0 0
      vertex 0 1 0
    endloop
  endfacet
endsolid demo
`;
    const mesh = parseStl(new TextEncoder().encode(text).buffer as ArrayBuffer);
    expect(mesh.triangleCount).toBe(1);
    expect([...mesh.normals.slice(0, 3)]).toEqual([0, 0, 1]);
  });
});

const realPath = fileURLToPath(
  new URL("../../output/mbb3d/mbb3d.stl", import.meta.url),
);

describe.skipIf(!existsSync(realPath))("parseStl on the real mbb3d.stl", () => {
  it("triangle count matches the binary layout", () => {
    const raw = readFileSync(realPath);
    const buf = raw.buffer.slice(raw.byteOffset, raw.byteOffset + raw.byteLength);
    const mesh = parseStl(buf);
    expect(mesh.triangleCount).toBe((statSync(realPath).size - 84) / 50);
    expect(mesh.triangleCount).toBeGreaterThan(1000);
    for (const p of mesh.positions.subarray(0, 30)) expect(Number.isFinite(p)).toBe(true);
  });
});
