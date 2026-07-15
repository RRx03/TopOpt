// Framework-free parser for the .vti files produced by topopt_run
// (VTK XML ImageData, inline ASCII CellData — see src/io/VTKExporter.hpp).
// String/regex based on purpose: runs identically in the browser and in the
// node test runner (no DOMParser), and stays independent of vtk.js so the
// acceptance tests never touch WebGL.

export interface VtiField {
  name: string;
  /** Cell data, VTK order: x fastest, then y, then z. Length = nx*ny*nz. */
  data: Float32Array;
  min: number;
  max: number;
}

export interface VtiData {
  /** Grid resolution in cells (elements). */
  cellDims: [number, number, number];
  /** Point-grid resolution = cellDims + 1 on each axis. */
  pointDims: [number, number, number];
  origin: [number, number, number];
  spacing: [number, number, number];
  fields: VtiField[];
}

function attr(tag: string, name: string): string | null {
  const m = new RegExp(`\\b${name}\\s*=\\s*"([^"]*)"`).exec(tag);
  return m ? m[1]! : null;
}

function numbers(text: string): number[] {
  return text
    .trim()
    .split(/\s+/)
    .filter((t) => t.length > 0)
    .map(Number);
}

function vec3(tag: string, name: string, fallback: [number, number, number]): [number, number, number] {
  const raw = attr(tag, name);
  if (raw === null) return fallback;
  const v = numbers(raw);
  if (v.length !== 3 || v.some((x) => !Number.isFinite(x)))
    throw new Error(`invalid ${name}="${raw}"`);
  return [v[0]!, v[1]!, v[2]!];
}

export function parseVti(text: string): VtiData {
  const root = /<VTKFile\b[^>]*>/.exec(text);
  if (!root) throw new Error("not a VTK XML file (no <VTKFile> element)");
  const kind = attr(root[0], "type");
  if (kind !== "ImageData")
    throw new Error(`unsupported VTKFile type "${kind ?? "?"}" (expected ImageData)`);

  const imgTag = /<ImageData\b[^>]*>/.exec(text);
  if (!imgTag) throw new Error("missing <ImageData> element");
  const extentRaw = attr(imgTag[0], "WholeExtent");
  if (extentRaw === null) throw new Error("missing WholeExtent");
  const ext = numbers(extentRaw);
  if (ext.length !== 6) throw new Error(`invalid WholeExtent "${extentRaw}"`);
  const cellDims: [number, number, number] = [
    ext[1]! - ext[0]!,
    ext[3]! - ext[2]!,
    ext[5]! - ext[4]!,
  ];
  if (cellDims.some((n) => n < 1)) throw new Error(`degenerate extent "${extentRaw}"`);

  const origin = vec3(imgTag[0], "Origin", [0, 0, 0]);
  const spacing = vec3(imgTag[0], "Spacing", [1, 1, 1]);

  const cellData = /<CellData\b[^>]*>([\s\S]*?)<\/CellData>/.exec(text);
  if (!cellData) throw new Error("missing <CellData> (topopt_run writes cell fields)");

  const nCells = cellDims[0] * cellDims[1] * cellDims[2];
  const fields: VtiField[] = [];
  const arrayRe = /<DataArray\b([^>]*)>([\s\S]*?)<\/DataArray>/g;
  for (let m = arrayRe.exec(cellData[1]!); m !== null; m = arrayRe.exec(cellData[1]!)) {
    const name = attr(m[1]!, "Name") ?? `field${fields.length}`;
    const format = attr(m[1]!, "format") ?? "ascii";
    if (format !== "ascii")
      throw new Error(`DataArray "${name}": format "${format}" not supported (ascii only)`);
    const values = numbers(m[2]!);
    if (values.length !== nCells)
      throw new Error(
        `DataArray "${name}": ${values.length} values, expected ${nCells} (${cellDims.join("x")})`,
      );
    const data = new Float32Array(values);
    let min = Infinity;
    let max = -Infinity;
    for (const v of data) {
      if (!Number.isFinite(v)) throw new Error(`DataArray "${name}": non-finite value`);
      if (v < min) min = v;
      if (v > max) max = v;
    }
    fields.push({ name, data, min, max });
  }
  if (fields.length === 0) throw new Error("no <DataArray> in <CellData>");

  return {
    cellDims,
    pointDims: [cellDims[0] + 1, cellDims[1] + 1, cellDims[2] + 1],
    origin,
    spacing,
    fields,
  };
}

// Cell data -> point data (average of the 1..8 cells adjacent to each point).
// vtk.js iso-surfacing and linearly-interpolated slices both want point data;
// the solver writes per-element (cell) fields.
export function cellToPoint(cellDims: readonly number[], cell: Float32Array): Float32Array {
  const [nx, ny, nz] = [cellDims[0]!, cellDims[1]!, cellDims[2]!];
  const [px, py, pz] = [nx + 1, ny + 1, nz + 1];
  const out = new Float32Array(px * py * pz);
  for (let k = 0; k < pz; ++k) {
    const k0 = Math.max(k - 1, 0);
    const k1 = Math.min(k, nz - 1);
    for (let j = 0; j < py; ++j) {
      const j0 = Math.max(j - 1, 0);
      const j1 = Math.min(j, ny - 1);
      for (let i = 0; i < px; ++i) {
        const i0 = Math.max(i - 1, 0);
        const i1 = Math.min(i, nx - 1);
        let sum = 0;
        let n = 0;
        for (let ck = k0; ck <= k1; ++ck)
          for (let cj = j0; cj <= j1; ++cj)
            for (let ci = i0; ci <= i1; ++ci) {
              sum += cell[(ck * ny + cj) * nx + ci]!;
              ++n;
            }
        out[(k * py + j) * px + i] = sum / n;
      }
    }
  }
  return out;
}
