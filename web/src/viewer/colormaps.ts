// Colormap definitions shared by the vtk.js color transfer functions and the
// HTML scalar-bar legend. Framework-free (piecewise-linear control points).

export type ColormapName = "viridis" | "coolwarm";

export const COLORMAP_NAMES: readonly ColormapName[] = ["viridis", "coolwarm"];

type Stop = readonly [t: number, r: number, g: number, b: number];

// Standard 10-stop viridis ramp (matplotlib / d3-scale-chromatic).
const VIRIDIS: readonly Stop[] = [
  [0.0, 0.267, 0.005, 0.329],
  [0.111, 0.282, 0.157, 0.471],
  [0.222, 0.244, 0.29, 0.537],
  [0.333, 0.192, 0.408, 0.556],
  [0.444, 0.149, 0.51, 0.557],
  [0.556, 0.122, 0.619, 0.537],
  [0.667, 0.208, 0.718, 0.473],
  [0.778, 0.431, 0.808, 0.345],
  [0.889, 0.709, 0.871, 0.169],
  [1.0, 0.993, 0.906, 0.144],
];

// Moreland's diverging cool-warm (5 stops).
const COOLWARM: readonly Stop[] = [
  [0.0, 0.23, 0.299, 0.754],
  [0.25, 0.554, 0.69, 0.996],
  [0.5, 0.865, 0.865, 0.865],
  [0.75, 0.958, 0.603, 0.409],
  [1.0, 0.706, 0.016, 0.15],
];

export const COLORMAPS: Readonly<Record<ColormapName, readonly Stop[]>> = {
  viridis: VIRIDIS,
  coolwarm: COOLWARM,
};

/** Sample a colormap at t in [0,1]; returns rgb in [0,1]. */
export function sampleColormap(name: ColormapName, t: number): [number, number, number] {
  const stops = COLORMAPS[name];
  const x = Math.min(Math.max(t, 0), 1);
  for (let i = 1; i < stops.length; ++i) {
    const [t1, r1, g1, b1] = stops[i]!;
    if (x <= t1) {
      const [t0, r0, g0, b0] = stops[i - 1]!;
      const f = t1 > t0 ? (x - t0) / (t1 - t0) : 0;
      return [r0 + f * (r1 - r0), g0 + f * (g1 - g0), b0 + f * (b1 - b0)];
    }
  }
  const [, r, g, b] = stops[stops.length - 1]!;
  return [r, g, b];
}

/** Draw a horizontal colormap ramp into a canvas (used by the legend). */
export function drawColormap(canvas: HTMLCanvasElement, name: ColormapName): void {
  const ctx = canvas.getContext("2d");
  if (!ctx) return;
  const w = canvas.width;
  const h = canvas.height;
  for (let x = 0; x < w; ++x) {
    const [r, g, b] = sampleColormap(name, w > 1 ? x / (w - 1) : 0);
    ctx.fillStyle = `rgb(${Math.round(255 * r)},${Math.round(255 * g)},${Math.round(255 * b)})`;
    ctx.fillRect(x, 0, 1, h);
  }
}
