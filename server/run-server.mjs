#!/usr/bin/env node
// TopOpt Studio run server — launches build/topopt_run on this machine from
// the web UI (local or remote: same server started with --host 0.0.0.0 on a
// machine that has cloned + built the repo).
//
// Node native only (http/fs/child_process), zero npm dependency.
//
//   node server/run-server.mjs [--host 0.0.0.0] [--port 8787]
//
// API (CORS open — the UI runs on vite:5173, possibly on another machine):
//   GET    /api/health                     { ok, version, solver }
//   POST   /api/run                        body = .topopt.json (+ outputDir?)
//                                          -> { jobId } | 409 busy | 503 no solver
//   GET    /api/jobs/:id                   job state (running/done/failed/canceled)
//   GET    /api/jobs/:id/log               SSE: {type:"line"...} then {type:"exit"...}
//   DELETE /api/jobs/:id                   kill the running job
//   GET    /api/jobs/:id/artifacts/<file>  serve a produced file (no path escape)
//
// One run at a time (the solver saturates the CPU/GPU); jobs are in memory.

import { execFileSync, spawn } from "node:child_process";
import fs from "node:fs";
import http from "node:http";
import path from "node:path";
import { fileURLToPath } from "node:url";

const REPO_ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const SOLVER = path.join(REPO_ROOT, "build", "topopt_run");
const BODY_LIMIT = 8 * 1024 * 1024;

// --- CLI ----------------------------------------------------------------------

function parseArgs(argv) {
  const opts = { host: "127.0.0.1", port: 8787 };
  for (let i = 0; i < argv.length; ++i) {
    if (argv[i] === "--host" && argv[i + 1]) opts.host = argv[++i];
    else if (argv[i] === "--port" && argv[i + 1]) opts.port = Number(argv[++i]);
    else {
      console.error(`unknown argument: ${argv[i]}`);
      console.error("usage: node server/run-server.mjs [--host 0.0.0.0] [--port 8787]");
      process.exit(2);
    }
  }
  if (!Number.isInteger(opts.port) || opts.port < 0 || opts.port > 65535) {
    console.error(`invalid port: ${opts.port}`);
    process.exit(2);
  }
  return opts;
}

// --- version (git describe, resolved once at startup) --------------------------

let VERSION = "unknown";
try {
  VERSION = execFileSync("git", ["describe", "--always", "--dirty", "--tags"], {
    cwd: REPO_ROOT,
  })
    .toString()
    .trim() || "unknown";
} catch {
  /* not a git checkout — keep "unknown" */
}

// --- jobs ----------------------------------------------------------------------

/** @type {Map<string, Job>} */
const jobs = new Map();
/** @type {Job | null} */
let currentJob = null;
let jobSeq = 0;

function timestamp() {
  const d = new Date();
  const p = (n, w = 2) => String(n).padStart(w, "0");
  return (
    `${d.getFullYear()}${p(d.getMonth() + 1)}${p(d.getDate())}-` +
    `${p(d.getHours())}${p(d.getMinutes())}${p(d.getSeconds())}`
  );
}

function publicJob(job) {
  return {
    jobId: job.id,
    name: job.name,
    status: job.status,
    exitCode: job.exitCode,
    dir: path.relative(REPO_ROOT, job.dir),
    startedAt: job.startedAt,
    endedAt: job.endedAt,
    artifacts: job.artifacts,
  };
}

function listArtifacts(dir) {
  try {
    return fs
      .readdirSync(dir, { withFileTypes: true })
      .filter((e) => e.isFile())
      .map((e) => ({ name: e.name, size: fs.statSync(path.join(dir, e.name)).size }))
      .sort((a, b) => a.name.localeCompare(b.name));
  } catch {
    return [];
  }
}

// The solver fully buffers stdout when piped (no line flush) — run it under a
// pty via script(1) so convergence lines stream live. Fallback: direct spawn
// (chunky output, still complete at exit). The pty merges stderr into stdout.
const HAS_SCRIPT = fs.existsSync("/usr/bin/script");

function spawnSolver(specFile) {
  if (HAS_SCRIPT)
    return spawn("/usr/bin/script", ["-q", "/dev/null", SOLVER, specFile], {
      cwd: REPO_ROOT,
      stdio: ["ignore", "pipe", "pipe"],
      detached: true, // own process group: kill(-pid) reaches the solver too
    });
  return spawn(SOLVER, [specFile], {
    cwd: REPO_ROOT,
    stdio: ["ignore", "pipe", "pipe"],
    detached: true,
  });
}

// pty artifacts: \r line endings, and a leading "^D\b\b" echoed by script(1).
function cleanLine(line) {
  return line.replace(/\r+$/, "").replace(/\^D/g, "").replace(/[\b]/g, "");
}

function broadcast(job, event) {
  const msg = `data: ${JSON.stringify(event)}\n\n`;
  for (const res of job.subscribers) res.write(msg);
}

function pushLine(job, stream, line) {
  const clean = cleanLine(line);
  if (clean.trim() === "") return;
  const event = { type: "line", stream, line: clean };
  job.lines.push(event);
  broadcast(job, event);
}

function attachStream(job, readable, stream) {
  let buf = "";
  readable.on("data", (chunk) => {
    buf += chunk.toString();
    const parts = buf.split("\n");
    buf = parts.pop() ?? "";
    for (const line of parts) pushLine(job, stream, line);
  });
  readable.on("end", () => {
    if (buf) pushLine(job, stream, buf);
  });
}

function startJob(spec, outputDir) {
  const name = String(spec?.meta?.name ?? "");
  if (!/^[A-Za-z0-9._-]+$/.test(name))
    throw Object.assign(new Error("meta.name must be a filesystem-safe identifier"), {
      status: 400,
    });

  const base = path.resolve(REPO_ROOT, outputDir);
  if (base !== REPO_ROOT && !base.startsWith(REPO_ROOT + path.sep))
    throw Object.assign(new Error("outputDir must stay inside the repo"), { status: 400 });

  let dir = path.join(base, `${name}-${timestamp()}`);
  for (let n = 2; fs.existsSync(dir); ++n) dir = `${dir.replace(/-\d+$/, "")}-${n}`;
  fs.mkdirSync(dir, { recursive: true });

  // Traceability: the exact spec of the run is archived with its results.
  const runSpec = { ...spec, output: { ...(spec.output ?? {}), dir: path.relative(REPO_ROOT, dir) } };
  const specFile = path.join(dir, `${name}.topopt.json`);
  fs.writeFileSync(specFile, JSON.stringify(runSpec, null, 2) + "\n");

  const id = `job-${++jobSeq}-${Date.now().toString(36)}`;
  const job = {
    id,
    name,
    dir,
    status: "running",
    exitCode: null,
    artifacts: [],
    lines: [],
    subscribers: new Set(),
    proc: spawnSolver(specFile),
    canceled: false,
    startedAt: new Date().toISOString(),
    endedAt: null,
  };
  jobs.set(id, job);
  currentJob = job;

  attachStream(job, job.proc.stdout, "stdout");
  attachStream(job, job.proc.stderr, "stderr");

  job.proc.on("error", (err) => {
    pushLine(job, "stderr", `spawn failed: ${err.message}`);
  });
  job.proc.on("close", (code, signal) => {
    job.exitCode = code;
    job.status = job.canceled ? "canceled" : code === 0 ? "done" : "failed";
    job.endedAt = new Date().toISOString();
    job.artifacts = listArtifacts(job.dir);
    if (currentJob === job) currentJob = null;
    broadcast(job, {
      type: "exit",
      exitCode: code,
      signal: signal ?? null,
      status: job.status,
      artifacts: job.artifacts,
    });
    for (const res of job.subscribers) res.end();
    job.subscribers.clear();
    console.log(`[${job.id}] ${job.status} (exit=${code}) — ${job.artifacts.length} artifact(s)`);
  });

  console.log(`[${job.id}] running ${name} -> ${path.relative(REPO_ROOT, dir)}/`);
  return job;
}

function killJob(job) {
  job.canceled = true;
  try {
    process.kill(-job.proc.pid, "SIGTERM"); // whole group (script + solver)
  } catch {
    try {
      job.proc.kill("SIGTERM");
    } catch {
      /* already gone */
    }
  }
}

// --- HTTP ------------------------------------------------------------------------

const CORS = {
  "Access-Control-Allow-Origin": "*",
  "Access-Control-Allow-Methods": "GET, POST, DELETE, OPTIONS",
  "Access-Control-Allow-Headers": "Content-Type",
};

function sendJson(res, status, obj) {
  res.writeHead(status, { "Content-Type": "application/json", ...CORS });
  res.end(JSON.stringify(obj) + "\n");
}

function readBody(req) {
  return new Promise((resolve, reject) => {
    let size = 0;
    const chunks = [];
    req.on("data", (c) => {
      size += c.length;
      if (size > BODY_LIMIT) {
        reject(Object.assign(new Error("body too large"), { status: 413 }));
        req.destroy();
        return;
      }
      chunks.push(c);
    });
    req.on("end", () => resolve(Buffer.concat(chunks).toString()));
    req.on("error", reject);
  });
}

const CONTENT_TYPES = {
  ".vti": "application/xml", // VTK XML ImageData
  ".vtu": "application/xml",
  ".stl": "model/stl",
  ".png": "image/png",
  ".json": "application/json",
  ".txt": "text/plain",
  ".csv": "text/csv",
};

function serveArtifact(res, job, rawName) {
  let name;
  try {
    name = decodeURIComponent(rawName);
  } catch {
    return sendJson(res, 400, { error: "bad artifact name" });
  }
  // Path-traversal protection: resolve, then require the job dir prefix.
  const file = path.resolve(job.dir, name);
  if (!file.startsWith(job.dir + path.sep))
    return sendJson(res, 403, { error: "artifact path escapes the job directory" });
  let st;
  try {
    st = fs.statSync(file);
  } catch {
    return sendJson(res, 404, { error: "artifact not found" });
  }
  if (!st.isFile()) return sendJson(res, 404, { error: "artifact not found" });
  res.writeHead(200, {
    "Content-Type": CONTENT_TYPES[path.extname(file).toLowerCase()] ?? "application/octet-stream",
    "Content-Length": st.size,
    ...CORS,
  });
  fs.createReadStream(file).pipe(res);
}

function serveLog(res, job) {
  res.writeHead(200, {
    "Content-Type": "text/event-stream",
    "Cache-Control": "no-cache",
    Connection: "keep-alive",
    ...CORS,
  });
  // Replay history, then live-stream.
  for (const ev of job.lines) res.write(`data: ${JSON.stringify(ev)}\n\n`);
  if (job.status !== "running") {
    res.write(
      `data: ${JSON.stringify({
        type: "exit",
        exitCode: job.exitCode,
        signal: null,
        status: job.status,
        artifacts: job.artifacts,
      })}\n\n`,
    );
    res.end();
    return;
  }
  job.subscribers.add(res);
  res.on("close", () => job.subscribers.delete(res));
}

async function handle(req, res) {
  const url = new URL(req.url, "http://x");
  const p = url.pathname;

  if (req.method === "OPTIONS") {
    res.writeHead(204, CORS);
    return res.end();
  }

  if (req.method === "GET" && p === "/api/health")
    return sendJson(res, 200, { ok: true, version: VERSION, solver: fs.existsSync(SOLVER) });

  if (req.method === "POST" && p === "/api/run") {
    if (!fs.existsSync(SOLVER))
      return sendJson(res, 503, {
        error: "build/topopt_run introuvable — lancer ./setup.sh sur cette machine",
      });
    if (currentJob)
      return sendJson(res, 409, {
        error: "un run est déjà en cours (le solveur sature la machine)",
        job: publicJob(currentJob),
      });
    let spec;
    try {
      spec = JSON.parse(await readBody(req));
    } catch (err) {
      return sendJson(res, err.status ?? 400, { error: `invalid JSON body: ${err.message}` });
    }
    if (typeof spec !== "object" || spec === null || Array.isArray(spec))
      return sendJson(res, 400, { error: "body must be a .topopt.json object" });
    const outputDir =
      typeof spec.outputDir === "string" && spec.outputDir.trim() !== ""
        ? spec.outputDir.trim()
        : "output";
    delete spec.outputDir; // transport-only field, not part of the archived spec
    try {
      const job = startJob(spec, outputDir);
      return sendJson(res, 200, { jobId: job.id, dir: path.relative(REPO_ROOT, job.dir) });
    } catch (err) {
      return sendJson(res, err.status ?? 500, { error: err.message });
    }
  }

  const m = p.match(/^\/api\/jobs\/([^/]+)(?:\/(log|artifacts)(?:\/(.+))?)?$/);
  if (m) {
    const job = jobs.get(m[1]);
    if (!job) return sendJson(res, 404, { error: `unknown job ${m[1]}` });
    if (req.method === "GET" && m[2] === undefined) return sendJson(res, 200, publicJob(job));
    if (req.method === "DELETE" && m[2] === undefined) {
      if (job.status !== "running")
        return sendJson(res, 409, { error: `job is ${job.status}, not running` });
      killJob(job);
      return sendJson(res, 200, { ok: true, jobId: job.id });
    }
    if (req.method === "GET" && m[2] === "log") return serveLog(res, job);
    if (req.method === "GET" && m[2] === "artifacts" && m[3]) return serveArtifact(res, job, m[3]);
  }

  return sendJson(res, 404, { error: `no route ${req.method} ${p}` });
}

// --- main --------------------------------------------------------------------------

const { host, port } = parseArgs(process.argv.slice(2));
const server = http.createServer((req, res) => {
  handle(req, res).catch((err) => {
    console.error(err);
    if (!res.headersSent) sendJson(res, 500, { error: err.message });
    else res.end();
  });
});

server.listen(port, host, () => {
  const a = server.address();
  console.log(`topopt run server listening on http://${a.address}:${a.port}`);
  console.log(`  repo   : ${REPO_ROOT}`);
  console.log(`  solver : ${SOLVER} ${fs.existsSync(SOLVER) ? "(ok)" : "(MISSING — run ./setup.sh)"}`);
});

process.on("SIGINT", () => {
  if (currentJob) killJob(currentJob);
  server.close(() => process.exit(0));
  setTimeout(() => process.exit(0), 500).unref();
});
