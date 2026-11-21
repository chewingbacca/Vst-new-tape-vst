#!/usr/bin/env bash
# Run Tracktion pluginval against the built Splinter VST3.
#
#   scripts/run_pluginval.sh [strictness=10] [path/to/Plugin.vst3]
#
# Env:
#   PLUGINVAL_VERSION               pluginval release tag (default v1.0.4)
#   SPLINTER_PLUGINVAL_SKIP_GUI=1   pass --skip-gui-tests
#
# The pluginval binary is downloaded once into tools/pluginval/ (git-ignored).
# Logs go to build/pluginval-logs/. Exits with pluginval's exit code.
set -uo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
STRICTNESS="${1:-10}"
PLUGIN="${2:-$ROOT/build/Splinter_artefacts/Release/VST3/Splinter.vst3}"
VERSION="${PLUGINVAL_VERSION:-v1.0.4}"
TOOLS="$ROOT/tools/pluginval"
LOGDIR="$ROOT/build/pluginval-logs"

if [[ ! -e "$PLUGIN" ]]; then
  echo "run_pluginval: plugin not found: $PLUGIN" >&2
  echo "FAIL (plugin missing)"
  exit 2
fi
# pluginval wants an absolute path.
PLUGIN="$(cd "$(dirname "$PLUGIN")" && pwd)/$(basename "$PLUGIN")"

case "$(uname -s)" in
  Darwin) ASSET="pluginval_macOS.zip"; BIN="$TOOLS/pluginval.app/Contents/MacOS/pluginval" ;;
  Linux)  ASSET="pluginval_Linux.zip"; BIN="$TOOLS/pluginval" ;;
  *) echo "run_pluginval: unsupported OS $(uname -s)" >&2; echo "FAIL (unsupported OS)"; exit 2 ;;
esac

if [[ ! -x "$BIN" ]]; then
  URL="https://github.com/Tracktion/pluginval/releases/download/$VERSION/$ASSET"
  echo "run_pluginval: downloading $URL"
  mkdir -p "$TOOLS"
  if ! curl -fsSL --retry 3 -o "$TOOLS/$ASSET" "$URL"; then
    echo "run_pluginval: download failed" >&2
    echo "FAIL (could not download pluginval)"
    exit 2
  fi
  if ! unzip -oq "$TOOLS/$ASSET" -d "$TOOLS"; then
    echo "run_pluginval: unzip failed" >&2
    echo "FAIL (could not unpack pluginval)"
    exit 2
  fi
  rm -f "$TOOLS/$ASSET"
  chmod +x "$BIN"
fi

mkdir -p "$LOGDIR"

CMD=("$BIN"
     --strictness-level "$STRICTNESS"
     --validate-in-process
     --timeout-ms 600000
     --output-dir "$LOGDIR")
if [[ "${SPLINTER_PLUGINVAL_SKIP_GUI:-0}" == "1" ]]; then
  CMD+=(--skip-gui-tests)
fi
CMD+=(--validate "$PLUGIN")

# Headless Linux: wrap in a virtual X server.
if [[ "$(uname -s)" == "Linux" && -z "${DISPLAY:-}" ]]; then
  if command -v xvfb-run >/dev/null 2>&1; then
    CMD=(xvfb-run -a "${CMD[@]}")
  else
    echo "run_pluginval: no DISPLAY and xvfb-run not installed; forcing --skip-gui-tests" >&2
    CMD+=(--skip-gui-tests)
  fi
fi

echo "run_pluginval: $("$BIN" --version 2>/dev/null | head -1)"
echo "run_pluginval: ${CMD[*]}"
"${CMD[@]}"
RC=$?

if [[ $RC -eq 0 ]]; then
  echo "PASS (pluginval strictness $STRICTNESS)"
else
  echo "FAIL (pluginval strictness $STRICTNESS, exit code $RC)"
fi
exit $RC
