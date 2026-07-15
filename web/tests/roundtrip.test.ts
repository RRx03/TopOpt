// M2 round-trip: each of the 6 repo examples imports, re-exports and
// re-imports to a semantically identical spec (deep-equal on normalized
// specs, defaults included). Explicit spot checks cover the serialization
// traps called out by the milestone: body_force/drive, nozzle params,
// max_rel, heaviside schedules.

import { readFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { describe, expect, it } from "vitest";

import { specFromString, specToJson, specToString } from "../src/spec/serialize";

const EXAMPLES = [
  "mbb3d",
  "bracket_thermo",
  "cooling_jacket",
  "cooling_jacket_multi",
  "cooling_jacket_full",
  "nozzle_profiled",
] as const;

function readExample(id: string): string {
  return readFileSync(
    fileURLToPath(new URL(`../../examples/${id}.topopt.json`, import.meta.url)),
    "utf-8",
  );
}

describe("M2 round-trip — the 6 repo examples", () => {
  for (const id of EXAMPLES) {
    it(`${id}: import → export → import is lossless (semantic deep-equal)`, () => {
      const text = readExample(id);
      const imported = specFromString(text);
      const reimported = specFromString(specToString(imported));
      expect(reimported).toEqual(imported);
      // and stable on a second lap (idempotent normalization)
      expect(specFromString(specToString(reimported))).toEqual(imported);
    });
  }

  it("cooling_jacket_full: preserved fields (drive, flow/thermal BCs, heaviside, 4 constraints)", () => {
    const s = specFromString(specToString(specFromString(readExample("cooling_jacket_full"))));
    expect(s.body_force).toEqual([0, 0, 30]);
    expect(s.flow).toHaveLength(6); // 5 selectors + the drive-entry residue
    expect(s.flow.map((e) => e.dof)).toEqual(["wall", "wall", "slip", "slip", "pressure", ""]);
    expect(s.flow[4]?.node).toBe("corner:x-,y-,z-");
    expect(s.thermal.map((e) => [e.face || e.region, e.dof, e.value])).toEqual([
      ["z-", "T", 0],
      ["z+", "T", 0],
      ["z:8:12", "Q", 3000],
    ]);
    expect(s.heaviside_beta).toEqual([1, 2, 4, 8, 16]);
    expect(s.constraints).toEqual([
      { type: "volume", max: 0.4, max_rel: 0 },
      { type: "tmax", max: 16.0, max_rel: 0 },
      { type: "dissipation", max: 4.0e4, max_rel: 0 },
      { type: "vonmises", max: 0.15, max_rel: 0 },
    ]);
    expect(s.brinkman_max).toBe(500.0);
    expect(s.loads[0]?.value).toBe(1.69);
  });

  it("nozzle_profiled: preserved fields (axi dim, nozzle geometry, pressure BC, max_rel)", () => {
    const s = specFromString(specToString(specFromString(readExample("nozzle_profiled"))));
    expect(s.dim).toBe("axi");
    expect(s.geometry).toBe("nozzle");
    expect(s.nozzle).toEqual({ r_throat: 0.6, K: 0.2, wall: 0.7 });
    expect(s.pressure).toEqual([
      { face: "inner", edge: "", node: "", region: "", dof: "", value: 4.0 },
    ]);
    expect(s.constraints).toEqual([{ type: "vonmises", max: 0, max_rel: 1.6 }]);
    expect(s.heaviside_beta).toEqual([1, 2, 3, 4]);
    expect(s.objective).toBe("mass");
  });

  it("cooling_jacket_full: change max_iter, re-export → ONLY that delta", () => {
    const text = readExample("cooling_jacket_full");
    const original = specFromString(text);
    const edited = specFromString(text);
    edited.max_iter = 200;

    // semantic: the edited export differs from the original spec by max_iter only
    expect(specFromString(specToString(edited))).toEqual({ ...original, max_iter: 200 });

    // structural: the emitted JSON trees differ at optimize.max_iter only
    const a = specToJson(original) as { optimize: Record<string, unknown> };
    const b = specToJson(edited) as { optimize: Record<string, unknown> };
    expect(a.optimize.max_iter).toBeUndefined(); // 60 = schema default, omitted
    expect(b.optimize.max_iter).toBe(200);
    delete a.optimize.max_iter;
    delete b.optimize.max_iter;
    expect(b).toEqual(a);
  });

  it("drive survives even when the flow list carries only the drive entry", () => {
    const s = specFromString(
      JSON.stringify({ physics: ["fluid"], bc: { flow: [{ drive: [1, 2, 3] }] } }),
    );
    expect(s.body_force).toEqual([1, 2, 3]);
    const back = specFromString(specToString(s));
    expect(back.body_force).toEqual([1, 2, 3]);
    expect(back.flow).toEqual(s.flow);
  });
});
