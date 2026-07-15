// Single wrapper around @kitware/vtk.js — the only module of the app that
// imports it (the Results mode loads it as a lazy chunk, the editor bundle
// stays vtk-free). The official .d.ts cover everything used here except the
// rendering profiles and vtkImageMarchingCubes, typed locally in
// vtk-modules.d.ts — no `any` leaks out of this module.

// OpenGL-only profiles (the generic ones would also bundle the whole WebGPU
// backend; GenericRenderWindow defaults to the WebGL view anyway).
import "@kitware/vtk.js/Rendering/OpenGL/Profiles/Geometry"; // PolyData mapper/actor
import "@kitware/vtk.js/Rendering/OpenGL/Profiles/Volume"; // ImageMapper/ImageSlice

import vtkGenericRenderWindow from "@kitware/vtk.js/Rendering/Misc/GenericRenderWindow";
import vtkImageData from "@kitware/vtk.js/Common/DataModel/ImageData";
import vtkDataArray from "@kitware/vtk.js/Common/Core/DataArray";
import vtkPolyData from "@kitware/vtk.js/Common/DataModel/PolyData";
import vtkActor from "@kitware/vtk.js/Rendering/Core/Actor";
import vtkMapper from "@kitware/vtk.js/Rendering/Core/Mapper";
import vtkImageMapper from "@kitware/vtk.js/Rendering/Core/ImageMapper";
import vtkImageSlice from "@kitware/vtk.js/Rendering/Core/ImageSlice";
import vtkColorTransferFunction from "@kitware/vtk.js/Rendering/Core/ColorTransferFunction";
import vtkImageMarchingCubes from "@kitware/vtk.js/Filters/General/ImageMarchingCubes";
import { SlicingMode } from "@kitware/vtk.js/Rendering/Core/ImageMapper/Constants";

import { COLORMAPS, type ColormapName } from "./colormaps";
import type { StlMesh } from "./stlParser";

export type SliceAxis = 0 | 1 | 2;

// Opaque handles for viewer.ts (which never touches vtk API directly).
export type ScalarImage = ReturnType<typeof vtkImageData.newInstance>;
export type ColorFunction = ReturnType<typeof vtkColorTransferFunction.newInstance>;
type AnyActor = ReturnType<typeof vtkActor.newInstance> | ReturnType<typeof vtkImageSlice.newInstance>;

const BG: [number, number, number] = [0.063, 0.075, 0.094]; // --bg #101318

// --- render window -----------------------------------------------------------

export interface ViewerContext {
  render(): void;
  resize(): void;
  resetCamera(): void;
  setActors(actors: readonly AnyActor[]): void;
  dispose(): void;
}

export function createViewerContext(container: HTMLElement): ViewerContext {
  const grw = vtkGenericRenderWindow.newInstance({ background: BG, listenWindowResize: true });
  grw.setContainer(container);
  const renderer = grw.getRenderer();
  const renderWindow = grw.getRenderWindow();
  let current: AnyActor[] = [];
  return {
    render: () => renderWindow.render(),
    resize: () => grw.resize(),
    resetCamera: () => {
      renderer.resetCamera();
      renderWindow.render();
    },
    setActors: (actors) => {
      for (const a of current) renderer.removeActor(a);
      current = [...actors];
      for (const a of current) renderer.addActor(a);
    },
    dispose: () => grw.delete(),
  };
}

// --- data --------------------------------------------------------------------

/** Uniform image with one point-data scalar field (values length = prod(pointDims)). */
export function makeScalarImage(
  pointDims: readonly [number, number, number],
  spacing: readonly [number, number, number],
  origin: readonly [number, number, number],
  values: Float32Array,
  name: string,
): ScalarImage {
  const image = vtkImageData.newInstance();
  image.setDimensions(pointDims[0], pointDims[1], pointDims[2]);
  image.setSpacing([spacing[0], spacing[1], spacing[2]]);
  image.setOrigin([origin[0], origin[1], origin[2]]);
  image.getPointData().setScalars(
    vtkDataArray.newInstance({ name, values, numberOfComponents: 1 }),
  );
  return image;
}

// --- colormap ----------------------------------------------------------------

export function makeColorFunction(name: ColormapName, min: number, max: number): ColorFunction {
  const ctf = vtkColorTransferFunction.newInstance();
  const span = max > min ? max - min : 1;
  for (const [t, r, g, b] of COLORMAPS[name]) ctf.addRGBPoint(min + t * span, r, g, b);
  return ctf;
}

// --- iso-surface pipeline ------------------------------------------------------

export interface IsoPipeline {
  actor: AnyActor;
  setImage(image: ScalarImage): void;
  setValue(v: number): void;
}

export function makeIsoPipeline(color: readonly [number, number, number]): IsoPipeline {
  const mc = vtkImageMarchingCubes.newInstance({
    contourValue: 0.5,
    computeNormals: true,
    mergePoints: true,
  });
  const mapper = vtkMapper.newInstance();
  mapper.setScalarVisibility(false); // solid color, shaded
  mapper.setInputConnection(mc.getOutputPort());
  const actor = vtkActor.newInstance();
  actor.setMapper(mapper);
  actor.getProperty().setColor(color[0], color[1], color[2]);
  actor.getProperty().setSpecular(0.15);
  return {
    actor,
    setImage: (image) => mc.setInputData(image),
    setValue: (v) => mc.setContourValue(v),
  };
}

// --- orthogonal slice pipeline -------------------------------------------------

export interface SlicePipeline {
  actor: AnyActor;
  setImage(image: ScalarImage): void;
  setSlice(index: number): void;
  setColorFunction(ctf: ColorFunction): void;
}

const SLICING_MODES = [SlicingMode.I, SlicingMode.J, SlicingMode.K] as const;

export function makeSlicePipeline(axis: SliceAxis): SlicePipeline {
  const mapper = vtkImageMapper.newInstance();
  mapper.setSlicingMode(SLICING_MODES[axis]);
  const actor = vtkImageSlice.newInstance();
  actor.setMapper(mapper);
  const prop = actor.getProperty();
  prop.setInterpolationTypeToLinear();
  prop.setUseLookupTableScalarRange(true);
  return {
    actor,
    setImage: (image) => mapper.setInputData(image),
    setSlice: (index) => mapper.setSlice(index),
    setColorFunction: (ctf) => prop.setRGBTransferFunction(0, ctf),
  };
}

// --- STL mesh ------------------------------------------------------------------

export function makeMeshActor(mesh: StlMesh): AnyActor {
  const pd = vtkPolyData.newInstance();
  pd.getPoints().setData(mesh.positions, 3);
  const cells = new Uint32Array(4 * mesh.triangleCount);
  for (let t = 0; t < mesh.triangleCount; ++t) {
    cells[4 * t] = 3;
    cells[4 * t + 1] = 3 * t;
    cells[4 * t + 2] = 3 * t + 1;
    cells[4 * t + 3] = 3 * t + 2;
  }
  pd.getPolys().setData(cells);
  pd.getPointData().setNormals(
    vtkDataArray.newInstance({ name: "Normals", values: mesh.normals, numberOfComponents: 3 }),
  );
  const mapper = vtkMapper.newInstance();
  mapper.setInputData(pd);
  const actor = vtkActor.newInstance();
  actor.setMapper(mapper);
  actor.getProperty().setColor(0.72, 0.76, 0.82);
  actor.getProperty().setSpecular(0.2);
  return actor;
}
