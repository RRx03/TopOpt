// Framework-free STL parser (binary — what topopt_run writes — plus ASCII as
// a convenience). Returns a triangle soup: positions/normals as 9 floats per
// triangle, normals replicated per vertex (flat shading, faithful to the
// marching-cubes / voxel geometry the solver exports).

export interface StlMesh {
  triangleCount: number;
  /** 9 floats per triangle (3 vertices). */
  positions: Float32Array;
  /** One normal per vertex, replicated from the facet normal. */
  normals: Float32Array;
}

function facetNormal(p: Float32Array, t: number, stored: [number, number, number]): [number, number, number] {
  const [sx, sy, sz] = stored;
  if (sx * sx + sy * sy + sz * sz > 1e-12) return stored;
  // Degenerate stored normal: recompute from the winding.
  const o = 9 * t;
  const ux = p[o + 3]! - p[o]!;
  const uy = p[o + 4]! - p[o + 1]!;
  const uz = p[o + 5]! - p[o + 2]!;
  const vx = p[o + 6]! - p[o]!;
  const vy = p[o + 7]! - p[o + 1]!;
  const vz = p[o + 8]! - p[o + 2]!;
  const nx = uy * vz - uz * vy;
  const ny = uz * vx - ux * vz;
  const nz = ux * vy - uy * vx;
  const len = Math.hypot(nx, ny, nz) || 1;
  return [nx / len, ny / len, nz / len];
}

function parseBinary(buf: ArrayBuffer): StlMesh {
  const view = new DataView(buf);
  const count = view.getUint32(80, true);
  const positions = new Float32Array(9 * count);
  const normals = new Float32Array(9 * count);
  let off = 84;
  for (let t = 0; t < count; ++t) {
    const stored: [number, number, number] = [
      view.getFloat32(off, true),
      view.getFloat32(off + 4, true),
      view.getFloat32(off + 8, true),
    ];
    off += 12;
    for (let v = 0; v < 3; ++v) {
      positions[9 * t + 3 * v] = view.getFloat32(off, true);
      positions[9 * t + 3 * v + 1] = view.getFloat32(off + 4, true);
      positions[9 * t + 3 * v + 2] = view.getFloat32(off + 8, true);
      off += 12;
    }
    off += 2; // attribute byte count
    const [nx, ny, nz] = facetNormal(positions, t, stored);
    for (let v = 0; v < 3; ++v) {
      normals[9 * t + 3 * v] = nx;
      normals[9 * t + 3 * v + 1] = ny;
      normals[9 * t + 3 * v + 2] = nz;
    }
  }
  return { triangleCount: count, positions, normals };
}

function parseAscii(text: string): StlMesh {
  const vertexRe = /vertex\s+([^\s]+)\s+([^\s]+)\s+([^\s]+)/g;
  const coords: number[] = [];
  for (let m = vertexRe.exec(text); m !== null; m = vertexRe.exec(text)) {
    coords.push(Number(m[1]), Number(m[2]), Number(m[3]));
  }
  if (coords.length === 0 || coords.length % 9 !== 0)
    throw new Error(`ASCII STL: ${coords.length / 3} vertices (expected a multiple of 3)`);
  if (coords.some((c) => !Number.isFinite(c))) throw new Error("ASCII STL: non-finite vertex");
  const count = coords.length / 9;
  const positions = new Float32Array(coords);
  const normals = new Float32Array(coords.length);
  for (let t = 0; t < count; ++t) {
    const [nx, ny, nz] = facetNormal(positions, t, [0, 0, 0]);
    for (let v = 0; v < 3; ++v) {
      normals[9 * t + 3 * v] = nx;
      normals[9 * t + 3 * v + 1] = ny;
      normals[9 * t + 3 * v + 2] = nz;
    }
  }
  return { triangleCount: count, positions, normals };
}

export function parseStl(buf: ArrayBuffer): StlMesh {
  // Binary if the 80-byte header + count matches the file size exactly
  // (an ASCII file starting with "solid" can't collide with that check).
  if (buf.byteLength >= 84) {
    const count = new DataView(buf).getUint32(80, true);
    if (84 + 50 * count === buf.byteLength) return parseBinary(buf);
  }
  const head = new TextDecoder().decode(buf.slice(0, 512));
  if (/^\s*solid\b/.test(head)) return parseAscii(new TextDecoder().decode(buf));
  throw new Error("not an STL file (neither valid binary layout nor ASCII 'solid')");
}
