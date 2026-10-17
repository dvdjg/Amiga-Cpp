# `tools/assets` — herramientas de assets

Herramientas de host que convierten assets **fuente** de `assets/` en blobs y cabeceras listos para el engine. Leen de `assets/`, escriben en `out/assets/<pipeline>/` y nunca generan binarios versionados (`docs/STRUCTURE.md` §5–§6).

| Herramienta | Qué hace |
|---|---|
| `gen-manifest.mjs` | Genera `assets.manifest.hpp` desde `assets.manifest.json` (blobs + geometría + `register_assets`). Contrato en `docs/tools/ASSET_MANIFEST.md`. |
| `hud-atlas.mjs` | **Atlas de iconos del HUD** (`eng::hud::IconAtlas`): hoja PNG → cuantización → planar interleaved por filas → `.bin` Chip + `.hpp` + `.json` + preview. Formato en `docs/engine/architecture/HUD_SYSTEM.md` §8.2. |

## `hud-atlas.mjs`

```
node tools/assets/hud-atlas.mjs <sheet.png> [--frame 16x16] [--planes 2]
     [--masked|--no-masked] [--colors N] [--out out/assets/hud/<nombre>]
     [--check] [--selftest]
```

- **Entrada**: hoja PNG con frames en orden lectura (izquierda→derecha, arriba→abajo); `frame_w` múltiplo de 16 (potencia de dos preferente, ≤64×64).
- **Cuantización**: paleta exacta si los colores caben en `2^planes` (o `2^(planes-1)` con máscara); si no, *median-cut* determinista sobre RGB444 (12 bits, la paleta del chipset).
- **Formato de salida** (`--planes N`): un solo bloque de palabras interleaved por filas; `stride_words = (frame_w/16) × frame_h` por plano; offset del frame `f` = `f × planes × stride_words`; **bit 15 = píxel 0** (MSB primero), palabras big-endian. `--masked` usa el plano 0 como máscara (1 = opaco; índice 0 = transparente); `--no-masked` reserva los `planes` bits como color (índice 0 es un color).
- **Salidas** (`out/assets/hud/<nombre>/`): `<nombre>.atlas.bin` (palabras), `<nombre>.atlas.hpp` (`INCBIN_CHIP` + tabla de frames `constexpr` + `make_icon_atlas()`), `<nombre>.atlas.json` (procedencia, geometría, paleta y sha256) y `<nombre>.preview.png` (reconstrucción para revisión visual).
- **Validación**: el propio pipeline decodifica el `.bin` y exige round-trip 100 % índice a índice; `--check` regenera en memoria y compara byte a byte (para CI); `--selftest` fija geometría, bit order y round-trip con un PNG sintético, sin assets externos.

Asset de prueba del pipeline: `assets/amiga/hud/test_icons.png` (4 iconos 16×16 enmascarados).
