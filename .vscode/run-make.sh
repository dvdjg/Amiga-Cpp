#!/usr/bin/env bash
# Run gnumake with the Amiga Debug extension toolchain (Git Bash / Cursor terminal).
if [ -z "$AMIGA_BIN_PATH" ]; then
  echo "run-make.sh: AMIGA_BIN_PATH no está definido (usa la tarea VS Code o exporta la ruta del extension bin)" >&2
  exit 1
fi
path="${AMIGA_BIN_PATH//\\/\/}"
path="/${path:0:1}/${path:3}"
export PATH="$path/opt/bin:$path:$PATH"
export AMIGA_BIN_PATH="$AMIGA_BIN_PATH"
if [ -x "$path/gnumake.exe" ]; then
  exec "$path/gnumake.exe" "$@"
fi
exec gnumake "$@"
