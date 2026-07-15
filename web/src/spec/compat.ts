// Compatibility matrix (WEB_MODELER_SPEC.md §4): physics (+dim) → admissible
// objectives and constraint types. Used three ways:
//  (a) UI selects only offer the admissible entries;
//  (b) import: deviations are listed as non-blocking warnings;
//  (c) export: deviations are blocking errors (e.g. vonmises without elastic).

import type { Dim, ProblemSpec } from "./ProblemSpec";

export const KNOWN_OBJECTIVES = ["compliance", "mass"] as const;
export const KNOWN_CONSTRAINTS = ["volume", "tmax", "dissipation", "vonmises"] as const;

// Hard physics prerequisites, independent of the matrix rows.
const CONSTRAINT_REQUIRES: Readonly<Record<string, string>> = {
  vonmises: "elastic",
  tmax: "thermal",
  dissipation: "fluid",
};
const OBJECTIVE_REQUIRES: Readonly<Record<string, string>> = {
  compliance: "elastic",
};

export interface CompatRule {
  physics: readonly string[]; // exact set (order-insensitive)
  dim: Dim;
  label: string;
  objectives: readonly string[];
  constraints: readonly string[];
}

// One row per line of the §4 table.
export const COMPAT_RULES: readonly CompatRule[] = [
  {
    physics: ["elastic"],
    dim: "3d",
    label: "elastic (3d)",
    objectives: ["compliance", "mass"],
    constraints: ["volume", "vonmises"],
  },
  {
    physics: ["thermal", "elastic"],
    dim: "3d",
    label: "thermal + elastic (3d)",
    objectives: ["mass"],
    constraints: ["volume", "vonmises"],
  },
  {
    physics: ["fluid", "thermal", "elastic"],
    dim: "3d",
    label: "fluid + thermal + elastic (3d)",
    objectives: ["compliance"],
    constraints: ["volume", "tmax", "dissipation", "vonmises"],
  },
  {
    physics: ["elastic"],
    dim: "axi",
    label: "elastic (axi, nozzle)",
    objectives: ["mass"],
    constraints: ["vonmises"], // bound via max_rel (relative to the solid design)
  },
];

function sameSet(a: readonly string[], b: readonly string[]): boolean {
  return a.length === b.length && a.every((x) => b.includes(x));
}

export function findRule(
  physics: readonly string[],
  dim: Dim,
): CompatRule | undefined {
  return COMPAT_RULES.find((r) => r.dim === dim && sameSet(r.physics, physics));
}

function hardAllowed(
  known: readonly string[],
  requires: Readonly<Record<string, string>>,
  physics: readonly string[],
): string[] {
  return known.filter((k) => {
    const req = requires[k];
    return req === undefined || physics.includes(req);
  });
}

// Admissible objectives for the current spec (matrix row, or the hard physics
// prerequisites when the combination is outside the matrix).
export function allowedObjectives(s: ProblemSpec): string[] {
  const rule = findRule(s.physics, s.dim);
  if (rule) return [...rule.objectives];
  return hardAllowed(KNOWN_OBJECTIVES, OBJECTIVE_REQUIRES, s.physics);
}

// Admissible constraint types for the current spec.
export function allowedConstraintTypes(s: ProblemSpec): string[] {
  const rule = findRule(s.physics, s.dim);
  if (rule) return [...rule.constraints];
  return hardAllowed(KNOWN_CONSTRAINTS, CONSTRAINT_REQUIRES, s.physics);
}

// All deviations of the spec from the matrix (empty = conforming). The same
// list is shown as warnings at import and as blocking errors at export.
export function checkCompat(s: ProblemSpec): string[] {
  const issues = new Set<string>();
  const rule = findRule(s.physics, s.dim);
  const label = rule?.label ?? `${s.physics.join(" + ")} (${s.dim})`;

  if (!rule)
    issues.add(
      `physics [${s.physics.join(", ")}] with dim "${s.dim}" is outside the compatibility matrix`,
    );

  // objective
  if (!(KNOWN_OBJECTIVES as readonly string[]).includes(s.objective)) {
    issues.add(`unknown objective "${s.objective}"`);
  } else {
    const reqO = OBJECTIVE_REQUIRES[s.objective];
    if (reqO !== undefined && !s.physics.includes(reqO))
      issues.add(`objective "${s.objective}" requires physics "${reqO}"`);
    else if (rule && !rule.objectives.includes(s.objective))
      issues.add(
        `objective "${s.objective}" is not admissible for ${label} (allowed: ${rule.objectives.join(", ")})`,
      );
  }

  // constraints
  for (const c of s.constraints) {
    if (!(KNOWN_CONSTRAINTS as readonly string[]).includes(c.type)) {
      issues.add(`unknown constraint type "${c.type}"`);
      continue;
    }
    const req = CONSTRAINT_REQUIRES[c.type];
    if (req !== undefined && !s.physics.includes(req))
      issues.add(`constraint "${c.type}" requires physics "${req}"`);
    else if (rule && !rule.constraints.includes(c.type))
      issues.add(
        `constraint "${c.type}" is not admissible for ${label} (allowed: ${rule.constraints.join(", ")})`,
      );
    if (c.max <= 0 && c.max_rel <= 0)
      issues.add(`constraint "${c.type}" needs max > 0 or max_rel > 0`);
    if (s.dim === "axi" && c.type === "vonmises" && c.max_rel <= 0)
      issues.add(
        `constraint "vonmises" in axi mode uses max_rel (bound relative to the solid design)`,
      );
  }

  return [...issues];
}
