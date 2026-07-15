import "./style.css";
import { Store } from "./state";
import { mbb3dPreset } from "./spec/presets";
import { Editor } from "./editor/scene";
import { setupPanels } from "./panels/panels";
import { setupBCPanel } from "./panels/bcPanel";
import { setupExportPanel } from "./panels/exportPanel";

const store = new Store(mbb3dPreset());

new Editor(document.getElementById("viewport")!, store);
setupPanels(document.getElementById("gui-panel")!, store);
setupBCPanel(document.getElementById("bc-panel")!, store);
setupExportPanel(document.getElementById("export-panel")!, store);
