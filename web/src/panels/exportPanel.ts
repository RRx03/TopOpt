// Export panel: download <name>.topopt.json + live JSON preview (copyable).

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
  const pre = document.createElement("pre");
  pre.className = "json-preview";

  row.appendChild(dl);
  row.appendChild(copy);
  container.appendChild(h);
  container.appendChild(row);
  container.appendChild(pre);

  const render = (): void => {
    dl.textContent = `download ${store.spec.name}.topopt.json`;
    pre.textContent = specToString(store.spec);
  };

  dl.addEventListener("click", () => {
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
