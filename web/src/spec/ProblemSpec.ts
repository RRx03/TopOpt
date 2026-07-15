// TypeScript mirror of src/io/ProblemSpec.hpp — the .topopt.json contract.
// Fields and defaults MUST stay identical to the C++ struct (source of truth).

export type Dim = "3d" | "axi";
export type Vec3 = [number, number, number];
export type IVec3 = [number, number, number];

// One selector is set (face | edge | node | region); the others stay "".
export interface BCEntry {
  face: string;
  edge: string;
  node: string;
  region: string;
  dof: string; // "x" | "y" | "z" | "all" | "T"
  value: number; // load / pressure / Q / T
}

export interface Constraint {
  type: string;
  max: number;
  // max_rel > 0 takes precedence over max (bound relative to the solid design).
  max_rel: number;
}

export interface NozzleParams {
  r_throat: number;
  K: number;
  wall: number;
}

export interface ProblemSpec {
  // meta
  name: string;
  dim: Dim;
  version: number;

  // domain
  grid: IVec3;
  size_mm: Vec3;
  geometry: string; // "box" | "nozzle"
  nozzle: NozzleParams;

  // material
  E0: number;
  Emin: number;
  nu: number;
  penal: number;
  k_solid: number;
  k_fluid: number;
  alpha_th: number;
  Tref: number;
  mu: number;
  brinkman_max: number;
  brinkman_q: number;

  // physics
  physics: string[];

  // boundary conditions (raw selectors, resolved by the C++ BCResolver)
  fixed: BCEntry[];
  loads: BCEntry[];
  pressure: BCEntry[];
  thermal: BCEntry[];
  flow: BCEntry[];
  body_force: Vec3; // Stokes drive (bc.flow "drive")

  // filter
  filter_radius_mm: number;
  heaviside_beta: number[]; // empty = no projection
  heaviside_eta: number;

  // optimize
  objective: string; // "compliance" | "mass" | ...
  constraints: Constraint[];
  optimizer: string; // "mma" | "oc"
  max_iter: number;
  penal_continuation: boolean;

  // output
  output_dir: string;
  formats: string[];
  fields: string[];
  stl_iso: number;
  stl_method: string; // "marching_cubes" | "voxel"
}

// Exact defaults of ProblemSpec.hpp.
export function defaultSpec(): ProblemSpec {
  return {
    name: "problem",
    dim: "3d",
    version: 1,

    grid: [1, 1, 1],
    size_mm: [1, 1, 1],
    geometry: "box",
    nozzle: { r_throat: 0.6, K: 0.2, wall: 0.7 },

    E0: 1.0,
    Emin: 1e-4,
    nu: 0.3,
    penal: 3.0,
    k_solid: 1.0,
    k_fluid: 0.3,
    alpha_th: 1e-3,
    Tref: 0.0,
    mu: 1.0,
    brinkman_max: 1e2,
    brinkman_q: 0.1,

    physics: ["elastic"],

    fixed: [],
    loads: [],
    pressure: [],
    thermal: [],
    flow: [],
    body_force: [0, 0, 0],

    filter_radius_mm: 1.5,
    heaviside_beta: [],
    heaviside_eta: 0.5,

    objective: "compliance",
    constraints: [],
    optimizer: "mma",
    max_iter: 60,
    penal_continuation: false,

    output_dir: "output",
    formats: ["vti"],
    fields: ["density"],
    stl_iso: 0.5,
    stl_method: "marching_cubes",
  };
}

export function emptyBC(): BCEntry {
  return { face: "", edge: "", node: "", region: "", dof: "", value: 0 };
}

// Canonical edge selector "a,b": faces ordered by axis (x < y < z), matching
// the contract examples ("x-,y+", "x+,y-").
export function canonicalEdge(a: string, b: string): string {
  const [fa, fb] = a[0]! <= b[0]! ? [a, b] : [b, a];
  return `${fa},${fb}`;
}
