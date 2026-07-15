// lil-gui property panels: meta, domain, material, filter, optimize, output.
// M2: the optimize folder is generalized — typed constraint list (volume,
// tmax, dissipation, vonmises with max/max_rel) and objective select, both
// filtered by the compatibility matrix. Physics/dim stay read-only (V2 will
// make them editable together with the thermal/flow BC editors).

import GUI from "lil-gui";
import type { Store } from "../state";
import { allowedConstraintTypes, allowedObjectives } from "../spec/compat";

function parseBetaList(text: string): number[] {
  return text
    .split(",")
    .map((t) => Number(t.trim()))
    .filter((v) => Number.isFinite(v) && v > 0);
}

// Sensible starting bound when a constraint is added from the UI.
const DEFAULT_MAX: Readonly<Record<string, number>> = {
  volume: 0.5,
  tmax: 10.0,
  dissipation: 1e4,
  vonmises: 0.2,
};

export function setupPanels(container: HTMLElement, store: Store): void {
  const gui = new GUI({ container, title: "TopOpt Studio — problem" });

  // Mutable view bound to the GUI; refreshed from the spec on store changes.
  const p = {
    name: "",
    physics: "",
    nelx: 1,
    nely: 1,
    nelz: 1,
    Lx: 1,
    Ly: 1,
    Lz: 1,
    geometry: "",
    E0: 1,
    Emin: 1e-4,
    nu: 0.3,
    penal: 3,
    radius_mm: 1.5,
    beta: "",
    eta: 0.5,
    optimizer: "mma",
    max_iter: 60,
    dir: "output",
  };

  const pull = (): void => {
    const s = store.spec;
    p.name = s.name;
    p.physics = `${s.physics.join(" + ")} (${s.dim})`;
    [p.nelx, p.nely, p.nelz] = s.grid;
    [p.Lx, p.Ly, p.Lz] = s.size_mm;
    p.geometry =
      s.geometry === "nozzle"
        ? `nozzle · r_throat ${s.nozzle.r_throat} · K ${s.nozzle.K} · wall ${s.nozzle.wall}`
        : s.geometry;
    p.E0 = s.E0;
    p.Emin = s.Emin;
    p.nu = s.nu;
    p.penal = s.penal;
    p.radius_mm = s.filter_radius_mm;
    p.beta = s.heaviside_beta.join(", ");
    p.eta = s.heaviside_eta;
    p.optimizer = s.optimizer;
    p.max_iter = s.max_iter;
    p.dir = s.output_dir;
  };
  pull();

  const push = (): void => {
    const s = store.spec;
    s.name = p.name;
    s.grid = [Math.max(1, Math.round(p.nelx)), Math.max(1, Math.round(p.nely)), Math.max(1, Math.round(p.nelz))];
    s.size_mm = [p.Lx, p.Ly, p.Lz];
    s.E0 = p.E0;
    s.Emin = p.Emin;
    s.nu = p.nu;
    s.penal = p.penal;
    s.filter_radius_mm = p.radius_mm;
    s.heaviside_beta = parseBetaList(p.beta);
    s.heaviside_eta = p.eta;
    s.optimizer = p.optimizer;
    s.max_iter = Math.max(1, Math.round(p.max_iter));
    s.output_dir = p.dir;
    store.emit();
  };

  const meta = gui.addFolder("meta");
  meta.add(p, "name").onFinishChange(push);
  meta.add(p, "physics").name("physics (V2)").disable();

  const domain = gui.addFolder("domain");
  domain.add(p, "nelx", 1, 256, 1).name("grid nelx").onFinishChange(push);
  domain.add(p, "nely", 1, 256, 1).name("grid nely").onFinishChange(push);
  domain.add(p, "nelz", 1, 256, 1).name("grid nelz").onFinishChange(push);
  domain.add(p, "Lx").name("size X (mm)").onFinishChange(push);
  domain.add(p, "Ly").name("size Y (mm)").onFinishChange(push);
  domain.add(p, "Lz").name("size Z (mm)").onFinishChange(push);
  domain.add(p, "geometry").name("geometry (V2)").disable();

  const material = gui.addFolder("material");
  material.add(p, "E0").onFinishChange(push);
  material.add(p, "Emin").onFinishChange(push);
  material.add(p, "nu", 0, 0.499).onFinishChange(push);
  material.add(p, "penal", 1, 6).onFinishChange(push);
  material.close();

  const filter = gui.addFolder("filter");
  filter.add(p, "radius_mm").name("radius (mm)").onFinishChange(push);
  filter.add(p, "beta").name("heaviside β (list)").onFinishChange(push);
  filter.add(p, "eta", 0, 1).name("heaviside η").onFinishChange(push);
  filter.close();

  // --- optimize: rebuilt from the spec on every change (dynamic constraint
  // list). Children are destroyed/recreated in place so the folder keeps its
  // open/closed state.
  const optimize = gui.addFolder("optimize");

  const rebuildOptimize = (): void => {
    const s = store.spec;
    for (const child of [...optimize.children]) child.destroy();

    // objective — only the admissible entries (plus the current value if an
    // imported spec deviates, so it stays visible instead of silently moving).
    const objectives = allowedObjectives(s);
    if (!objectives.includes(s.objective)) objectives.push(s.objective);
    const ov = { objective: s.objective };
    optimize.add(ov, "objective", objectives).onFinishChange((v: string) => {
      s.objective = v;
      store.emit();
    });

    // typed constraints
    s.constraints.forEach((c, i) => {
      const rel = c.max_rel > 0;
      const view = {
        bound: rel ? c.max_rel : c.max,
        rel,
        remove: () => {
          s.constraints.splice(i, 1);
          store.emit();
        },
      };
      optimize
        .add(view, "bound")
        .name(`${c.type} · ${rel ? "max_rel" : "max"}`)
        .onFinishChange((v: number) => {
          if (rel) c.max_rel = v;
          else c.max = v;
          store.emit();
        });
      if (c.type === "vonmises")
        optimize
          .add(view, "rel")
          .name("vonmises: relative bound")
          .onFinishChange((v: boolean) => {
            const bound = rel ? c.max_rel : c.max;
            c.max = v ? 0 : bound;
            c.max_rel = v ? bound : 0;
            store.emit();
          });
      optimize.add(view, "remove").name(`✕ remove ${c.type}`);
    });

    // add — types admissible for the current physics and not already present
    const addable = allowedConstraintTypes(s).filter(
      (t) => !s.constraints.some((c) => c.type === t),
    );
    if (addable.length > 0) {
      const av = {
        type: addable[0]!,
        add: () => {
          const relByDefault = s.dim === "axi" && av.type === "vonmises";
          s.constraints.push({
            type: av.type,
            max: relByDefault ? 0 : (DEFAULT_MAX[av.type] ?? 1),
            max_rel: relByDefault ? 1.5 : 0,
          });
          store.emit();
        },
      };
      optimize.add(av, "type", addable).name("new constraint");
      optimize.add(av, "add").name("+ add constraint");
    }

    optimize.add(p, "optimizer", ["mma", "oc"]).onFinishChange(push);
    optimize.add(p, "max_iter", 1, 1000, 1).onFinishChange(push);
  };
  rebuildOptimize();

  const output = gui.addFolder("output");
  output.add(p, "dir").name("output dir").onFinishChange(push);
  output.close();

  store.on(() => {
    pull();
    rebuildOptimize();
    for (const c of gui.controllersRecursive()) c.updateDisplay();
  });
}
