/// <reference types="vitest/config" />
import { defineConfig } from "vite";

export default defineConfig({
  base: "./",
  build: { chunkSizeWarningLimit: 1200 },
  test: { environment: "node", include: ["tests/**/*.test.ts"] },
});
