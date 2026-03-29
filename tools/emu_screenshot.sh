#!/usr/bin/env bash
# Launch DuckStation, wait for it to render, screenshot the window, then quit.
# Usage: ./tools/emu_screenshot.sh [seconds] [output_path]
#
# Requires: macOS (screencapture, osascript)

set -euo pipefail

WAIT="${1:-15}"
OUT="${2:-/tmp/emu_screenshot.png}"
PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PROJECT_NAME=$(cat "$PROJECT_ROOT/.project" 2>/dev/null || echo "dice_roller")
BIN="$PROJECT_ROOT/out/${PROJECT_NAME}.bin"
DUCKSTATION="${DUCKSTATION:-/Applications/DuckStation.app/Contents/MacOS/DuckStation}"

if [[ ! -f "$BIN" ]]; then
  echo "Error: $BIN not found. Run 'make build' first." >&2
  exit 1
fi

if [[ ! -f "$DUCKSTATION" ]]; then
  echo "Error: DuckStation not found at $DUCKSTATION" >&2
  exit 1
fi

echo "Launching DuckStation with $BIN ..."
"$DUCKSTATION" -batch -fastboot "$BIN" &
EMU_PID=$!

echo "Waiting ${WAIT}s for emulator to boot ..."
sleep "$WAIT"

# Find the DuckStation window ID via CGWindowListCopyWindowInfo (Swift one-liner)
# This avoids any Python/pyobjc dependency.
WINDOW_ID=$(swift -e '
import CoreGraphics
if let list = CGWindowListCopyWindowInfo(.optionOnScreenOnly, kCGNullWindowID) as? [[String: Any]] {
    for w in list {
        let owner = w[kCGWindowOwnerName as String] as? String ?? ""
        if owner.lowercased().contains("duckstation") || owner.lowercased().contains("duck station") {
            if let wid = w[kCGWindowNumber as String] as? Int, wid > 0 {
                print(wid)
                exit(0)
            }
        }
    }
}
exit(1)
' 2>/dev/null) || true

if [[ -n "$WINDOW_ID" ]]; then
  echo "Capturing window ID $WINDOW_ID ..."
  screencapture -x -o -l "$WINDOW_ID" "$OUT"
else
  echo "Warning: could not find DuckStation window, capturing full screen ..."
  screencapture -x "$OUT"
fi

echo "Quitting DuckStation ..."
kill "$EMU_PID" 2>/dev/null || true
pkill -f "DuckStation.*${PROJECT_NAME}" 2>/dev/null || true
# Give it a moment to shut down cleanly
sleep 1

echo "Screenshot saved to $OUT"
