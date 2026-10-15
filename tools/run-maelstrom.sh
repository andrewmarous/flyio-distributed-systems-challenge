#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
if [[ "${1:-}" == "--help" || "${1:-}" == "-h" ]]; then
  echo "Usage: $0 [BAZEL_TARGET [WORKLOAD [MAELSTROM_OPTIONS...]]]"
  echo "Defaults: //apps/c1_echo:echo echo --node-count 1 --time-limit 10"
  exit 0
fi
TARGET="${1:-//apps/c1_echo:echo}"
WORKLOAD="${2:-echo}"
if (( $# > 0 )); then shift; fi
if (( $# > 0 )); then shift; fi

for tool in bazel java dot gnuplot; do
  command -v "$tool" >/dev/null 2>&1 || {
    echo "Missing required tool: $tool (see DEVELOPMENT.md)" >&2; exit 1;
  }
done
java -version >/dev/null 2>&1 || {
  echo "Java is installed but cannot start; see DEVELOPMENT.md" >&2; exit 1;
}
if [[ ! -f "$ROOT/third_party/maelstrom/lib/maelstrom.jar" ]]; then
  echo "Run tools/setup-maelstrom.sh first." >&2
  exit 1
fi

cd "$ROOT"
bazel build "$TARGET"
# Resolve the actual output instead of assuming a particular bazel-bin layout.
BINARY="$(bazel cquery "$TARGET" --output=files)"
[[ -n "$BINARY" && "$BINARY" != *$'\n'* ]] || {
  echo "Target must produce exactly one executable: $TARGET" >&2; exit 1;
}
if [[ "$BINARY" != /* ]]; then BINARY="$ROOT/$BINARY"; fi
[[ -x "$BINARY" ]] || {
  echo "Target output is not executable: $BINARY" >&2; exit 1;
}

# This release has no --store option; its store/ is relative to the working directory.
RUN_DIR="$(mkdir -p "$ROOT/.cache/maelstrom" && mktemp -d "$ROOT/.cache/maelstrom/run-XXXXXX")"
echo "Maelstrom results: $RUN_DIR/store" >&2
cd "$RUN_DIR"
exec java -Djava.awt.headless=true \
  -jar "$ROOT/third_party/maelstrom/lib/maelstrom.jar" test \
  -w "$WORKLOAD" --bin "$BINARY" --node-count 1 --time-limit 10 "$@"
