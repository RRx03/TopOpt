// M2 presets: the gallery embeds the 6 repo examples at build time
// (web/src/presets/*.topopt.json, static raw imports). Source of truth is
// examples/ — each embedded preset must be byte-identical to the repo file,
// parse to the same spec, and pass the compatibility matrix cleanly.

import { readFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { describe, expect, it } from "vitest";

import { checkCompat } from "../src/spec/compat";
import { examplePresets } from "../src/spec/presets";
import { specFromString } from "../src/spec/serialize";

function repoFile(file: string): string {
  return readFileSync(
    fileURLToPath(new URL(`../../examples/${file}`, import.meta.url)),
    "utf-8",
  );
}

describe("M2 presets — gallery = the repo examples", () => {
  it("embeds exactly the 6 repo examples", () => {
    expect(examplePresets.map((p) => p.file).sort()).toEqual([
      "bracket_thermo.topopt.json",
      "cooling_jacket.topopt.json",
      "cooling_jacket_full.topopt.json",
      "cooling_jacket_multi.topopt.json",
      "mbb3d.topopt.json",
      "nozzle_profiled.topopt.json",
    ]);
  });

  for (const p of examplePresets) {
    it(`${p.id}: embedded copy is byte-identical to examples/${p.file}`, () => {
      expect(p.raw).toBe(repoFile(p.file));
    });

    it(`${p.id}: make() parses to the same spec as the repo file`, () => {
      expect(p.make()).toEqual(specFromString(repoFile(p.file)));
    });

    it(`${p.id}: conforms to the compatibility matrix (no warnings)`, () => {
      expect(checkCompat(p.make())).toEqual([]);
    });

    it(`${p.id}: gallery metadata is filled`, () => {
      expect(p.physics.length).toBeGreaterThan(0);
      expect(p.description.length).toBeGreaterThan(0);
    });
  }
});
