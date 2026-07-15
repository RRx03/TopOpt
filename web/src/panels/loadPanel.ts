// Load panel: import a .topopt.json (file picker + drag-drop anywhere on the
// window) and the preset gallery (the 6 repo examples embedded at build).
// Import replaces the Store state; compatibility-matrix deviations are shown
// as a NON-blocking warning list (export is where they become blocking).

import { checkCompat } from "../spec/compat";
import { blankPreset, examplePresets } from "../spec/presets";
import { specFromString } from "../spec/serialize";
import type { Store } from "../state";

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

export function setupLoadPanel(container: HTMLElement, store: Store): void {
  container.appendChild(el("h2", undefined, "Load"));

  // -- import ----------------------------------------------------------------
  const row = el("div", "bc-row");
  const input = el("input");
  input.type = "file";
  input.accept = ".json,.topopt.json,application/json";
  input.style.display = "none";
  const btn = el("button", "btn btn-primary", "import .topopt.json…");
  row.appendChild(btn);
  row.appendChild(input);
  container.appendChild(row);
  container.appendChild(
    el("div", "hint", "…or drop a .topopt.json file anywhere in the window."),
  );

  const status = el("div", "import-status");
  status.style.display = "none";
  container.appendChild(status);

  const showStatus = (kind: "ok" | "warn" | "error", title: string, items: string[]): void => {
    status.className = `import-status import-${kind}`;
    status.textContent = "";
    status.appendChild(el("div", "import-title", title));
    for (const it of items) status.appendChild(el("div", "import-item", `• ${it}`));
    status.style.display = "";
  };

  const loadText = (text: string, origin: string): void => {
    let spec;
    try {
      spec = specFromString(text);
    } catch (err) {
      showStatus("error", `import failed — ${origin}`, [
        err instanceof Error ? err.message : String(err),
      ]);
      return;
    }
    store.loadSpec(spec);
    const issues = checkCompat(spec);
    if (issues.length > 0)
      showStatus(
        "warn",
        `imported "${spec.name}" — compatibility warnings (non-blocking):`,
        issues,
      );
    else showStatus("ok", `imported "${spec.name}" ✓`, []);
  };

  const loadFile = (file: File): void => {
    void file.text().then((text) => loadText(text, file.name));
  };

  btn.addEventListener("click", () => input.click());
  input.addEventListener("change", () => {
    const f = input.files?.[0];
    if (f) loadFile(f);
    input.value = ""; // allow re-importing the same file
  });

  // drag-drop on the whole window
  window.addEventListener("dragover", (e) => {
    if (e.dataTransfer?.types.includes("Files")) e.preventDefault();
  });
  window.addEventListener("drop", (e) => {
    const f = e.dataTransfer?.files[0];
    if (!f) return;
    e.preventDefault();
    loadFile(f);
  });

  // -- preset gallery ----------------------------------------------------------
  container.appendChild(el("h3", undefined, "Presets (repo examples)"));
  const gallery = el("div", "preset-list");
  container.appendChild(gallery);

  for (const p of examplePresets) {
    const item = el("div", "preset-item");
    const head = el("div", "preset-head");
    const load = el("button", "btn preset-load", p.id);
    load.title = `load ${p.file}`;
    head.appendChild(load);
    head.appendChild(el("span", "preset-phys", p.physics));
    item.appendChild(head);
    item.appendChild(el("div", "preset-desc", p.description));
    gallery.appendChild(item);
    load.addEventListener("click", () => loadText(p.raw, p.file));
  }

  const blankRow = el("div", "bc-row");
  const blankBtn = el("button", "btn", "blank (elastic box)");
  blankRow.appendChild(blankBtn);
  container.appendChild(blankRow);
  blankBtn.addEventListener("click", () => {
    store.loadSpec(blankPreset());
    status.style.display = "none";
  });
}
