// M3 acceptance: parse the real solver output for the cooling jacket case
// (regenerate with `./build/topopt_run examples/cooling_jacket_full.topopt.json`
// if absent) plus synthetic coverage of the .vti parser and the cell->point
// conversion used by the iso-surface / slice pipelines.

import { existsSync, readFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { describe, expect, it } from "vitest";

import { cellToPoint, parseVti } from "../src/viewer/vtiParser";

const realPath = fileURLToPath(
  new URL(
    "../../output/cooling_jacket_full/cooling_jacket_full.vti",
    import.meta.url,
  ),
);

describe("parseVti on the real cooling_jacket_full.vti (M3 acceptance)", () => {
  it("exists (run ./build/topopt_run examples/cooling_jacket_full.topopt.json)", () => {
    expect(existsSync(realPath)).toBe(true);
  });

  const data = existsSync(realPath)
    ? parseVti(readFileSync(realPath, "utf-8"))
    : null;

  it("has the 12x12x20 grid of the example", () => {
    expect(data!.cellDims).toEqual([12, 12, 20]);
    expect(data!.pointDims).toEqual([13, 13, 21]);
    expect(data!.spacing).toEqual([1, 1, 1]);
  });

  it("carries the four v3 fields (density, speed, temperature, displacement)", () => {
    const names = data!.fields.map((f) => f.name);
    expect(names).toEqual(["density", "speed", "temperature", "displacement"]);
    for (const f of data!.fields) expect(f.data.length).toBe(12 * 12 * 20);
  });

  it("has plausible ranges per field", () => {
    const by = new Map(data!.fields.map((f) => [f.name, f]));
    const density = by.get("density")!;
    expect(density.min).toBeGreaterThanOrEqual(0);
    expect(density.max).toBeLessThanOrEqual(1);
    // The iso-surface at 0.5 must cut the field (fluid channels + solid both present).
    expect(density.min).toBeLessThan(0.5);
    expect(density.max).toBeGreaterThan(0.5);

    const speed = by.get("speed")!;
    expect(speed.min).toBeGreaterThanOrEqual(0);
    expect(speed.max).toBeGreaterThan(0);

    const temperature = by.get("temperature")!;
    expect(temperature.max).toBeGreaterThan(temperature.min); // non-constant: colormap usable
    expect(temperature.max).toBeGreaterThan(0); // heated walls
    // SUPG-stabilized convection may undershoot below the T=0 inlet Dirichlet,
    // but the undershoot stays small vs the peak (solver reports ~[-1.4, 10.8]).
    expect(Math.abs(temperature.min)).toBeLessThan(temperature.max);

    const displacement = by.get("displacement")!;
    expect(displacement.min).toBeGreaterThanOrEqual(0); // |u| magnitude
  });
});

const SYNTHETIC = `<?xml version="1.0"?>
<VTKFile type="ImageData" version="1.0" byte_order="LittleEndian">
  <ImageData WholeExtent="0 3 0 1 0 1" Origin="1 2 3" Spacing="0.5 0.5 0.5">
    <Piece Extent="0 3 0 1 0 1">
      <CellData Scalars="a">
        <DataArray type="Float32" Name="a" format="ascii">
          0 0.5 1
        </DataArray>
        <DataArray type="Float32" Name="b" format="ascii">
          -1 -2 -3
        </DataArray>
      </CellData>
    </Piece>
  </ImageData>
</VTKFile>
`;

describe("parseVti (synthetic)", () => {
  it("parses dims, origin, spacing and fields with min/max", () => {
    const d = parseVti(SYNTHETIC);
    expect(d.cellDims).toEqual([3, 1, 1]);
    expect(d.pointDims).toEqual([4, 2, 2]);
    expect(d.origin).toEqual([1, 2, 3]);
    expect(d.spacing).toEqual([0.5, 0.5, 0.5]);
    expect(d.fields.map((f) => f.name)).toEqual(["a", "b"]);
    expect([...d.fields[0]!.data]).toEqual([0, 0.5, 1]);
    expect(d.fields[0]!.min).toBe(0);
    expect(d.fields[0]!.max).toBe(1);
    expect(d.fields[1]!.min).toBe(-3);
    expect(d.fields[1]!.max).toBe(-1);
  });

  it("rejects non-ImageData files", () => {
    expect(() =>
      parseVti(`<VTKFile type="PolyData"></VTKFile>`),
    ).toThrow(/ImageData/);
  });

  it("rejects a value count that does not match the extent", () => {
    expect(() => parseVti(SYNTHETIC.replace("0 0.5 1", "0 0.5"))).toThrow(
      /expected 3/,
    );
  });

  it("rejects binary/appended DataArrays with a clear message", () => {
    expect(() =>
      parseVti(SYNTHETIC.replace('format="ascii"', 'format="binary"')),
    ).toThrow(/ascii only/);
  });
});

describe("cellToPoint", () => {
  it("replicates a single cell to its 8 corners", () => {
    const out = cellToPoint([1, 1, 1], new Float32Array([7]));
    expect(out.length).toBe(8);
    expect([...out]).toEqual(new Array(8).fill(7) as number[]);
  });

  it("averages adjacent cells on shared points", () => {
    // Two cells along x: values 0 and 1 -> middle plane of points = 0.5.
    const out = cellToPoint([2, 1, 1], new Float32Array([0, 1]));
    expect(out.length).toBe(3 * 2 * 2);
    for (let p = 0; p < 12; p += 3) {
      expect(out[p]).toBe(0); // x=0 plane
      expect(out[p + 1]).toBe(0.5); // shared plane
      expect(out[p + 2]).toBe(1); // x=2 plane
    }
  });
});
