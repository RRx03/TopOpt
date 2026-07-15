// Export panel: download <name>.topopt.json + live JSON preview (copyable).
// M2: compatibility-matrix deviations are BLOCKING here — the errors are
// listed and the download is disabled until the spec conforms.

import { checkCompat } from "../spec/compat";
import { specToString } from "../spec/serialize";
import type { Store } from "../state";

export function setupExportPanel(container: HTMLElement, store: Store): void {
  const h = document.createElement("h2");
  h.textContent = "Export";
  const row = document.createElement("div");
  row.className = "bc-row";
  const dl = document.createElement("button");
  dl.className = "btn btn-primary";
  const copy = document.createElement("button");
  copy.className = "btn";
  copy.textContent = "copy JSON";
  const errBox = document.createElement("div");
  errBox.className = "import-status import-error";
  errBox.style.display = "none";
  const pre = document.createElement("pre");
  pre.className = "json-preview";

  row.appendChild(dl);
  row.appendChild(copy);
  container.appendChild(h);
  container.appendChild(row);
  container.appendChild(errBox);
  container.appendChild(pre);

  const render = (): void => {
    dl.textContent = `download ${store.spec.name}.topopt.json`;
    pre.textContent = specToString(store.spec);

    const issues = checkCompat(store.spec);
    dl.disabled = issues.length > 0;
    dl.title = issues.length > 0 ? "export blocked by compatibility errors" : "";
    errBox.textContent = "";
    if (issues.length > 0) {
      const title = document.createElement("div");
      title.className = "import-title";
      title.textContent = "export bloqué — erreurs de compatibilité :";
      errBox.appendChild(title);
      for (const it of issues) {
        const line = document.createElement("div");
        line.className = "import-item";
        line.textContent = `• ${it}`;
        errBox.appendChild(line);
      }
    }
    errBox.style.display = issues.length > 0 ? "" : "none";
  };

  dl.addEventListener("click", () => {
    if (checkCompat(store.spec).length > 0) return; // blocking errors
    const blob = new Blob([specToString(store.spec)], { type: "application/json" });
    const url = URL.createObjectURL(blob);
    const a = document.createElement("a");
    a.href = url;
    a.download = `${store.spec.name}.topopt.json`;
    a.click();
    URL.revokeObjectURL(url);
  });

  copy.addEventListener("click", () => {
    void navigator.clipboard.writeText(specToString(store.spec)).then(() => {
      copy.textContent = "copied ✓";
      setTimeout(() => (copy.textContent = "copy JSON"), 1200);
    });
  });

  store.on(render);
  render();
}
