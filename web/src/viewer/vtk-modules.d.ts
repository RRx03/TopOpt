// Local typings for the @kitware/vtk.js entry points that ship without .d.ts
// (the rendering profiles are side-effect-only registrations; ImageMarchingCubes
// has an implementation but no declaration file). Only the surface actually
// used by vtk.ts is declared — everything else keeps the official typings.

declare module "@kitware/vtk.js/Rendering/OpenGL/Profiles/Geometry";
declare module "@kitware/vtk.js/Rendering/OpenGL/Profiles/Volume";

declare module "@kitware/vtk.js/Filters/General/ImageMarchingCubes" {
  import type { vtkAlgorithm, vtkObject } from "@kitware/vtk.js/interfaces";

  export interface vtkImageMarchingCubes extends vtkObject, vtkAlgorithm {
    setContourValue(value: number): boolean;
    getContourValue(): number;
    setComputeNormals(computeNormals: boolean): boolean;
    setMergePoints(mergePoints: boolean): boolean;
  }

  export interface IImageMarchingCubesInitialValues {
    contourValue?: number;
    computeNormals?: boolean;
    mergePoints?: boolean;
  }

  export function newInstance(
    initialValues?: IImageMarchingCubesInitialValues,
  ): vtkImageMarchingCubes;

  const vtkImageMarchingCubes: { newInstance: typeof newInstance };
  export default vtkImageMarchingCubes;
}
