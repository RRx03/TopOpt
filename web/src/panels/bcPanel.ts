// Boundary-conditions panel (vanilla DOM): current selection, add support /
// add load forms, list of BCs with delete buttons. M2: non-elastic entries
// (thermal / flow / pressure / drive) are preserved on import and listed in a
// read-only section — their edition is the V2 of the 3D editor.

import type { BCEntry, ProblemSpec } from "../spec/ProblemSpec";
import { describeBC, type BCKind, type Store } from "../state";

function where(e: BCEntry): string {
  if (e.face) return `face ${e.face}`;
  if (e.edge) return `edge ${e.edge}`;
  if (e.node) return `node ${e.node}`;
  if (e.region) return `region ${e.region}`;
  return "(no selector)";
}

type AdvKind = "thermal" | "flow" | "pressure";

function describeAdvanced(kind: AdvKind, e: BCEntry): string {
  if (kind === "thermal") return `thermal · ${where(e)} · ${e.dof || "?"} = ${e.value}`;
  if (kind === "flow") return `flow · ${where(e)} · ${e.dof || "?"}`;
  return `pressure · ${where(e)} · p = ${e.value}`;
}

// Read-only rows: every advanced BC of the spec + the Stokes drive.
function advancedRows(s: ProblemSpec): string[] {
  const rows: string[] = [];
  const kinds: readonly AdvKind[] = ["thermal", "flow", "pressure"];
  for (const kind of kinds)
    for (const e of s[kind]) {
      if (kind === "flow" && !e.face && !e.edge && !e.node && !e.region && !e.dof)
        continue; // parse residue of a drive-only entry, shown via body_force
      rows.push(describeAdvanced(kind, e));
    }
  if (s.body_force.some((v) => v !== 0))
    rows.push(`flow drive = [${s.body_force.join(", ")}]`);
  return rows;
}

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

function dofSelect(dofs: string[]): HTMLSelectElement {
  const sel = el("select");
  for (const d of dofs) {
    const o = el("option", undefined, d);
    o.value = d;
    sel.appendChild(o);
  }
  return sel;
}

export function setupBCPanel(container: HTMLElement, store: Store): void {
  const selInfo = el("div", "sel-info");
  const hint = el(
    "div",
    "hint",
    "Click a face to select it; shift+click an adjacent face to select their common edge. Loads are total forces distributed over the selection (mm units).",
  );

  // -- add support --
  const supRow = el("div", "bc-row");
  supRow.appendChild(el("span", "bc-tag bc-tag-fixed", "support"));
  const supDof = dofSelect(["x", "y", "z", "all"]);
  supRow.appendChild(supDof);
  const supBtn = el("button", "btn", "add fixed");
  supRow.appendChild(supBtn);

  // -- add load --
  const loadRow = el("div", "bc-row");
  loadRow.appendChild(el("span", "bc-tag bc-tag-load", "load"));
  const loadDof = dofSelect(["x", "y", "z"]);
  loadRow.appendChild(loadDof);
  const loadVal = el("input");
  loadVal.type = "number";
  loadVal.step = "0.1";
  loadVal.value = "-1";
  loadVal.title = "total force on the selection";
  loadRow.appendChild(loadVal);
  const loadBtn = el("button", "btn", "add load");
  loadRow.appendChild(loadBtn);

  const list = el("div", "bc-list");

  // V2 banner + read-only advanced BCs (shown only when the spec has them).
  const banner = el("div", "banner-v2");
  const advTitle = el("h3", undefined, "BCs avancées (lecture seule)");
  const advList = el("div", "bc-list");

  container.appendChild(el("h2", undefined, "Boundary conditions"));
  container.appendChild(banner);
  container.appendChild(selInfo);
  container.appendChild(supRow);
  container.appendChild(loadRow);
  container.appendChild(hint);
  container.appendChild(el("h3", undefined, "Applied"));
  container.appendChild(list);
  container.appendChild(advTitle);
  container.appendChild(advList);

  supBtn.addEventListener("click", () => {
    if (store.selection)
      store.addFixed(store.selection, supDof.value as "x" | "y" | "z" | "all");
  });
  loadBtn.addEventListener("click", () => {
    const v = Number(loadVal.value);
    if (store.selection && Number.isFinite(v) && v !== 0)
      store.addLoad(store.selection, loadDof.value as "x" | "y" | "z", v);
  });

  const render = (): void => {
    const sel = store.selection;
    selInfo.textContent = sel
      ? sel.faces.length === 2
        ? `selected: edge ${sel.faces.join(" ∩ ")}`
        : `selected: face ${sel.faces[0]}`
      : "no selection";
    selInfo.classList.toggle("active", !!sel);
    supBtn.disabled = loadBtn.disabled = !sel;

    list.textContent = "";
    const kinds: BCKind[] = ["fixed", "loads"];
    let count = 0;
    for (const kind of kinds)
      store.spec[kind].forEach((e, i) => {
        count++;
        const row = el("div", "bc-item");
        row.appendChild(
          el("span", `bc-dot ${kind === "fixed" ? "bc-dot-fixed" : "bc-dot-load"}`),
        );
        row.appendChild(el("span", "bc-desc", describeBC(kind, e)));
        const del = el("button", "btn btn-del", "×");
        del.title = "remove";
        del.addEventListener("click", () => store.removeBC(kind, i));
        row.appendChild(del);
        list.appendChild(row);
      });
    if (count === 0) list.appendChild(el("div", "empty", "none yet"));

    // banner: non-elastic physics / axi dim are read-only in M2 (editor V2)
    const s = store.spec;
    const notes: string[] = [];
    if (s.physics.includes("fluid") || s.physics.includes("thermal"))
      notes.push("physique fluide-thermique : édition V2");
    if (s.dim === "axi") notes.push("dim axi : édition V2");
    banner.textContent = notes.join(" · ");
    banner.style.display = notes.length ? "" : "none";

    // read-only advanced entries (preserved verbatim on export)
    const rows = advancedRows(s);
    advList.textContent = "";
    for (const text of rows) {
      const row = el("div", "bc-item bc-item-ro");
      row.appendChild(el("span", "bc-dot bc-dot-adv"));
      row.appendChild(el("span", "bc-desc", text));
      row.appendChild(el("span", "ro-tag", "ro"));
      advList.appendChild(row);
    }
    advTitle.style.display = rows.length ? "" : "none";
    advList.style.display = rows.length ? "" : "none";
  };

  store.on(render);
  render();
}
