# Pipeline de tiles EHB para el mapa de la demo 201

Este documento describe, de forma verificada y reproducible, cómo se genera el mapa
EHB que dibuja `demos/201_ehb_map` a partir de un bitmap de tiles genérico: qué
scripts de `tools/ehb` intervienen, con qué argumentos se llamaron, qué producen
cada uno y cómo esos datos se incrustan en el programa. El objetivo del pipeline es
que el Amiga **no transforme píxeles en CPU**: todo lo que se incbina (banco,
paleta y tabla de índices) sale ya en la convención que consume el chipset EHB
(regla 7 de `docs/roadmap/REGLAS_PIPELINE_TILES.md`).

## 1. Qué queremos y de dónde partimos

```
 Beginning Fields.png              cuantización EHB       slicing + dedupe + índice
 (The Fan-tasy Tileset, 640x640) ───────────────► 32 bases + half ───────────────► tilebank
              16x16 tiles, 40x40                                                   (banco + tabla)
```

- **Origen**: `C:/Users/dvdjg/Documents/programa/Assets/2D/The Fan-tasy Tileset (Free)/Tiled/Tilemaps/Beginning Fields.png`.
  Es un atlas de 640×640 píxeles: 40×40 celdas de 16×16. Vive **fuera del repo**
  (carpeta de Assets); no perder la ruta si se reubica el equipo.
- **Sistema de destino**: Amiga OCS, chipset **EHB** (HalfBrite): 6 bitplanes, 32
  registros de color base y los 32 "half" (base/2) generados por hardware. Índice de
  píxel de 6 bits: `0..31` = base, `32..63` = half (bit 5 = plano 6). Es la convención
  llamada **BASES-PRIMERO**.
- **Representación física**: banco de bloques X-Limited e interleaved (320 píxeles de
  ancho por planelínea) listo para el Blitter, más el tilebank indexado crudo.

## 2. Scripts de `tools/ehb` y qué hace cada uno

| Script | Entrada | Salida | Rol |
|---|---|---|---|
| `quantize-ehb.mjs` | PNG original | `palette.json` (32 bases RGB), `ehb_palette.h`, `ehb_preview.png` | Elige 32 bases con k-means **half-aware** ordenadas por luminosidad; reserva base 0 para transparencia si hace falta |
| `slice-tiles.mjs` | PNG + `palette.json` | `tilebank_indexed.h`, `tilebank.raw.bin`, `tiles.json`, `reconstruct.png`, `tilebank.png` | Cuantiza el original a EHB, extrae tiles únicos exactos, (fusión opcional), reconstruye y **compara al 100%**, exporta en BASES-PRIMERO |
| `emit-const-201.mjs` | `tilebank_indexed.h` + `tiles.json` | `const_game_201.h` | Renombrado determinista: `kEhbPalette[64·3]` + `kRenderMap[1600]` + externs del banco incbin |
| `emit-xlimited-bank.mjs` | `tilebank.raw.bin` | `tilebank.xlimited.bin` + `tilebank.xlimited.h` | Convierte el banco indexado al banco de bloques interleaved 320 px que dibuja el algoritmo X-Limited |
| `test-expindex.mjs` | — | assert host | Verifica que la permutación `expIndex` (intercalado → BASES-PRIMERO) es biyectiva |
| `parse-tmx.mjs` / `gid-to-bank.mjs` | `.tmx` de Tiled (JSON) | `tmx_bf.json`, `mapa_ehb.h` (opcional) | Ruta alternativa de edición de niveles. La demo 201 actual NO la usa: el mapa sale directo del PNG |

## 3. Comandos exactos (regeneración verificada)

El bloque canónico actual de la 201 (mismos comandos que `REGLAS_PIPELINE_TILES.md`):

```bash
SOURCE="C:/Users/dvdjg/Documents/programa/Assets/2D/The Fan-tasy Tileset (Free)/Tiled/Tilemaps/Beginning Fields.png"

# 0) Aserto host: la permutación de export es biyectiva (no rompe índices).
node tools/ehb/test-expindex.mjs

# 1) Cuantizador EHB (32 bases + half). -> out/ehb/palette.json
node tools/ehb/quantize-ehb.mjs "$SOURCE" --out out/ehb

# 2) Slicing + dedupe + tabla + export BASES-PRIMERO (assert COMPARAR = 100%).
#    - sin --ehb-merge: 1149 tiles únicos de 1600 celdas, COMPARAR = 100.00%
#    - con --ehb-merge 0.9X: fusiona parecidos (reporta absorción y %, regla 5)
node tools/ehb/slice-tiles.mjs "$SOURCE" --palette out/ehb/palette.json --out out/ehb

# 3) Cabecera C++ heredada por la demo (paleta + tabla de índices del mapa).
node tools/ehb/emit-const-201.mjs   # -> out/ehb/const_game_201.h

# 4) Banco de bloques X-Limited interleaved para incbin en .MEMF_CHIP.
node tools/ehb/emit-xlimited-bank.mjs # -> out/ehb/tilebank.xlimited.bin (+.h)

# 5) Compilar y ejecutar la demo.
bash ./tools/build/build-demo.sh demos/201_ehb_map --debug --clean
bash ./tools/run/run-demo.sh demos/201_ehb_map
```

Resultados medidos de una regeneración limpia (señales de la salida real):

- `slice`: `640x640 -> 40x40 tiles de 16`; `tiles únicos (EHB exacto): 1149 de 1600`;
  `COMPARAR: original(cuantizado EHB) vs reconstruido = 100.00% (409600)`; los PNG
  indexados pasan `roundtrip=0 -> OK`.
- `emit-const`: `192 colores, 1600 tiles en mapa, stride 256, 294144 B`.
- `emit-xlimited`: `1149 tiles -> 222720 B, 5568 planelíneas`.

**Determinismo**: regenerando hoy todos los artefactos clave son byte-idénticos a los
comprometidos en `out/ehb` (SHA256 de `palette.json`, `tilebank.raw.bin`, `tiles.json`,
`tilebank_indexed.h`, `const_game_201.h`, `tilebank.xlimited.bin`, `tilebank.xlimited.h`,
`reconstruct.png`, `tilebank.png` y `ehb_palette.h`). Es decir: el `out/ehb` actual se
generó con **estos** comandos y sin edición manual.

## 4. Qué produce cada paso (formato de los datos)

### 4.1 `quantize-ehb` → paleta

- K-means modificado **half-aware**: un píxel del original se asigna a `min(dist(base),
  dist(half))`; a los half se les sube ×2 por componente con clamp antes de promediar
  (evita que un carry cruce al canal anterior). 60 iteraciones como máximo.
- Las 32 bases se ordenan por luminosidad y se bajan a RGB444 (`0x0RGB`).
- `palette.json` guarda solo las 32 bases RGB (`bases`) y los `planes`.
- `ehb_palette.h` (`kEhbPalette[32]`) NO se usa en la demo 201: se genera solo como
  referencia; la paleta definitiva la emite `emit-const-201` desde el export del slicer.

### 4.2 `slice-tiles` → tiles, tabla y reconstrucción

Pasos internos (idénticos a las reglas de oro):

1. **Cuantizar el original a la paleta EHB antes de extraer** (`origEhb`).
2. **Dedupe exacto**: cada tile de 16×16 se hashea por su contenido de índices EHB; los
   repetidos se apuntan a la misma entrada del banco. `1149` únicos de `1600`.
   El orden del banco es el del primer hallazgo (raster del atlas).
3. **(Opcional) fusión por similitud** con `--ehb-merge F` (fracción de índices iguales).
   Reporta "antes→después" y deja de cumplirse el 100%; por eso el modo canónico de la
   201 es **sin fusión**.
4. **Reconstruir y comparar en el mismo espacio**: se vuelve a montar la imagen con
   `banco[tabla]` y se compara índice a índice contra `origEhb`. Sin fusión debe ser
   `100.00%`; si no, `slice-tiles` aborta.
5. **Export BASES-PRIMERO** (regla 7): internamente el slicer trabaja con índices
   INTERCALADOS `[base0,half0,base1,half1,…]`; al exportar convierte con `expIndex`
   (`tools/ehb/ehb-export-map.mjs`): base k `2k→k`, half k `2k+1→32+k` (más el slot
   transparente si lo hay). Es biyectiva, probada por `test-expindex.mjs`, y no
   idempotente: se aplica una sola vez. Todo lo que se exporta (`.h`, `.bin`,
   `tiles.json`, PNG) sale en convención BASES-PRIMERO.

Export resultante de `slice-tiles`:

- **`tilebank_indexed.h`**: `kTileIndexedPalette[palSize·3]`, `kTileBankStride=256`,
  `kTileBankBytes=294144`, `kTileIndexedMap[1600]` (tabla celda→índice de banco) y el
  modo raw: datos en `tilebank.raw.bin` (incbin, 1 byte/píxel).
- **`tilebank.raw.bin`**: 1149 tiles × 256 bytes, stride fijo, índice EHB 0..63.
- **`tiles.json`**: `{tile:16, cols:40, rows:40, palette:[64·3], bank:[{x,y,pix:256}],
  map:[1600]}` — la versión inspeccionable del banco (para herramientas host).
- **`reconstruct.png`** y **`tilebank.png`**: PNG indexados con **encoder propio**
  (`PLTE`+`IDAT`+`crc32`) y verificación `roundtrip=0` y `colorType=3`.

### 4.3 `emit-const-201` → `const_game_201.h`

No re-ensambla nada a mano: renombra de forma determinista los arrays de
`tilebank_indexed.h` a los símbolos que consume la demo (`kEhbPalette`,
`kRenderMap`) y declara el banco incbin. Fuente única = el pipeline; cero edición.

### 4.4 `emit-xlimited-bank` → banco interleaved

Convierte el banco indexado al layout que consume `draw_block_job` del engine
X-Limited: bitmap de 320 px de ancho (40 B/planelínea), tile `t` en
`(t%20, t/20)`, cada píxel indice EHB → sus 6 planos (bit p del índice = plano p;
bit 5 = half). Se convierte **en el host** para no cargar también el raw (287 KB) más
el banco (222 KB) más el display en 512 KB de Chip RAM.

## 5. Cómo se mete en el programa (demo 201)

```
 const_game_201.h          incluye como datos estáticos e incbin
   ├─ kEhbPalette[64·3]    → g_palette[32] bases (COLOR00..31); el half lo hace el hardware
   ├─ kRenderMap[1600]     → scene_cfg.map.cells (celda → índice de banco)
   └─ tilebank.xlimited.bin → sección .MEMF_CHIP (asm) → scene_cfg.blocks_prebuilt (aliado, sin copia)
```

- El banco se incbina en Chip RAM con `__asm__(".section tiles.MEMF_CHIP...")` en
  `demos/201_ehb_map/src/main.cpp:43-50`; `elf2hunk` lo emite como hunk HUNKF_CHIP y
  `LoadSeg` lo sitúa en Chip RAM.
- `XlimitedScene` **alia** el banco (`scene_cfg.blocks_prebuilt`), no lo copia ni lo
  transforma: el rectángulo de Chip total (banco + display + copper) cabe en 512 KB.
- La paleta solo carga las 32 bases en `COLOR00..31` (`g_palette[i] =
  kEhbPalette[i]`); el bit 0 de `BPLCON4=1` activa EHB (`xlimited.hpp` compositor
  single 6 planos) y el hardware genera los half con `base >> 1`.
- El mapa `kRenderMap` es la **tabla de índices**: cada celda dice qué tile del banco
  pintar; `draw_block_job` (`xlimited.hpp:772`) resuelve el índice → offset del banco
  `(block%20, block/20)` y programa el blit.

## 6. Diagrama de datos

```
 Beginning Fields.png (640x640)
        │ quantize-ehb.mjs
        ▼
 palette.json (32 bases) ────────────────────────────► ehb_palette.h (referencia)
        │ slice-tiles.mjs (cuantiza, dedupe, tabla, recompara 100%)
        ├──► tilebank_indexed.h : paleta[64·3] + kTileIndexedMap[1600] + stride
        ├──► tilebank.raw.bin  : 1149 tiles × 256 B (indices BASES-PRIMERO)
        ├──► tiles.json        : banco inspeccionable + mapa
        ├──► reconstruct.png   : montaje del original desde banco+tabla (verify)
        │       │ emit-const-201.mjs
        │       ▼
        │   const_game_201.h : kEhbPalette + kRenderMap (lo que compila la demo)
        │       emit-xlimited-bank.mjs
        │       ▼
        └────────────► tilebank.xlimited.bin (banco interleaved 222720 B, .MEMF_CHIP)
```

Los tres consumidores en el programa son: la **paleta** (`COLOR00..31`, EHB on), la
**tabla de índices** (`kRenderMap`, mapa 40×40 edge-clamped) y el **banco de
bloques** (`g_tilebank_xlimited`, alias sin copia). Los tres proceden directamente
del pipeline aquí documentado y son deterministas.

## 7. Ruta de edición de niveles (Tiled, no usada por la 201 actual)

`parse-tmx.mjs` lee un `.tmx` exportado a JSON y produce `tmx_bf.json`; `gid-to-bank.mjs`
convierte los `gid` al índice de banco (`out/ehb/tmx_bf.json` existe). Esta vía sirve
para diseñar el nivel en Tiled con `--tileset-gid` y `--layer Ground`. La 201 actual
deriva su mapa directamente del PNG de `slice-tiles` (misma tabla que reconstruye el
original), por lo que esta ruta queda como opcional/futura, no como parte del flujo
verificado de la demo.

## 8. Cómo se verifica que "el tile colocado corresponde a la tabla"

- **Datos (host, concluyente)**: `reconstruct.png` montado con `banco[kRenderMap]`
  coincide al 100.00% con el original cuantizado; el `expIndex` (intercalado →
  BASES-PRIMERO) es biyectivo y su aplicación está probada por `test-expindex.mjs`; la
  regeneración completa es byte-idéntica al `out/ehb` comprometido.
- **Direccionamiento del blit (host, concluyente)**: `draw_block_job` calcula el offset
  del banco como `(block%20)·2 + (block/20)·96·40`, exactamente el layout que escribe
  `emit-xlimited-bank.mjs`; con `block = kRenderMap[celda]` el blit copia **los píxeles
  del tile que dice la tabla**.
- **Runtime (parcial)**: la captura real muestra la paleta EHB esperada y el segmento
  inicial (cámara centrada) no presenta columnas/filas de tile repetido. Queda abierta
  la verificación celda a celda para todos los estados de cámara (en especial los
  bordes), que exige correlacionar cada frame con su `mapposx/mapposy` exacto; es el
  plan de instrumentación de telemetría descrito en
  `docs/roadmap/XLIMITED_8WAY_EHB_201.md`.