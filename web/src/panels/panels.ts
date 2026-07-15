// lil-gui property panels: meta, domain, material, filter, optimize, output.
// M1 exposes the elastic physics only (physics = ["elastic"], not editable).

import GUI from "lil-gui";
import type { Store } from "../state";
import { presets } from "../spec/presets";

function parseBetaList(text: string): number[] {
  return text
    .split(",")
    .map((t) => Number(t.trim()))
    .filter((v) => Number.isFinite(v) && v > 0);
}

export function setupPanels(container: HTMLElement, store: Store): void {
  const gui = new GUI({ container, title: "TopOpt Studio — problem" });

  // Mutable view bound to the GUI; refreshed from the spec on store changes.
  const p = {
    preset: "mbb3d",
    name: "",
    nelx: 1,
    nely: 1,
    nelz: 1,
    Lx: 1,
    Ly: 1,
    Lz: 1,
    E0: 1,
    Emin: 1e-4,
    nu: 0.3,
    penal: 3,
    radius_mm: 1.5,
    beta: "",
    eta: 0.5,
    objective: "compliance",
    volumeMax: 0.3,
    optimizer: "mma",
    max_iter: 60,
    dir: "output",
  };

  const pull = (): void => {
    const s = store.spec;
    p.name = s.name;
    [p.nelx, p.nely, p.nelz] = s.grid;
    [p.Lx, p.Ly, p.Lz] = s.size_mm;
    p.E0 = s.E0;
    p.Emin = s.Emin;
    p.nu = s.nu;
    p.penal = s.penal;
    p.radius_mm = s.filter_radius_mm;
    p.beta = s.heaviside_beta.join(", ");
    p.eta = s.heaviside_eta;
    p.objective = s.objective;
    p.volumeMax = s.constraints.find((c) => c.type === "volume")?.max ?? 0;
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
    s.objective = p.objective;
    const vol = s.constraints.find((c) => c.type === "volume");
    if (p.volumeMax > 0) {
      if (vol) vol.max = p.volumeMax;
      else s.constraints.push({ type: "volume", max: p.volumeMax, max_rel: 0 });
    } else {
      s.constraints = s.constraints.filter((c) => c.type !== "volume");
    }
    s.optimizer = p.optimizer;
    s.max_iter = Math.max(1, Math.round(p.max_iter));
    s.output_dir = p.dir;
    store.emit();
  };

  gui.add(p, "preset", Object.keys(presets)).name("preset").onChange((k: string) => {
    store.loadSpec(presets[k]!());
  });

  const meta = gui.addFolder("meta");
  meta.add(p, "name").onFinishChange(push);

  const domain = gui.addFolder("domain");
  domain.add(p, "nelx", 1, 256, 1).name("grid nelx").onFinishChange(push);
  domain.add(p, "nely", 1, 256, 1).name("grid nely").onFinishChange(push);
  domain.add(p, "nelz", 1, 256, 1).name("grid nelz").onFinishChange(push);
  domain.add(p, "Lx").name("size X (mm)").onFinishChange(push);
  domain.add(p, "Ly").name("size Y (mm)").onFinishChange(push);
  domain.add(p, "Lz").name("size Z (mm)").onFinishChange(push);

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

  const optimize = gui.addFolder("optimize");
  optimize.add(p, "objective", ["compliance", "mass"]).onFinishChange(push);
  optimize.add(p, "volumeMax", 0, 1).name("volume max (0 = off)").onFinishChange(push);
  optimize.add(p, "optimizer", ["mma", "oc"]).onFinishChange(push);
  optimize.add(p, "max_iter", 1, 1000, 1).onFinishChange(push);

  const output = gui.addFolder("output");
  output.add(p, "dir").name("output dir").onFinishChange(push);
  output.close();

  store.on(() => {
    pull();
    for (const c of gui.controllersRecursive()) c.updateDisplay();
  });
}
