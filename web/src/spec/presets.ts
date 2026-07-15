// Presets = full ProblemSpec states (defaults come from defaultSpec, the
// exact mirror of ProblemSpec.hpp).

import { defaultSpec, emptyBC, type ProblemSpec } from "./ProblemSpec";

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

export const presets: Record<string, () => ProblemSpec> = {
  blank: blankPreset,
  mbb3d: mbb3dPreset,
};
