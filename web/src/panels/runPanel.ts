// Run panel (M4): launch build/topopt_run through server/run-server.mjs
// without leaving the browser — local target always, remote target only when
// VITE_REMOTE_HOST is set (web/.env.local, gitignored). Live convergence via
// SSE (iteration counter + last objective), cancel, and one-click loading of
// the produced .vti/.stl into the Results tab (local AND remote: the artifact
// is fetched from the executing machine, no folder digging).

import { checkCompat } from "../spec/compat";
import { specToJson } from "../spec/serialize";
import type { Store } from "../state";

export interface RunPanelHooks {
  /** Open a result file in the Results tab (M3 parsers). */
  openResult(file: File): Promise<void>;
}

interface Artifact {
  name: string;
  size: number;
}

interface LogEvent {
  type: "line" | "exit";
  stream?: "stdout" | "stderr";
  line?: string;
  exitCode?: number | null;
  status?: string;
  artifacts?: Artifact[];
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

function fmtSize(bytes: number): string {
  if (bytes < 1024) return `${bytes} B`;
  if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(1)} kB`;
  return `${(bytes / (1024 * 1024)).toFixed(2)} MB`;
}

// Convergence lines of the topopt_run drivers:
//   elastic     "  it  10 | C=23.2488 | vol=0.3001 | change=0.2000"
//   mass/fluid  "  10   0.4132   1.2345e+00    8  +0.0123  0.031"  (it, objective, …)
function parseIteration(line: string): { it: number; obj: string } | null {
  let m = /^\s*it\s+(\d+)\s*\|\s*([A-Za-z]+\s*=\s*[-+0-9.eE]+)/.exec(line);
  if (m) return { it: Number(m[1]), obj: m[2]!.replace(/\s/g, "") };
  m = /^\s*(\d+)\s+([-+]?\d+(?:\.\d+)?(?:[eE][-+]?\d+)?)(?:\s|$)/.exec(line);
  if (m) return { it: Number(m[1]), obj: `obj=${m[2]}` };
  return null;
}

interface Target {
  id: string;
  label: string;
  host: () => string;
  port: () => string;
  dot: HTMLSpanElement;
}

export function setupRunPanel(
  container: HTMLElement,
  store: Store,
  hooks: RunPanelHooks,
): void {
  container.appendChild(el("h2", undefined, "Run"));

  // --- targets ---------------------------------------------------------------
  const targets: Target[] = [];
  let active = 0;
  const targetBox = el("div", "run-targets");
  container.appendChild(targetBox);

  const addTarget = (
    id: string,
    label: string,
    host: () => string,
    port: () => string,
    extra?: HTMLElement,
  ): void => {
    const row = el("label", "run-target");
    const radio = el("input");
    radio.type = "radio";
    radio.name = "run-target";
    radio.checked = targets.length === 0;
    const dot = el("span", "health-dot");
    dot.title = "état inconnu";
    row.append(radio, dot, el("span", "run-target-label", label));
    if (extra) row.appendChild(extra);
    targetBox.appendChild(row);
    const idx = targets.length;
    targets.push({ id, label, host, port, dot });
    radio.addEventListener("change", () => {
      if (radio.checked) {
        active = idx;
        void checkHealth(targets[idx]!);
      }
    });
  };

  addTarget("local", "Local", () => "127.0.0.1", () => "8787");

  // Remote target: ONLY when VITE_REMOTE_HOST is configured (web/.env.local,
  // gitignored — personal config, invisible to other users of the repo).
  const envHost = (import.meta.env.VITE_REMOTE_HOST as string | undefined) ?? "";
  const envPort = (import.meta.env.VITE_REMOTE_PORT as string | undefined) ?? "8787";
  if (envHost !== "") {
    const wrap = el("span", "run-remote-fields");
    const hostIn = el("input", "run-host");
    hostIn.value = envHost;
    hostIn.title = "hôte distant (VITE_REMOTE_HOST)";
    const portIn = el("input", "run-port");
    portIn.value = envPort;
    portIn.title = "port distant (VITE_REMOTE_PORT)";
    wrap.append(hostIn, portIn);
    addTarget("remote", "Distant", () => hostIn.value.trim(), () => portIn.value.trim(), wrap);
    hostIn.addEventListener("change", () => void checkHealth(targets[1]!));
    portIn.addEventListener("change", () => void checkHealth(targets[1]!));
  }

  const baseUrl = (t: Target): string => `http://${t.host()}:${t.port() || "8787"}`;

  const checkHealth = async (t: Target): Promise<void> => {
    t.dot.className = "health-dot";
    t.dot.title = "vérification…";
    try {
      const ctrl = new AbortController();
      const timer = setTimeout(() => ctrl.abort(), 3000);
      const r = await fetch(`${baseUrl(t)}/api/health`, { signal: ctrl.signal });
      clearTimeout(timer);
      const h = (await r.json()) as { ok?: boolean; version?: string; solver?: boolean };
      const up = r.ok && h.ok === true;
      t.dot.className = `health-dot ${up && h.solver ? "health-up" : "health-warn"}`;
      t.dot.title = up
        ? `serveur ${h.version ?? "?"} — solveur ${h.solver ? "présent" : "ABSENT (./setup.sh)"}`
        : "réponse invalide";
      if (!up) t.dot.className = "health-dot health-down";
    } catch {
      t.dot.className = "health-dot health-down";
      t.dot.title = `injoignable — lancer: node server/run-server.mjs${t.id === "remote" ? " --host 0.0.0.0" : ""}`;
    }
  };

  // Health-check at panel load, for every target.
  for (const t of targets) void checkHealth(t);

  // --- output dir + run/cancel -------------------------------------------------
  const dirRow = el("div", "bc-row");
  dirRow.appendChild(el("span", "run-dir-label", "dossier de sortie"));
  const dirIn = el("input", "run-dir");
  dirIn.value = "output";
  dirIn.title = "relatif au repo de la machine qui exécute";
  dirRow.appendChild(dirIn);
  container.appendChild(dirRow);

  const btnRow = el("div", "bc-row");
  const runBtn = el("button", "btn btn-primary", "Run");
  const cancelBtn = el("button", "btn btn-del", "annuler");
  cancelBtn.style.display = "none";
  btnRow.append(runBtn, cancelBtn);
  container.appendChild(btnRow);

  const progress = el("div", "run-progress");
  progress.style.display = "none";
  container.appendChild(progress);

  const status = el("div", "import-status");
  status.style.display = "none";
  container.appendChild(status);

  const logPre = el("pre", "run-log");
  logPre.style.display = "none";
  container.appendChild(logPre);

  const artBox = el("div", "artifact-list");
  container.appendChild(artBox);

  let running = false;
  let es: EventSource | null = null;
  let jobUrl = ""; // <base>/api/jobs/<id>

  const showStatus = (kind: "ok" | "warn" | "error", msg: string): void => {
    status.className = `import-status import-${kind}`;
    status.textContent = msg;
    status.style.display = "";
  };

  // Same gate as the export button (M2 validation is blocking).
  const refreshRunButton = (): void => {
    const issues = checkCompat(store.spec);
    runBtn.disabled = running || issues.length > 0;
    runBtn.title =
      issues.length > 0 ? "run bloqué par la validation (voir Export)" : "";
  };
  store.on(refreshRunButton);
  refreshRunButton();

  const renderArtifacts = (base: string, artifacts: Artifact[]): void => {
    artBox.textContent = "";
    if (artifacts.length === 0) return;
    artBox.appendChild(el("h3", undefined, "Artefacts"));
    for (const a of artifacts) {
      const row = el("div", "artifact-item");
      const loadable = /\.(vti|stl)$/i.test(a.name);
      const btn = el("button", "btn artifact-btn", a.name);
      btn.disabled = !loadable;
      btn.title = loadable
        ? "charger dans l'onglet Résultats"
        : `${base}/artifacts/${encodeURIComponent(a.name)}`;
      row.append(btn, el("span", "artifact-size", fmtSize(a.size)));
      artBox.appendChild(row);
      if (!loadable) continue;
      btn.addEventListener("click", () => {
        btn.disabled = true;
        void fetch(`${base}/artifacts/${encodeURIComponent(a.name)}`)
          .then((r) => {
            if (!r.ok) throw new Error(`HTTP ${r.status}`);
            return r.blob();
          })
          .then((blob) => hooks.openResult(new File([blob], a.name)))
          .catch((err: unknown) => {
            showStatus("error", `${a.name}: ${err instanceof Error ? err.message : String(err)}`);
          })
          .finally(() => (btn.disabled = false));
      });
    }
  };

  const finishRun = (): void => {
    running = false;
    es?.close();
    es = null;
    cancelBtn.style.display = "none";
    refreshRunButton();
  };

  runBtn.addEventListener("click", () => {
    if (running || checkCompat(store.spec).length > 0) return;
    const t = targets[active]!;
    const base = baseUrl(t);
    const maxIter = store.spec.max_iter;
    const body = { ...specToJson(store.spec), outputDir: dirIn.value.trim() || "output" };

    running = true;
    refreshRunButton();
    cancelBtn.style.display = "";
    logPre.textContent = "";
    logPre.style.display = "";
    artBox.textContent = "";
    progress.style.display = "";
    progress.textContent = `lancement sur ${t.label.toLowerCase()}…`;
    status.style.display = "none";

    void fetch(`${base}/api/run`, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(body),
    })
      .then(async (r) => {
        const j = (await r.json()) as { jobId?: string; error?: string; dir?: string };
        if (!r.ok || !j.jobId) throw new Error(j.error ?? `HTTP ${r.status}`);
        jobUrl = `${base}/api/jobs/${j.jobId}`;
        showStatus("ok", `job ${j.jobId} → ${j.dir ?? "?"}/`);
        watchLog(maxIter);
        void checkHealth(t);
      })
      .catch((err: unknown) => {
        finishRun();
        progress.style.display = "none";
        showStatus("error", `run refusé: ${err instanceof Error ? err.message : String(err)}`);
        void checkHealth(t);
      });
  });

  const watchLog = (maxIter: number): void => {
    es = new EventSource(`${jobUrl}/log`);
    es.onmessage = (msg: MessageEvent<string>) => {
      const ev = JSON.parse(msg.data) as LogEvent;
      if (ev.type === "line" && ev.line !== undefined) {
        logPre.textContent += ev.line + "\n";
        logPre.scrollTop = logPre.scrollHeight;
        const p = parseIteration(ev.line);
        if (p) progress.textContent = `it ${p.it}/${maxIter} · ${p.obj}`;
        return;
      }
      if (ev.type === "exit") {
        finishRun();
        const ok = ev.exitCode === 0;
        progress.textContent = ok ? "terminé ✓" : `terminé — ${ev.status} (exit ${ev.exitCode})`;
        showStatus(
          ok ? "ok" : "error",
          ok
            ? `run terminé — ${ev.artifacts?.length ?? 0} artefact(s)`
            : `run ${ev.status} (exit ${ev.exitCode})`,
        );
        renderArtifacts(jobUrl, ev.artifacts ?? []);
      }
    };
    es.onerror = () => {
      if (!running) return; // normal close after the exit event
      finishRun();
      showStatus("error", "flux de log interrompu (serveur arrêté ?)");
    };
  };

  cancelBtn.addEventListener("click", () => {
    if (!running || jobUrl === "") return;
    cancelBtn.disabled = true;
    void fetch(jobUrl, { method: "DELETE" })
      .catch(() => undefined)
      .finally(() => (cancelBtn.disabled = false));
  });
}
