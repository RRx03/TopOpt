// M1 golden test: build the mbb3d state through the same internal API the UI
// uses (Store: face/edge selections + BC forms + panel fields), export it, and
// check the result is semantically identical to examples/mbb3d.topopt.json
// (both sides parsed with the mirror of ProblemSpec::fromJson, deep-equal —
// defaults included).

import { readFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { describe, expect, it } from "vitest";

import { defaultSpec } from "../src/spec/ProblemSpec";
import { specFromString, specToString } from "../src/spec/serialize";
import { mbb3dPreset } from "../src/spec/presets";
import { Store } from "../src/state";

const goldenPath = fileURLToPath(
  new URL("../../examples/mbb3d.topopt.json", import.meta.url),
);
const goldenText = readFileSync(goldenPath, "utf-8");

function buildMbb3dViaUIActions(): Store {
  const store = new Store(defaultSpec());
  const s = store.spec;

  // Panels: meta / domain / optimize / output.
  s.name = "mbb3d";
  s.grid = [60, 20, 20];
  s.size_mm = [60, 20, 20];
  s.constraints.push({ type: "volume", max: 0.3, max_rel: 0 });
  s.optimizer = "oc";
  s.max_iter = 60;
  s.output_dir = "output/mbb3d";
  s.formats = ["vti", "stl"];
  s.fields = ["density"];

  // Mouse: face / edge selections + BC forms, exactly the M1 interactions.
  store.selectFace("x-", false);
  store.addFixed(store.selection!, "x"); // { face:"x-", dof:"x" }

  store.selectFace("z-", false);
  store.addFixed(store.selection!, "z"); // { face:"z-", dof:"z" }

  store.selectFace("x+", false);
  store.selectFace("y-", true); // shift+click adjacent face -> edge
  store.addFixed(store.selection!, "y"); // { edge:"x+,y-", dof:"y" }

  store.selectFace("y+", false);
  store.selectFace("x-", true); // canonical order -> "x-,y+"
  store.addLoad(store.selection!, "y", -1.0); // { edge:"x-,y+", dof:"y", value:-1 }

  return store;
}

describe("M1 golden — mbb3d", () => {
  it("export built via the UI API is semantically identical to examples/mbb3d.topopt.json", () => {
    const store = buildMbb3dViaUIActions();
    const exported = specToString(store.spec);
    expect(specFromString(exported)).toEqual(specFromString(goldenText));
  });

  it("edge selectors match the golden file exactly (canonical face order)", () => {
    const store = buildMbb3dViaUIActions();
    expect(store.spec.fixed.map((e) => e.face || e.edge)).toEqual(["x-", "z-", "x+,y-"]);
    expect(store.spec.loads[0]?.edge).toBe("x-,y+");
  });

  it("mbb3d preset matches the golden file too", () => {
    expect(specFromString(specToString(mbb3dPreset()))).toEqual(
      specFromString(goldenText),
    );
  });

  it("serialization round-trips without loss", () => {
    const spec = buildMbb3dViaUIActions().spec;
    expect(specFromString(specToString(spec))).toEqual(spec);
  });

  it("defaults mirror ProblemSpec.hpp (spot checks)", () => {
    const d = defaultSpec();
    expect(d.Emin).toBe(1e-4);
    expect(d.filter_radius_mm).toBe(1.5);
    expect(d.physics).toEqual(["elastic"]);
    expect(d.objective).toBe("compliance");
    expect(d.optimizer).toBe("mma");
    expect(d.max_iter).toBe(60);
    expect(d.formats).toEqual(["vti"]);
    expect(d.fields).toEqual(["density"]);
    expect(d.output_dir).toBe("output");
    expect(d.nozzle).toEqual({ r_throat: 0.6, K: 0.2, wall: 0.7 });
  });
});
