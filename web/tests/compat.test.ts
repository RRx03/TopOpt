// M2 compatibility matrix (WEB_MODELER_SPEC.md §4): admissible objectives and
// constraint types per physics(+dim), accepted and rejected cases.

import { describe, expect, it } from "vitest";

import {
  allowedConstraintTypes,
  allowedObjectives,
  checkCompat,
  findRule,
} from "../src/spec/compat";
import { defaultSpec, type ProblemSpec } from "../src/spec/ProblemSpec";

function spec(edit: (s: ProblemSpec) => void): ProblemSpec {
  const s = defaultSpec();
  edit(s);
  return s;
}

describe("compatibility matrix — rules", () => {
  it("elastic (3d): compliance/mass, volume/vonmises", () => {
    const s = spec(() => {});
    expect(allowedObjectives(s)).toEqual(["compliance", "mass"]);
    expect(allowedConstraintTypes(s)).toEqual(["volume", "vonmises"]);
  });

  it("thermal + elastic (3d): mass only, volume/vonmises", () => {
    const s = spec((x) => (x.physics = ["thermal", "elastic"]));
    expect(allowedObjectives(s)).toEqual(["mass"]);
    expect(allowedConstraintTypes(s)).toEqual(["volume", "vonmises"]);
  });

  it("fluid + thermal + elastic (3d): compliance only, all four constraint types", () => {
    const s = spec((x) => (x.physics = ["fluid", "thermal", "elastic"]));
    expect(allowedObjectives(s)).toEqual(["compliance"]);
    expect(allowedConstraintTypes(s)).toEqual(["volume", "tmax", "dissipation", "vonmises"]);
  });

  it("elastic (axi): mass only, vonmises only", () => {
    const s = spec((x) => (x.dim = "axi"));
    expect(allowedObjectives(s)).toEqual(["mass"]);
    expect(allowedConstraintTypes(s)).toEqual(["vonmises"]);
  });

  it("physics order does not matter for rule lookup", () => {
    expect(findRule(["elastic", "thermal"], "3d")).toBe(findRule(["thermal", "elastic"], "3d"));
    expect(findRule(["elastic", "thermal"], "3d")).toBeDefined();
  });

  it("unknown combination: falls back to the hard physics prerequisites", () => {
    const s = spec((x) => (x.physics = ["thermal"]));
    expect(allowedObjectives(s)).toEqual(["mass"]); // compliance needs elastic
    expect(allowedConstraintTypes(s)).toEqual(["volume", "tmax"]);
  });
});

describe("compatibility matrix — accepted cases", () => {
  it("mbb3d-like: elastic + compliance + volume", () => {
    const s = spec((x) => {
      x.constraints = [{ type: "volume", max: 0.3, max_rel: 0 }];
    });
    expect(checkCompat(s)).toEqual([]);
  });

  it("thermo-elastic mass-min with a vonmises cap", () => {
    const s = spec((x) => {
      x.physics = ["thermal", "elastic"];
      x.objective = "mass";
      x.constraints = [{ type: "vonmises", max: 0.12, max_rel: 0 }];
    });
    expect(checkCompat(s)).toEqual([]);
  });

  it("v3 combined constraints (volume + tmax + dissipation + vonmises)", () => {
    const s = spec((x) => {
      x.physics = ["fluid", "thermal", "elastic"];
      x.objective = "compliance";
      x.constraints = [
        { type: "volume", max: 0.4, max_rel: 0 },
        { type: "tmax", max: 16, max_rel: 0 },
        { type: "dissipation", max: 4e4, max_rel: 0 },
        { type: "vonmises", max: 0.15, max_rel: 0 },
      ];
    });
    expect(checkCompat(s)).toEqual([]);
  });

  it("axi nozzle: mass + vonmises via max_rel", () => {
    const s = spec((x) => {
      x.dim = "axi";
      x.geometry = "nozzle";
      x.objective = "mass";
      x.constraints = [{ type: "vonmises", max: 0, max_rel: 1.6 }];
    });
    expect(checkCompat(s)).toEqual([]);
  });
});

describe("compatibility matrix — rejected cases", () => {
  it("vonmises without elastic physics (the canonical export error)", () => {
    const s = spec((x) => {
      x.physics = ["fluid", "thermal"];
      x.objective = "mass";
      x.constraints = [{ type: "vonmises", max: 0.2, max_rel: 0 }];
    });
    const issues = checkCompat(s);
    expect(issues.some((m) => m.includes('"vonmises"') && m.includes('"elastic"'))).toBe(true);
  });

  it("tmax on a pure elastic problem", () => {
    const s = spec((x) => {
      x.constraints = [{ type: "tmax", max: 10, max_rel: 0 }];
    });
    const issues = checkCompat(s);
    expect(issues.some((m) => m.includes('"tmax"') && m.includes('"thermal"'))).toBe(true);
  });

  it("dissipation without fluid physics", () => {
    const s = spec((x) => {
      x.physics = ["thermal", "elastic"];
      x.objective = "mass";
      x.constraints = [{ type: "dissipation", max: 1e4, max_rel: 0 }];
    });
    const issues = checkCompat(s);
    expect(issues.some((m) => m.includes('"dissipation"') && m.includes('"fluid"'))).toBe(true);
  });

  it("objective compliance is not admissible for thermal + elastic", () => {
    const s = spec((x) => {
      x.physics = ["thermal", "elastic"];
      x.objective = "compliance";
    });
    const issues = checkCompat(s);
    expect(issues.some((m) => m.includes('objective "compliance"'))).toBe(true);
  });

  it("objective mass is not admissible for the fluid-thermal-elastic row", () => {
    const s = spec((x) => {
      x.physics = ["fluid", "thermal", "elastic"];
      x.objective = "mass";
    });
    expect(checkCompat(s).some((m) => m.includes('objective "mass"'))).toBe(true);
  });

  it("vonmises with an absolute max in axi mode (matrix wants max_rel)", () => {
    const s = spec((x) => {
      x.dim = "axi";
      x.objective = "mass";
      x.constraints = [{ type: "vonmises", max: 0.2, max_rel: 0 }];
    });
    expect(checkCompat(s).some((m) => m.includes("max_rel"))).toBe(true);
  });

  it("unknown physics combination is flagged", () => {
    const s = spec((x) => {
      x.physics = ["fluid"];
    });
    expect(checkCompat(s).some((m) => m.includes("outside the compatibility matrix"))).toBe(
      true,
    );
  });

  it("constraint without a positive bound is flagged", () => {
    const s = spec((x) => {
      x.constraints = [{ type: "volume", max: 0, max_rel: 0 }];
    });
    expect(checkCompat(s).some((m) => m.includes("max > 0 or max_rel > 0"))).toBe(true);
  });

  it("unknown objective / constraint types are flagged", () => {
    const s = spec((x) => {
      x.objective = "stiffness";
      x.constraints = [{ type: "buckling", max: 1, max_rel: 0 }];
    });
    const issues = checkCompat(s);
    expect(issues.some((m) => m.includes('unknown objective "stiffness"'))).toBe(true);
    expect(issues.some((m) => m.includes('unknown constraint type "buckling"'))).toBe(true);
  });
});
