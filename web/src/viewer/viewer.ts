// Results mode: load a .vti (solver output) or .stl and render it with vtk.js.
// - .vti : field list, iso-surface (threshold slider), orthogonal X/Y/Z slices
//   with position sliders, viridis/coolwarm colormaps + min/max scalar bar.
//   Default combo on load = the M3 acceptance case: density iso-surface at 0.5
//   + mid-height slice colored by temperature (cooling jacket: channels + T).
// - .stl : shaded geometry, orbit.
// All vtk.js access goes through ./vtk (lazy chunk); parsing is framework-free.

import { COLORMAP_NAMES, drawColormap, type ColormapName } from "./colormaps";
import { parseStl, type StlMesh } from "./stlParser";
import { cellToPoint, parseVti, type VtiData, type VtiField } from "./vtiParser";
import {
  createViewerContext,
  makeColorFunction,
  makeIsoPipeline,
  makeMeshActor,
  makeScalarImage,
  makeSlicePipeline,
  type IsoPipeline,
  type ScalarImage,
  type SlicePipeline,
  type ViewerContext,
} from "./vtk";

const AXES = ["X", "Y", "Z"] as const;
const ISO_COLOR: readonly [number, number, number] = [0.42, 0.7, 1.0]; // accent blue

function el<K extends keyof HTMLElementTagNameMap>(
  tag: K,
  cls?: string,
  text?: string,
): HTMLElementTagNameMap[K] {
  const e = document.createElement(tag);
  if (cls) e.className = cls;
  if (text !== undefined) e.textContent = text;
  return e;
}

function fmt(v: number): string {
  if (v === 0) return "0";
  return Number(v.toPrecision(3)).toString();
}

interface SliceUI {
  enabled: HTMLInputElement;
  pos: HTMLInputElement;
  label: HTMLSpanElement;
}

export class ResultsViewer {
  private ctx: ViewerContext;

  // data
  private vti: VtiData | null = null;
  private images = new Map<string, ScalarImage>();

  // pipelines (created once, fed with the image of the selected field)
  private iso: IsoPipeline;
  private slices: [SlicePipeline, SlicePipeline, SlicePipeline];
  private meshActor: ReturnType<typeof makeMeshActor> | null = null;

  // controls
  private status: HTMLDivElement;
  private info: HTMLDivElement;
  private vtiControls: HTMLDivElement;
  private fieldSel: HTMLSelectElement;
  private cmapSel: HTMLSelectElement;
  private isoOn: HTMLInputElement;
  private isoFieldSel: HTMLSelectElement;
  private isoRange: HTMLInputElement;
  private isoLabel: HTMLSpanElement;
  private sliceUIs: SliceUI[] = [];

  // legend overlay
  private legend: HTMLDivElement;
  private legendCanvas: HTMLCanvasElement;
  private legendMin: HTMLSpanElement;
  private legendMax: HTMLSpanElement;
  private legendField: HTMLSpanElement;

  constructor(viewport: HTMLElement, sidebar: HTMLElement) {
    this.ctx = createViewerContext(viewport);
    this.iso = makeIsoPipeline(ISO_COLOR);
    this.slices = [makeSlicePipeline(0), makeSlicePipeline(1), makeSlicePipeline(2)];

    // --- legend overlay (in the 3D viewport) --------------------------------
    this.legend = el("div", "scalar-bar");
    this.legendField = el("span", "scalar-bar-field");
    this.legendCanvas = el("canvas");
    this.legendCanvas.width = 160;
    this.legendCanvas.height = 10;
    this.legendMin = el("span", "scalar-bar-val");
    this.legendMax = el("span", "scalar-bar-val");
    this.legend.append(this.legendField, this.legendMin, this.legendCanvas, this.legendMax);
    this.legend.style.display = "none";
    viewport.appendChild(this.legend);

    // --- sidebar controls -----------------------------------------------------
    sidebar.appendChild(el("h2", undefined, "Results"));

    const row = el("div", "bc-row");
    const input = el("input");
    input.type = "file";
    input.accept = ".vti,.stl";
    input.style.display = "none";
    const btn = el("button", "btn btn-primary", "load .vti / .stl…");
    row.append(btn, input);
    sidebar.appendChild(row);
    sidebar.appendChild(
      el("div", "hint", "…or drop a .vti / .stl file anywhere in the window."),
    );
    btn.addEventListener("click", () => input.click());
    input.addEventListener("change", () => {
      const f = input.files?.[0];
      if (f) void this.loadFile(f);
      input.value = "";
    });

    this.status = el("div", "import-status");
    this.status.style.display = "none";
    sidebar.appendChild(this.status);

    this.info = el("div", "result-info");
    this.info.style.display = "none";
    sidebar.appendChild(this.info);

    this.vtiControls = el("div");
    this.vtiControls.style.display = "none";
    sidebar.appendChild(this.vtiControls);

    // field + colormap
    this.vtiControls.appendChild(el("h3", undefined, "Field (slices + scale)"));
    const fieldRow = el("div", "bc-row");
    this.fieldSel = el("select");
    this.cmapSel = el("select");
    for (const name of COLORMAP_NAMES) this.cmapSel.appendChild(new Option(name, name));
    fieldRow.append(this.fieldSel, this.cmapSel);
    this.vtiControls.appendChild(fieldRow);
    this.fieldSel.addEventListener("change", () => this.update());
    this.cmapSel.addEventListener("change", () => this.update());

    // iso-surface
    this.vtiControls.appendChild(el("h3", undefined, "Iso-surface"));
    const isoRow1 = el("div", "bc-row");
    this.isoOn = el("input");
    this.isoOn.type = "checkbox";
    this.isoOn.checked = true;
    this.isoFieldSel = el("select");
    isoRow1.append(this.isoOn, this.isoFieldSel);
    this.vtiControls.appendChild(isoRow1);
    const isoRow2 = el("div", "bc-row");
    this.isoRange = el("input", "wide-range");
    this.isoRange.type = "range";
    this.isoLabel = el("span", "range-val");
    isoRow2.append(this.isoRange, this.isoLabel);
    this.vtiControls.appendChild(isoRow2);
    this.isoOn.addEventListener("change", () => this.update());
    this.isoFieldSel.addEventListener("change", () => this.onIsoFieldChange());
    this.isoRange.addEventListener("input", () => this.update());

    // slices
    this.vtiControls.appendChild(el("h3", undefined, "Slices"));
    for (let axis = 0; axis < 3; ++axis) {
      const r = el("div", "bc-row");
      const on = el("input");
      on.type = "checkbox";
      const tag = el("span", "axis-tag", AXES[axis]);
      const pos = el("input", "wide-range");
      pos.type = "range";
      pos.min = "0";
      pos.step = "1";
      const label = el("span", "range-val");
      r.append(on, tag, pos, label);
      this.vtiControls.appendChild(r);
      on.addEventListener("change", () => this.update());
      pos.addEventListener("input", () => this.update());
      this.sliceUIs.push({ enabled: on, pos, label });
    }

    this.vtiControls.appendChild(
      el(
        "div",
        "hint",
        "Iso-surface field + threshold are independent from the slice field: " +
          "e.g. density iso at 0.5 (coolant channels) + slice colored by temperature.",
      ),
    );
  }

  /** Call when the Results tab becomes visible (the canvas needs a resize). */
  onShow(): void {
    this.ctx.resize();
    this.ctx.render();
  }

  async loadFile(file: File): Promise<void> {
    const name = file.name.toLowerCase();
    try {
      if (name.endsWith(".vti")) {
        this.loadVti(parseVti(await file.text()), file.name);
      } else if (name.endsWith(".stl")) {
        this.loadStl(parseStl(await file.arrayBuffer()), file.name);
      } else {
        throw new Error("unsupported file type (expected .vti or .stl)");
      }
      this.showStatus("ok", `loaded ${file.name} ✓`);
    } catch (err) {
      this.showStatus("error", `${file.name}: ${err instanceof Error ? err.message : String(err)}`);
    }
  }

  // --- .vti -------------------------------------------------------------------

  private loadVti(data: VtiData, filename: string): void {
    this.vti = data;
    this.meshActor = null;

    // Cell fields -> point-data images (one per field, arrays shared with vtk).
    this.images.clear();
    for (const f of data.fields) {
      this.images.set(
        f.name,
        makeScalarImage(data.pointDims, data.spacing, data.origin, cellToPoint(data.cellDims, f.data), f.name),
      );
    }

    // Field selectors.
    const names = data.fields.map((f) => f.name);
    this.fieldSel.textContent = "";
    this.isoFieldSel.textContent = "";
    for (const n of names) {
      this.fieldSel.appendChild(new Option(n, n));
      this.isoFieldSel.appendChild(new Option(n, n));
    }
    // Acceptance defaults: iso on density@0.5, slice colored by temperature.
    this.isoFieldSel.value = names.includes("density") ? "density" : names[0]!;
    this.fieldSel.value = names.includes("temperature") ? "temperature" : names[0]!;
    this.isoOn.checked = true;
    this.setIsoSliderBounds(true);

    // Slices: Z at mid-height on, X/Y off at mid.
    for (let axis = 0; axis < 3; ++axis) {
      const ui = this.sliceUIs[axis]!;
      const nPts = data.pointDims[axis]!;
      ui.pos.max = String(nPts - 1);
      ui.pos.value = String(Math.floor(nPts / 2));
      ui.enabled.checked = axis === 2;
    }

    this.info.style.display = "";
    this.info.textContent =
      `${filename} — grid ${data.cellDims.join("×")} cells · ` +
      `${data.fields.length} field(s): ${names.join(", ")}`;
    this.vtiControls.style.display = "";

    this.update();
    this.ctx.resetCamera();
  }

  private field(name: string): VtiField | undefined {
    return this.vti?.fields.find((f) => f.name === name);
  }

  private setIsoSliderBounds(reset: boolean): void {
    const f = this.field(this.isoFieldSel.value);
    if (!f) return;
    const span = f.max > f.min ? f.max - f.min : 1;
    this.isoRange.min = String(f.min);
    this.isoRange.max = String(f.max);
    this.isoRange.step = String(span / 200);
    if (reset) {
      // 0.5 is the density convention; otherwise mid-range.
      const def = f.name === "density" && f.min <= 0.5 && f.max >= 0.5 ? 0.5 : f.min + span / 2;
      this.isoRange.value = String(def);
    }
  }

  private onIsoFieldChange(): void {
    this.setIsoSliderBounds(true);
    this.update();
  }

  /** Re-sync the whole vtk pipeline with the control state, then render. */
  private update(): void {
    if (this.meshActor) {
      this.ctx.setActors([this.meshActor]);
      this.legend.style.display = "none";
      this.ctx.render();
      return;
    }
    if (!this.vti) return;

    const actors: Parameters<ViewerContext["setActors"]>[0][number][] = [];

    // Iso-surface.
    const isoField = this.field(this.isoFieldSel.value);
    const isoValue = Number(this.isoRange.value);
    this.isoLabel.textContent = fmt(isoValue);
    if (this.isoOn.checked && isoField) {
      this.iso.setImage(this.images.get(isoField.name)!);
      this.iso.setValue(isoValue);
      actors.push(this.iso.actor);
    }

    // Slices, colored by the active field.
    const f = this.field(this.fieldSel.value) ?? this.vti.fields[0]!;
    const cmap = this.cmapSel.value as ColormapName;
    const ctf = makeColorFunction(cmap, f.min, f.max);
    const image = this.images.get(f.name)!;
    for (let axis = 0; axis < 3; ++axis) {
      const ui = this.sliceUIs[axis]!;
      const idx = Number(ui.pos.value);
      ui.label.textContent = String(idx);
      if (!ui.enabled.checked) continue;
      const s = this.slices[axis as 0 | 1 | 2];
      s.setImage(image);
      s.setSlice(idx);
      s.setColorFunction(ctf);
      actors.push(s.actor);
    }

    // Scalar bar.
    drawColormap(this.legendCanvas, cmap);
    this.legendField.textContent = f.name;
    this.legendMin.textContent = fmt(f.min);
    this.legendMax.textContent = fmt(f.max);
    this.legend.style.display = "";

    this.ctx.setActors(actors);
    this.ctx.render();
  }

  // --- .stl -------------------------------------------------------------------

  private loadStl(mesh: StlMesh, filename: string): void {
    this.vti = null;
    this.images.clear();
    this.meshActor = makeMeshActor(mesh);
    this.vtiControls.style.display = "none";
    this.info.style.display = "";
    this.info.textContent = `${filename} — ${mesh.triangleCount} triangles (binary/ASCII STL)`;
    this.update();
    this.ctx.resetCamera();
  }

  // --- misc -------------------------------------------------------------------

  private showStatus(kind: "ok" | "error", msg: string): void {
    this.status.className = `import-status import-${kind}`;
    this.status.textContent = msg;
    this.status.style.display = "";
  }
}
