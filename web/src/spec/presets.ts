// Presets = full ProblemSpec states (defaults come from defaultSpec, the
// exact mirror of ProblemSpec.hpp).
//
// The 6 repo examples are embedded at build time as static raw-JSON imports
// (no fetch); web/src/presets/*.topopt.json are verbatim copies of examples/
// and a test asserts byte-identity with the repo files (source of truth).

import { defaultSpec, emptyBC, type ProblemSpec } from "./ProblemSpec";
import { specFromString } from "./serialize";

import mbb3dRaw from "../presets/mbb3d.topopt.json?raw";
import bracketThermoRaw from "../presets/bracket_thermo.topopt.json?raw";
import coolingJacketRaw from "../presets/cooling_jacket.topopt.json?raw";
import coolingJacketMultiRaw from "../presets/cooling_jacket_multi.topopt.json?raw";
import coolingJacketFullRaw from "../presets/cooling_jacket_full.topopt.json?raw";
import nozzleProfiledRaw from "../presets/nozzle_profiled.topopt.json?raw";

// Blank elastic box with a workable resolution (pure schema defaults except
// the domain, which defaults to a degenerate 1x1x1 grid in the C++ struct).
export function blankPreset(): ProblemSpec {
  const s = defaultSpec();
  s.grid = [40, 20, 20];
  s.size_mm = [40, 20, 20];
  s.constraints = [{ type: "volume", max: 0.5, max_rel: 0 }];
  return s;
}

// State matching examples/mbb3d.topopt.json (golden reference of milestone M1).
export function mbb3dPreset(): ProblemSpec {
  const s = defaultSpec();
  s.name = "mbb3d";
  s.grid = [60, 20, 20];
  s.size_mm = [60, 20, 20];
  s.fixed = [
    { ...emptyBC(), face: "x-", dof: "x" },
    { ...emptyBC(), face: "z-", dof: "z" },
    { ...emptyBC(), edge: "x+,y-", dof: "y" },
  ];
  s.loads = [{ ...emptyBC(), edge: "x-,y+", dof: "y", value: -1.0 }];
  s.constraints = [{ type: "volume", max: 0.3, max_rel: 0 }];
  s.optimizer = "oc";
  s.max_iter = 60;
  s.output_dir = "output/mbb3d";
  s.formats = ["vti", "stl"];
  s.fields = ["density"];
  return s;
}

// --- gallery: the 6 repo examples --------------------------------------------

export interface ExamplePreset {
  id: string;
  file: string; // filename under examples/ (and web/src/presets/)
  physics: string; // human label, derived from the embedded spec
  description: string;
  raw: string; // exact repo file content (embedded at build)
  make(): ProblemSpec;
}

function physicsLabel(raw: string): string {
  const s = specFromString(raw);
  return `${s.physics.join(" + ")} (${s.dim})`;
}

function preset(id: string, raw: string, description: string): ExamplePreset {
  return {
    id,
    file: `${id}.topopt.json`,
    physics: physicsLabel(raw),
    description,
    raw,
    make: () => specFromString(raw),
  };
}

export const examplePresets: readonly ExamplePreset[] = [
  preset(
    "mbb3d",
    mbb3dRaw,
    "MBB beam: compliance min under a 30% volume cap (M1 golden).",
  ),
  preset(
    "bracket_thermo",
    bracketThermoRaw,
    "Thermo-elastic bracket: mass min under a von Mises cap, tip heat flux.",
  ),
  preset(
    "cooling_jacket",
    coolingJacketRaw,
    "Fluid-cooled jacket: compliance min, volume constraint only.",
  ),
  preset(
    "cooling_jacket_multi",
    coolingJacketMultiRaw,
    "Cooling jacket with volume + tmax + dissipation constraints (all active).",
  ),
  preset(
    "cooling_jacket_full",
    coolingJacketFullRaw,
    "Cooling jacket with four active constraints, incl. von Mises stress.",
  ),
  preset(
    "nozzle_profiled",
    nozzleProfiledRaw,
    "Axisymmetric nozzle wall: mass min, relative von Mises bound (max_rel).",
  ),
];
