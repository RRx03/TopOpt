#!/usr/bin/env bash
# TopOpt — bootstrap of a fresh clone (idempotent).
#   ./setup.sh          build the solver + install the Studio deps
#   ./setup.sh --test   same, then run the full CPU validation suite
set -euo pipefail

cd "$(dirname "$0")"

RUN_TESTS=0
for arg in "$@"; do
  case "$arg" in
    --test) RUN_TESTS=1 ;;
    *) echo "usage: ./setup.sh [--test]" >&2; exit 2 ;;
  esac
done

step() { printf '\n\033[1;34m== %s\033[0m\n' "$*"; }
warn() { printf '\033[1;33mWARNING:\033[0m %s\n' "$*"; }
fail() { printf '\033[1;31mERROR:\033[0m %s\n' "$*" >&2; exit 1; }

# --- 1. environment checks -----------------------------------------------------
step "environment checks"

if [[ "$(uname -s)" != "Darwin" ]]; then
  warn "this project targets macOS (Metal GPU solver) — got $(uname -s)."
elif [[ "$(uname -m)" != "arm64" ]]; then
  warn "this project targets Apple Silicon — got $(uname -m)."
else
  echo "macOS on Apple Silicon: ok"
fi

command -v clang >/dev/null 2>&1 || fail "clang not found — install the Xcode command-line tools: xcode-select --install"
command -v make  >/dev/null 2>&1 || fail "make not found — install the Xcode command-line tools: xcode-select --install"
echo "clang + make: ok ($(clang --version | head -n1))"

command -v node >/dev/null 2>&1 || fail "node not found — install Node.js >= 20 (brew install node, or nodejs.org)"
NODE_MAJOR="$(node -p 'process.versions.node.split(".")[0]')"
[[ "$NODE_MAJOR" -ge 20 ]] || fail "Node.js >= 20 required — found $(node --version) (brew install node, or nodejs.org)"
command -v npm >/dev/null 2>&1 || fail "npm not found — comes with Node.js (brew install node, or nodejs.org)"
echo "node $(node --version) + npm $(npm --version): ok"

# --- 2. solver -------------------------------------------------------------------
step "building the solver (make -j: binaries + shaders.metallib)"
make -j

if [[ "$RUN_TESTS" -eq 1 ]]; then
  step "running the CPU validation suite (make test_cpu)"
  make test_cpu
fi

# --- 3. Studio (web UI) ------------------------------------------------------------
step "installing TopOpt Studio dependencies (web/)"
(cd web && npm install)

# --- 4. recap ------------------------------------------------------------------------
step "done — how to use"
cat <<'EOF'
  Studio (web UI)     cd web && npm run dev          then open http://localhost:5173
  Run server (local)  node server/run-server.mjs     the Studio "Run" panel talks to it
  Remote runs         on the remote machine (same repo, after ./setup.sh):
                        node server/run-server.mjs --host 0.0.0.0
                      on this machine: cp web/.env.example web/.env.local,
                      set VITE_REMOTE_HOST=<remote ip>, restart npm run dev
  Direct CLI          ./build/topopt_run examples/mbb3d.topopt.json
                      -> output/mbb3d/mbb3d.vti (open in ParaView or the Studio)
EOF
