#!/usr/bin/env bash
# Genera los blobs de arte/audio de la demo y el header del manifiesto.
set -euo pipefail
node tools/assets/gen-caverna.mjs
node tools/assets/gen-manifest.mjs games/500_caverna/src/assets.manifest.json
