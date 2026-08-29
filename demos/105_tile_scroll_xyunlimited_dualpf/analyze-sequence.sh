#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
DEMO="demos/105_tile_scroll_xyunlimited_dualpf"
SEQ="$ROOT/out/run/105_tile_scroll_xyunlimited_dualpf/sequence"

"$ROOT/tools/run/run-demo.sh" "$DEMO" --settle-ms 500 --sequence-frames 720 --sequence-interval-ms 20 "${1:-}"
node "$ROOT/tools/analyze/analyze_105_tile_visibility.mjs" "$SEQ"
"$ROOT/tools/analyze/analyze-frame-sequence.sh" "$SEQ" --expect-animated
echo "OK 105_tile_scroll_xyunlimited_dualpf sequence"
