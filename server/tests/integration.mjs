#!/usr/bin/env node
// End-to-end integration test of server/run-server.mjs (M4 acceptance):
// start the server, POST the mbb3d example spec, follow the SSE log, check the
// artifact list, GET the .vti, and verify the run left `git status` untouched.
// Skips cleanly (exit 0) when build/topopt_run is missing.
//
//   node server/tests/integration.mjs

import assert from "node:assert/strict";
import { execFileSync, spawn } from "node:child_process";
import fs from "node:fs";
import http from "node:http";
import path from "node:path";
import { fileURLToPath } from "node:url";

const REPO_ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..", "..");
const SOLVER = path.join(REPO_ROOT, "build", "topopt_run");
const PORT = Number(process.env.TOPOPT_TEST_PORT ?? 8797);
const BASE = `http://127.0.0.1:${PORT}`;

if (!fs.existsSync(SOLVER)) {
  console.log("SKIP: build/topopt_run missing — run ./setup.sh first");
  process.exit(0);
}

function req(method, urlPath, body) {
  return new Promise((resolve, reject) => {
    const r = http.request(`${BASE}${urlPath}`, { method }, (res) => {
      let data = "";
      res.on("data", (c) => (data += c));
      res.on("end", () => {
        let json = null;
        try {
          json = JSON.parse(data);
        } catch {
          /* raw body (artifacts) */
        }
        resolve({ status: res.statusCode, json, raw: data, headers: res.headers });
      });
    });
    r.on("error", reject);
    if (body !== undefined) {
      r.setHeader("Content-Type", "application/json");
      r.write(JSON.stringify(body));
    }
    r.end();
  });
}

/** Follow the SSE log until the exit event; returns { lines, exit }. */
function followLog(jobId, timeoutMs) {
  return new Promise((resolve, reject) => {
    const timer = setTimeout(
      () => reject(new Error(`SSE: no exit event within ${timeoutMs / 1000}s`)),
      timeoutMs,
    );
    const r = http.get(`${BASE}/api/jobs/${jobId}/log`, (res) => {
      assert.equal(res.statusCode, 200);
      assert.match(String(res.headers["content-type"]), /^text\/event-stream/);
      const lines = [];
      let buf = "";
      res.on("data", (chunk) => {
        buf += chunk.toString();
        let idx;
        while ((idx = buf.indexOf("\n\n")) !== -1) {
          const frame = buf.slice(0, idx);
          buf = buf.slice(idx + 2);
          const data = frame
            .split("\n")
            .filter((l) => l.startsWith("data: "))
            .map((l) => l.slice(6))
            .join("");
          if (data === "") continue;
          const ev = JSON.parse(data);
          if (ev.type === "line") lines.push(ev.line);
          else if (ev.type === "exit") {
            clearTimeout(timer);
            r.destroy();
            resolve({ lines, exit: ev });
            return;
          }
        }
      });
      res.on("end", () => {
        clearTimeout(timer);
        reject(new Error("SSE stream ended without an exit event"));
      });
    });
    r.on("error", (e) => {
      clearTimeout(timer);
      reject(e);
    });
  });
}

const gitStatus = () =>
  execFileSync("git", ["status", "--short"], { cwd: REPO_ROOT }).toString();

async function main() {
  const statusBefore = gitStatus();

  // --- start the server -------------------------------------------------------
  const server = spawn("node", [path.join(REPO_ROOT, "server", "run-server.mjs"), "--port", String(PORT)], {
    cwd: REPO_ROOT,
    stdio: ["ignore", "pipe", "pipe"],
  });
  process.on("exit", () => server.kill("SIGTERM"));
  await new Promise((resolve, reject) => {
    server.stdout.on("data", (d) => {
      if (d.toString().includes("listening")) resolve();
    });
    server.on("exit", (c) => reject(new Error(`server exited early (code ${c})`)));
    setTimeout(() => reject(new Error("server did not start within 5s")), 5000);
  });

  // --- health -----------------------------------------------------------------
  const health = await req("GET", "/api/health");
  assert.equal(health.status, 200);
  assert.equal(health.json.ok, true);
  assert.equal(health.json.solver, true);
  assert.ok(typeof health.json.version === "string" && health.json.version.length > 0);
  console.log(`ok  health: version=${health.json.version} solver=true`);

  // --- 404 + path guards --------------------------------------------------------
  assert.equal((await req("GET", "/api/jobs/nope")).status, 404);
  console.log("ok  unknown job -> 404");

  // --- run mbb3d ------------------------------------------------------------------
  const spec = JSON.parse(
    fs.readFileSync(path.join(REPO_ROOT, "examples", "mbb3d.topopt.json"), "utf8"),
  );
  const run = await req("POST", "/api/run", { ...spec, outputDir: "output" });
  assert.equal(run.status, 200, `POST /api/run -> ${run.status}: ${run.raw}`);
  const { jobId } = run.json;
  assert.ok(jobId, "jobId returned");
  assert.match(run.json.dir, /^output\/mbb3d-\d{8}-\d{6}/);
  console.log(`ok  POST /api/run -> ${jobId} (${run.json.dir}/)`);

  // busy guard: a second run while this one is live must 409
  const busy = await req("POST", "/api/run", spec);
  assert.equal(busy.status, 409);
  assert.equal(busy.json.job.jobId, jobId);
  console.log("ok  concurrent run -> 409 with current job info");

  // --- SSE log until completion ---------------------------------------------------
  const { lines, exit } = await followLog(jobId, 15 * 60 * 1000);
  assert.equal(exit.exitCode, 0, `solver exit ${exit.exitCode}\n${lines.join("\n")}`);
  assert.ok(lines.some((l) => /^\s*it\s+\d+\s*\|\s*C=/.test(l)), "iteration lines seen");
  console.log(`ok  SSE: ${lines.length} lines, exit 0, last: "${lines[lines.length - 1]}"`);

  // --- artifacts --------------------------------------------------------------------
  const names = exit.artifacts.map((a) => a.name);
  assert.ok(names.includes("mbb3d.vti"), `artifacts: ${names.join(", ")}`);
  assert.ok(names.includes("mbb3d.stl"), "stl artifact present");
  assert.ok(names.includes("mbb3d.topopt.json"), "archived spec present");
  assert.ok(exit.artifacts.every((a) => a.size > 0), "artifact sizes > 0");

  const job = await req("GET", `/api/jobs/${jobId}`);
  assert.equal(job.json.status, "done");
  assert.deepEqual(job.json.artifacts, exit.artifacts);
  console.log(`ok  artifacts: ${exit.artifacts.map((a) => `${a.name} (${a.size} B)`).join(", ")}`);

  const vti = await req("GET", `/api/jobs/${jobId}/artifacts/mbb3d.vti`);
  assert.equal(vti.status, 200);
  assert.equal(vti.headers["content-type"], "application/xml");
  assert.ok(vti.raw.includes("<VTKFile"), ".vti payload is VTK XML");
  console.log(`ok  GET .vti: ${vti.raw.length} bytes of VTK XML`);

  // path traversal must be rejected (403/404, never a file outside the job dir)
  const evil = await req("GET", `/api/jobs/${jobId}/artifacts/..%2F..%2FMakefile`);
  assert.ok(evil.status === 403 || evil.status === 404, `traversal -> ${evil.status}`);
  assert.ok(!String(evil.raw).includes("topopt_run:"), "no repo file leaked");
  console.log(`ok  path traversal rejected (${evil.status})`);

  // --- git hygiene ---------------------------------------------------------------------
  const statusAfter = gitStatus();
  assert.equal(
    statusAfter,
    statusBefore,
    `the run changed git status:\nbefore:\n${statusBefore}\nafter:\n${statusAfter}`,
  );
  console.log("ok  git status unchanged by the run");

  server.kill("SIGTERM");
  console.log("\nPASS: run-server integration (health, run, 409, SSE, artifacts, traversal, git)");
}

main().catch((err) => {
  console.error(`FAIL: ${err.message}`);
  process.exit(1);
});
