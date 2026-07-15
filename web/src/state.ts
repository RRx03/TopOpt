// Single source of truth: one ProblemSpec + the current face/edge selection.
// Framework-free (no three.js / lil-gui imports) so the golden test can drive
// the exact same API the UI uses.

import {
  canonicalEdge,
  emptyBC,
  type BCEntry,
  type ProblemSpec,
} from "./spec/ProblemSpec";

export type Face = "x-" | "x+" | "y-" | "y+" | "z-" | "z+";
export const FACES: readonly Face[] = ["x-", "x+", "y-", "y+", "z-", "z+"];

// 1 face selected, or 2 adjacent faces (their intersection = an edge, the
// `edge:"x-,y+"` selector of the contract).
export interface Selection {
  faces: Face[];
}

export type BCKind = "fixed" | "loads";
export type Listener = () => void;

export function adjacent(a: Face, b: Face): boolean {
  return a[0] !== b[0]; // different axes => the faces share an edge
}

export function selectionToSelector(sel: Selection): Partial<BCEntry> {
  if (sel.faces.length === 2)
    return { edge: canonicalEdge(sel.faces[0]!, sel.faces[1]!) };
  return { face: sel.faces[0]! };
}

export function describeBC(kind: BCKind, e: BCEntry): string {
  const where = e.face ? `face ${e.face}` : e.edge ? `edge ${e.edge}` : e.node ? `node ${e.node}` : `region ${e.region}`;
  return kind === "fixed"
    ? `support · ${where} · dof ${e.dof}`
    : `load · ${where} · F${e.dof} = ${e.value}`;
}

export class Store {
  spec: ProblemSpec;
  selection: Selection | null = null;
  private listeners: Listener[] = [];

  constructor(spec: ProblemSpec) {
    this.spec = spec;
  }

  on(fn: Listener): void {
    this.listeners.push(fn);
  }

  emit(): void {
    for (const fn of this.listeners) fn();
  }

  loadSpec(spec: ProblemSpec): void {
    this.spec = spec;
    this.selection = null;
    this.emit();
  }

  // --- selection ---

  selectFace(face: Face, extend: boolean): void {
    if (
      extend &&
      this.selection?.faces.length === 1 &&
      this.selection.faces[0] !== face &&
      adjacent(this.selection.faces[0]!, face)
    ) {
      this.selection = { faces: [this.selection.faces[0]!, face] };
    } else {
      this.selection = { faces: [face] };
    }
    this.emit();
  }

  clearSelection(): void {
    if (!this.selection) return;
    this.selection = null;
    this.emit();
  }

  // --- boundary conditions ---

  addFixed(sel: Selection, dof: "x" | "y" | "z" | "all"): void {
    this.spec.fixed.push({ ...emptyBC(), ...selectionToSelector(sel), dof });
    this.emit();
  }

  addLoad(sel: Selection, dof: "x" | "y" | "z", value: number): void {
    this.spec.loads.push({ ...emptyBC(), ...selectionToSelector(sel), dof, value });
    this.emit();
  }

  removeBC(kind: BCKind, index: number): void {
    this.spec[kind].splice(index, 1);
    this.emit();
  }
}
