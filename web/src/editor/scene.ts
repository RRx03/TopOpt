// three.js editor: domain box (wireframe + floor grid + annotated axes),
// face picking (hover highlight, click select, shift+click on an adjacent
// face = edge selection), BC glyphs. Rebuilt from the Store on every change.

import * as THREE from "three";
import { OrbitControls } from "three/examples/jsm/controls/OrbitControls.js";
import type { Face, Store } from "../state";
import { FACES } from "../state";
import { edgeSegment, faceFrame } from "./geometry";
import { buildGlyphs } from "./glyphs";

const COLOR_BG = 0x101318;
const COLOR_ACCENT = 0x6cb2ff;
const COLOR_EDGE_SEL = 0xffd166;
const OPACITY_IDLE = 0.03;
const OPACITY_HOVER = 0.14;
const OPACITY_SELECTED = 0.28;

function axisLabel(text: string, color: string): THREE.Sprite {
  const canvas = document.createElement("canvas");
  canvas.width = canvas.height = 64;
  const ctx = canvas.getContext("2d")!;
  ctx.font = "bold 44px 'SF Mono', Menlo, monospace";
  ctx.textAlign = "center";
  ctx.textBaseline = "middle";
  ctx.fillStyle = color;
  ctx.fillText(text, 32, 34);
  const tex = new THREE.CanvasTexture(canvas);
  const sprite = new THREE.Sprite(
    new THREE.SpriteMaterial({ map: tex, depthTest: false, transparent: true }),
  );
  return sprite;
}

export class Editor {
  private renderer: THREE.WebGLRenderer;
  private scene = new THREE.Scene();
  private camera: THREE.PerspectiveCamera;
  private controls: OrbitControls;
  private raycaster = new THREE.Raycaster();
  private pointer = new THREE.Vector2();
  private domainGroup = new THREE.Group();
  private helpersGroup = new THREE.Group();
  private pickPlanes: THREE.Mesh[] = [];
  private hovered: Face | null = null;
  private downPos: { x: number; y: number } | null = null;
  private lastSize = "";

  constructor(
    private container: HTMLElement,
    private store: Store,
  ) {
    this.renderer = new THREE.WebGLRenderer({ antialias: true });
    this.renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2));
    this.renderer.setClearColor(COLOR_BG);
    container.appendChild(this.renderer.domElement);

    this.camera = new THREE.PerspectiveCamera(45, 1, 0.1, 5000);
    this.controls = new OrbitControls(this.camera, this.renderer.domElement);
    this.controls.enableDamping = true;
    this.controls.dampingFactor = 0.08;

    this.scene.add(new THREE.HemisphereLight(0xdfe6f0, 0x20242c, 1.0));
    const dir = new THREE.DirectionalLight(0xffffff, 1.2);
    dir.position.set(1, 2, 1.5);
    this.scene.add(dir);
    this.scene.add(this.helpersGroup);
    this.scene.add(this.domainGroup);

    const canvas = this.renderer.domElement;
    canvas.addEventListener("pointermove", (e) => this.onPointerMove(e));
    canvas.addEventListener("pointerdown", (e) => {
      this.downPos = { x: e.clientX, y: e.clientY };
    });
    canvas.addEventListener("pointerup", (e) => this.onPointerUp(e));

    new ResizeObserver(() => this.resize()).observe(container);
    this.resize();

    store.on(() => this.sync());
    this.sync();
    this.frameCamera();
    this.animate();
  }

  private get L(): readonly number[] {
    return this.store.spec.size_mm;
  }

  // --- build / rebuild -----------------------------------------------------

  // The scene is tiny (a box, 6 planes, a few dozen glyphs): rebuild it in
  // full on every state change instead of diffing.
  private sync(): void {
    this.rebuildDomain();
    const sizeKey = JSON.stringify(this.store.spec.size_mm);
    if (sizeKey !== this.lastSize) {
      this.lastSize = sizeKey;
      this.rebuildHelpers();
    }
  }

  private rebuildDomain(): void {
    this.domainGroup.clear();
    this.pickPlanes = [];
    const [Lx, Ly, Lz] = [this.L[0]!, this.L[1]!, this.L[2]!];

    // Domain box: faint volume + wireframe.
    const boxGeo = new THREE.BoxGeometry(Lx, Ly, Lz);
    boxGeo.translate(Lx / 2, Ly / 2, Lz / 2);
    const volume = new THREE.Mesh(
      boxGeo,
      new THREE.MeshBasicMaterial({
        color: 0x8899aa,
        transparent: true,
        opacity: 0.045,
        depthWrite: false,
      }),
    );
    this.domainGroup.add(volume);
    const wire = new THREE.LineSegments(
      new THREE.EdgesGeometry(boxGeo),
      new THREE.LineBasicMaterial({ color: 0x9aa7b8 }),
    );
    this.domainGroup.add(wire);

    // Pick planes (one per face), highlighted by hover / selection state.
    const selected = this.store.selection?.faces ?? [];
    for (const face of FACES) {
      const f = faceFrame(face, this.L);
      const geo = new THREE.PlaneGeometry(f.su, f.sv);
      const mat = new THREE.MeshBasicMaterial({
        color: COLOR_ACCENT,
        transparent: true,
        opacity: selected.includes(face)
          ? OPACITY_SELECTED
          : face === this.hovered
            ? OPACITY_HOVER
            : OPACITY_IDLE,
        side: THREE.DoubleSide,
        depthWrite: false,
      });
      const mesh = new THREE.Mesh(geo, mat);
      // PlaneGeometry spans XY with +Z normal: map (X->u, Y->v, Z->normal).
      const m = new THREE.Matrix4().makeBasis(f.u, f.v, f.normal);
      mesh.quaternion.setFromRotationMatrix(m);
      mesh.position.copy(f.center);
      mesh.userData.face = face;
      this.pickPlanes.push(mesh);
      this.domainGroup.add(mesh);
    }

    // Selected-edge highlight (two adjacent faces picked).
    if (selected.length === 2) {
      const [p0, p1] = edgeSegment(selected[0]!, selected[1]!, this.L);
      const len = p0.distanceTo(p1);
      const r = 0.006 * Math.max(Lx, Ly, Lz);
      const cyl = new THREE.Mesh(
        new THREE.CylinderGeometry(r, r, len, 8),
        new THREE.MeshBasicMaterial({ color: COLOR_EDGE_SEL }),
      );
      cyl.position.copy(p0).lerp(p1, 0.5);
      cyl.quaternion.setFromUnitVectors(
        new THREE.Vector3(0, 1, 0),
        p1.clone().sub(p0).normalize(),
      );
      this.domainGroup.add(cyl);
    }

    // BC glyphs.
    this.domainGroup.add(buildGlyphs(this.store.spec.fixed, this.store.spec.loads, this.L));
  }

  private rebuildHelpers(): void {
    this.helpersGroup.clear();
    const [Lx, Ly, Lz] = [this.L[0]!, this.L[1]!, this.L[2]!];
    const maxL = Math.max(Lx, Ly, Lz);

    // Floor grid (y = 0 plane).
    const grid = new THREE.GridHelper(3 * maxL, 30, 0x2e3542, 0x1d222b);
    grid.position.set(Lx / 2, 0, Lz / 2);
    this.helpersGroup.add(grid);

    // Annotated axes at the domain origin.
    const axes: Array<[THREE.Vector3, number, string, string]> = [
      [new THREE.Vector3(1, 0, 0), Lx, "X", "#e35d5d"],
      [new THREE.Vector3(0, 1, 0), Ly, "Y", "#67c26b"],
      [new THREE.Vector3(0, 0, 1), Lz, "Z", "#5b8cff"],
    ];
    for (const [dirV, len, name, color] of axes) {
      const end = dirV.clone().multiplyScalar(len * 1.25);
      const geo = new THREE.BufferGeometry().setFromPoints([new THREE.Vector3(), end]);
      this.helpersGroup.add(
        new THREE.Line(geo, new THREE.LineBasicMaterial({ color: new THREE.Color(color) })),
      );
      const label = axisLabel(name, color);
      label.position.copy(end).addScaledVector(dirV, 0.06 * maxL);
      label.scale.setScalar(0.09 * maxL);
      this.helpersGroup.add(label);
    }
  }

  private frameCamera(): void {
    const [Lx, Ly, Lz] = [this.L[0]!, this.L[1]!, this.L[2]!];
    const c = new THREE.Vector3(Lx / 2, Ly / 2, Lz / 2);
    const d = 1.7 * Math.max(Lx, Ly, Lz);
    this.camera.position.set(c.x + d * 0.8, c.y + d * 0.7, c.z + d * 0.9);
    this.controls.target.copy(c);
    this.controls.update();
  }

  // --- picking ---------------------------------------------------------------

  private pick(e: PointerEvent): Face | null {
    const rect = this.renderer.domElement.getBoundingClientRect();
    this.pointer.set(
      ((e.clientX - rect.left) / rect.width) * 2 - 1,
      -((e.clientY - rect.top) / rect.height) * 2 + 1,
    );
    this.raycaster.setFromCamera(this.pointer, this.camera);
    const hits = this.raycaster.intersectObjects(this.pickPlanes, false);
    return hits.length > 0 ? ((hits[0]!.object.userData.face as Face) ?? null) : null;
  }

  private onPointerMove(e: PointerEvent): void {
    const face = this.pick(e);
    if (face === this.hovered) return;
    this.hovered = face;
    this.renderer.domElement.style.cursor = face ? "pointer" : "default";
    this.rebuildDomain(); // refresh highlight opacities
  }

  private onPointerUp(e: PointerEvent): void {
    if (!this.downPos) return;
    const moved = Math.hypot(e.clientX - this.downPos.x, e.clientY - this.downPos.y);
    this.downPos = null;
    if (moved > 5) return; // orbit drag, not a click
    const face = this.pick(e);
    if (face) this.store.selectFace(face, e.shiftKey);
    else this.store.clearSelection();
  }

  // --- loop ------------------------------------------------------------------

  private resize(): void {
    const w = this.container.clientWidth || 1;
    const h = this.container.clientHeight || 1;
    this.renderer.setSize(w, h);
    this.camera.aspect = w / h;
    this.camera.updateProjectionMatrix();
  }

  private animate = (): void => {
    requestAnimationFrame(this.animate);
    this.controls.update();
    this.renderer.render(this.scene, this.camera);
  };
}
