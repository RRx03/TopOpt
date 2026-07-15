import "./style.css";
import { Store } from "./state";
import { mbb3dPreset } from "./spec/presets";
import { Editor } from "./editor/scene";
import { setupPanels } from "./panels/panels";
import { setupBCPanel } from "./panels/bcPanel";
import { setupExportPanel } from "./panels/exportPanel";
import { setupLoadPanel } from "./panels/loadPanel";
import type { ResultsViewer } from "./viewer/viewer";

const store = new Store(mbb3dPreset());

new Editor(document.getElementById("viewport")!, store);
setupPanels(document.getElementById("gui-panel")!, store);
setupLoadPanel(document.getElementById("load-panel")!, store);
setupBCPanel(document.getElementById("bc-panel")!, store);
setupExportPanel(document.getElementById("export-panel")!, store);

// --- M3: editor / results mode switch ----------------------------------------
// The editor (three.js scene + Store) stays alive while hidden — switching back
// restores the exact editing state. vtk.js lives in a lazy chunk: it is only
// downloaded on the first switch to Results (or first .vti/.stl drop).

type Mode = "editor" | "results";

const editorSide = document.getElementById("editor-side")!;
const resultsSide = document.getElementById("results-side")!;
const editorViewport = document.getElementById("viewport")!;
const resultsViewport = document.getElementById("results-viewport")!;
const guiPanel = document.getElementById("gui-panel")!;
const tabs = [...document.querySelectorAll<HTMLButtonElement>(".mode-tab")];

let viewer: ResultsViewer | null = null;
let viewerLoading: Promise<ResultsViewer> | null = null;

function ensureViewer(): Promise<ResultsViewer> {
  if (viewer) return Promise.resolve(viewer);
  viewerLoading ??= import("./viewer/viewer").then(({ ResultsViewer }) => {
    viewer = new ResultsViewer(resultsViewport, resultsSide);
    return viewer;
  });
  return viewerLoading;
}

function setMode(mode: Mode): void {
  const results = mode === "results";
  editorSide.hidden = results;
  editorViewport.hidden = results;
  guiPanel.hidden = results;
  resultsSide.hidden = !results;
  resultsViewport.hidden = !results;
  for (const t of tabs) t.classList.toggle("active", t.dataset.mode === mode);
  if (results) void ensureViewer().then((v) => v.onShow());
}

for (const t of tabs)
  t.addEventListener("click", () => setMode(t.dataset.mode as Mode));

// Results files dropped anywhere: capture phase so the .topopt.json handler of
// the load panel (bubble phase on window) never sees .vti/.stl files.
window.addEventListener(
  "dragover",
  (e) => {
    if (e.dataTransfer?.types.includes("Files")) e.preventDefault();
  },
  true,
);
window.addEventListener(
  "drop",
  (e) => {
    const f = e.dataTransfer?.files[0];
    if (!f || !/\.(vti|stl)$/i.test(f.name)) return; // let the editor import handle it
    e.preventDefault();
    e.stopPropagation();
    setMode("results");
    void ensureViewer().then((v) => v.loadFile(f));
  },
  true,
);
