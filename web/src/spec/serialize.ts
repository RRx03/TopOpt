// .topopt.json serialization. Emission policy: groups are always structured
// like the schema; scalar fields equal to the ProblemSpec defaults may be
// omitted (the C++ parser fills them back). specFromJson mirrors
// ProblemSpec::fromJson exactly (including the empty flow entry a drive-only
// object leaves behind), so `specFromJson(specToJson(s))` deep-equals `s` for
// any parsed/UI-built spec and golden comparisons are done on normalized specs.

import {
  defaultSpec,
  emptyBC,
  type BCEntry,
  type Dim,
  type ProblemSpec,
} from "./ProblemSpec";

type Json = Record<string, unknown>;

function bcToJson(e: BCEntry): Json {
  const o: Json = {};
  if (e.face) o.face = e.face;
  if (e.edge) o.edge = e.edge;
  if (e.node) o.node = e.node;
  if (e.region) o.region = e.region;
  if (e.dof) o.dof = e.dof;
  if (e.value !== 0) o.value = e.value;
  return o;
}

function sameArray(a: readonly unknown[], b: readonly unknown[]): boolean {
  return a.length === b.length && a.every((v, i) => v === b[i]);
}

export function specToJson(s: ProblemSpec): Json {
  const d = defaultSpec();
  const j: Json = {};

  const meta: Json = { name: s.name, version: s.version, dim: s.dim };
  j.meta = meta;

  const domain: Json = { grid: [...s.grid], size_mm: [...s.size_mm] };
  if (s.geometry !== d.geometry) domain.geometry = s.geometry;
  if (
    s.nozzle.r_throat !== d.nozzle.r_throat ||
    s.nozzle.K !== d.nozzle.K ||
    s.nozzle.wall !== d.nozzle.wall
  )
    domain.nozzle = { r_throat: s.nozzle.r_throat, K: s.nozzle.K, wall: s.nozzle.wall };
  j.domain = domain;

  const material: Json = {};
  const matKeys = [
    "E0",
    "Emin",
    "nu",
    "penal",
    "k_solid",
    "k_fluid",
    "alpha_th",
    "Tref",
    "mu",
    "brinkman_max",
    "brinkman_q",
  ] as const;
  for (const k of matKeys) if (s[k] !== d[k]) material[k] = s[k];
  if (Object.keys(material).length > 0) j.material = material;

  j.physics = [...s.physics];

  const bc: Json = {};
  if (s.fixed.length) bc.fixed = s.fixed.map(bcToJson);
  if (s.loads.length) bc.loads = s.loads.map(bcToJson);
  if (s.pressure.length) bc.pressure = s.pressure.map(bcToJson);
  if (s.thermal.length) bc.thermal = s.thermal.map(bcToJson);
  // The Stokes drive is emitted as its own trailing flow entry (the repo
  // convention); flow entries that serialize to {} (the parse residue of a
  // drive-only entry) are dropped so import → export is structurally stable.
  const hasDrive = s.body_force.some((v) => v !== 0);
  if (s.flow.length || hasDrive) {
    const flow = s.flow.map(bcToJson).filter((o) => Object.keys(o).length > 0);
    if (hasDrive) flow.push({ drive: [...s.body_force] });
    if (flow.length) bc.flow = flow;
  }
  if (Object.keys(bc).length > 0) j.bc = bc;

  const filter: Json = { radius_mm: s.filter_radius_mm };
  if (s.heaviside_beta.length > 0) {
    const heaviside: Json = { beta: [...s.heaviside_beta] };
    if (s.heaviside_eta !== d.heaviside_eta) heaviside.eta = s.heaviside_eta;
    filter.heaviside = heaviside;
  }
  j.filter = filter;

  const optimize: Json = { objective: s.objective };
  if (s.constraints.length > 0)
    optimize.constraints = s.constraints.map((c) => {
      const o: Json = { type: c.type };
      if (c.max_rel > 0) o.max_rel = c.max_rel;
      else o.max = c.max;
      return o;
    });
  if (s.optimizer !== d.optimizer) optimize.optimizer = s.optimizer;
  if (s.max_iter !== d.max_iter) optimize.max_iter = s.max_iter;
  if (s.penal_continuation !== d.penal_continuation)
    optimize.penal_continuation = s.penal_continuation;
  j.optimize = optimize;

  const output: Json = { dir: s.output_dir };
  if (!sameArray(s.formats, d.formats)) output.formats = [...s.formats];
  if (!sameArray(s.fields, d.fields)) output.fields = [...s.fields];
  if (s.stl_iso !== d.stl_iso) output.stl_iso = s.stl_iso;
  if (s.stl_method !== d.stl_method) output.stl_method = s.stl_method;
  j.output = output;

  return j;
}

export function specToString(s: ProblemSpec): string {
  return JSON.stringify(specToJson(s), null, 2) + "\n";
}

// --- parsing (mirror of ProblemSpec::fromJson) ---

function isObj(v: unknown): v is Json {
  return typeof v === "object" && v !== null && !Array.isArray(v);
}
function num(o: Json, k: string, dflt: number): number {
  const v = o[k];
  return typeof v === "number" ? v : dflt;
}
function str(o: Json, k: string, dflt: string): string {
  const v = o[k];
  return typeof v === "string" ? v : dflt;
}
function bool(o: Json, k: string, dflt: boolean): boolean {
  const v = o[k];
  return typeof v === "boolean" ? v : dflt;
}

function parseBCList(arr: unknown): BCEntry[] {
  if (!Array.isArray(arr)) return [];
  return arr.filter(isObj).map((e) => {
    const x = emptyBC();
    x.face = str(e, "face", "");
    x.edge = str(e, "edge", "");
    x.node = str(e, "node", "");
    x.region = str(e, "region", "");
    x.dof = str(e, "dof", "");
    if (typeof e.value === "number") x.value = e.value;
    else if (typeof e.T === "number") x.value = e.T;
    else if (typeof e.Q === "number") x.value = e.Q;
    return x;
  });
}

export function specFromJson(j: Json): ProblemSpec {
  const s = defaultSpec();
  if (isObj(j.meta)) {
    s.name = str(j.meta, "name", s.name);
    s.dim = str(j.meta, "dim", s.dim) as Dim;
    s.version = num(j.meta, "version", s.version);
  }
  if (isObj(j.domain)) {
    const d = j.domain;
    if (Array.isArray(d.grid)) s.grid = [d.grid[0], d.grid[1], d.grid[2]] as never;
    if (Array.isArray(d.size_mm))
      s.size_mm = [d.size_mm[0], d.size_mm[1], d.size_mm[2]] as never;
    s.geometry = str(d, "geometry", s.geometry);
    if (isObj(d.nozzle)) {
      s.nozzle.r_throat = num(d.nozzle, "r_throat", s.nozzle.r_throat);
      s.nozzle.K = num(d.nozzle, "K", s.nozzle.K);
      s.nozzle.wall = num(d.nozzle, "wall", s.nozzle.wall);
    }
  }
  if (isObj(j.material)) {
    const m = j.material;
    s.E0 = num(m, "E0", s.E0);
    s.Emin = num(m, "Emin", s.Emin);
    s.nu = num(m, "nu", s.nu);
    s.penal = num(m, "penal", s.penal);
    s.k_solid = num(m, "k_solid", s.k_solid);
    s.k_fluid = num(m, "k_fluid", s.k_fluid);
    s.alpha_th = num(m, "alpha_th", s.alpha_th);
    s.Tref = num(m, "Tref", s.Tref);
    s.mu = num(m, "mu", s.mu);
    s.brinkman_max = num(m, "brinkman_max", s.brinkman_max);
    s.brinkman_q = num(m, "brinkman_q", s.brinkman_q);
  }
  if (Array.isArray(j.physics)) s.physics = j.physics.map(String);
  if (isObj(j.bc)) {
    const b = j.bc;
    if (b.fixed !== undefined) s.fixed = parseBCList(b.fixed);
    if (b.loads !== undefined) s.loads = parseBCList(b.loads);
    if (b.pressure !== undefined) s.pressure = parseBCList(b.pressure);
    if (b.thermal !== undefined) s.thermal = parseBCList(b.thermal);
    if (b.flow !== undefined) {
      s.flow = parseBCList(b.flow);
      if (Array.isArray(b.flow))
        for (const e of b.flow)
          if (isObj(e) && Array.isArray(e.drive))
            s.body_force = [e.drive[0], e.drive[1], e.drive[2]] as never;
    }
  }
  if (isObj(j.filter)) {
    const f = j.filter;
    s.filter_radius_mm = num(f, "radius_mm", s.filter_radius_mm);
    if (isObj(f.heaviside)) {
      const h = f.heaviside;
      if (Array.isArray(h.beta)) s.heaviside_beta = h.beta.map(Number);
      s.heaviside_eta = num(h, "eta", s.heaviside_eta);
    }
  }
  if (isObj(j.optimize)) {
    const o = j.optimize;
    s.objective = str(o, "objective", s.objective);
    s.optimizer = str(o, "optimizer", s.optimizer);
    s.max_iter = num(o, "max_iter", s.max_iter);
    s.penal_continuation = bool(o, "penal_continuation", s.penal_continuation);
    if (Array.isArray(o.constraints))
      for (const c of o.constraints)
        if (isObj(c))
          s.constraints.push({
            type: str(c, "type", ""),
            max: num(c, "max", 0),
            max_rel: num(c, "max_rel", 0),
          });
  }
  if (isObj(j.output)) {
    const o = j.output;
    s.output_dir = str(o, "dir", s.output_dir);
    if (Array.isArray(o.formats)) s.formats = o.formats.map(String);
    if (Array.isArray(o.fields)) s.fields = o.fields.map(String);
    s.stl_iso = num(o, "stl_iso", s.stl_iso);
    s.stl_method = str(o, "stl_method", s.stl_method);
  }
  return s;
}

export function specFromString(text: string): ProblemSpec {
  const j: unknown = JSON.parse(text);
  if (!isObj(j)) throw new Error("ProblemSpec: root must be an object");
  return specFromJson(j);
}
