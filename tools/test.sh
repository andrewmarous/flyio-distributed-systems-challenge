#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
usage() {
  echo "Usage: $0 --challenge|-c [1|2|3|4|5|6]"
  echo "Run a challenge's full Maelstrom integration test. Challenges 1 through 3 are implemented."
}

fail() {
  echo "Error: $*" >&2
  usage >&2
  exit 2
}

CHALLENGE=""
while (( $# > 0 )); do
  case "$1" in
    --challenge|-c)
      [[ $# -ge 2 ]] || fail "$1 requires a challenge number"
      [[ -z "$CHALLENGE" ]] || fail "challenge specified more than once"
      CHALLENGE="$2"
      shift 2
      ;;
    --help|-h)
      usage
      exit 0
      ;;
    *) fail "unknown argument: $1" ;;
  esac
done

case "$CHALLENGE" in
  1)
    TARGET="//apps/c1_echo:echo"
    WORKLOAD="echo"
    TEST_ARGS=(--node-count 1 --time-limit 10)
    ;;
  2)
    TARGET="//apps/c2_unique_ids:unique_ids"
    WORKLOAD="unique-ids"
    TEST_ARGS=(--time-limit 30 --rate 1000 --node-count 3 --availability total --nemesis partition)
    ;;
  3)
    TARGET="//apps/c3_broadcast:broadcast"
    WORKLOAD="broadcast"
    TEST_ARGS=(--node-count 1 --time-limit 20 --rate 10)
    ;;
  4|5|6) fail "challenge $CHALLENGE is not implemented yet" ;;
  "") fail "--challenge/-c is required" ;;
  *) fail "invalid challenge '$CHALLENGE'; expected 1 through 6" ;;
esac

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

run_maelstrom() {
  # This release has no --store option; its store/ is relative to the working directory.
  local run_dir
  run_dir="$(mkdir -p "$ROOT/.cache/maelstrom" && mktemp -d "$ROOT/.cache/maelstrom/run-XXXXXX")"
  echo "Maelstrom results: $run_dir/store" >&2
  (
    cd "$run_dir"
    java -Djava.awt.headless=true \
      -jar "$ROOT/third_party/maelstrom/lib/maelstrom.jar" test \
      -w "$WORKLOAD" --bin "$BINARY" "$@"
  ) 2>&1 | tee "$run_dir/output.log"

  if [[ "$CHALLENGE" == 3 ]]; then
    # Maelstrom prints blank lines after its final verdict.
    local verdict
    verdict="$(awk 'NF { line = $0 } END { print line }' "$run_dir/output.log")"
    if [[ "$verdict" != 'Everything looks good! ヽ(‘ー`)ノ' ]]; then
      echo "Error: broadcast test did not end with the exact success verdict." >&2
      return 1
    fi
  fi
}

run_maelstrom "${TEST_ARGS[@]}"
if [[ "$CHALLENGE" == 3 ]]; then
  run_maelstrom --node-count 5 --time-limit 20 --rate 10
fi
