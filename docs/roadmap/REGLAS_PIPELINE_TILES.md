# Reglas de oro del pipeline de tiles/EHB (para no romper nada)

Estas invariantes se deben cumplir SIEMPRE; se verifica cada una en el paso que toca
(assert; si falla, aborta). Son el "sentido común" que se echó en falta en las primeras
iteraciones (2026-09-01).

## 1. Cuantizar el ORIGINAL a su paleta ANTES de extraer
El tile debe extraerse de la imagen YA en el espacio donde se dibuja (EHB: 32 base + half).
Nunca extraer del RGB original y hablar de "duplicados" en RGB: lo que importa es el índice
EHB que consumirá el chipset.

## 2. Comparar SIEMPRE en el mismo espacio
"¿Coincide la reconstrucción con el original?" se responde comparando
`original(cuantizado a EHB)` contra `reconstruido (desde banco+mapa)`, índice a índice.
Sin fusión debe dar 100.00%; es un assert. Comparar EHB contra RGB solo sirve para ver
cuánto pierde la paleta (aprox.), no para validar el mapa.

## 3. Un catálogo (tilebank) nunca debe ser mayor que el original que resume
Si hay N celdas y U tiles únicos, U ≤ N; el catálogo a 1× debe tener ≤ píxeles que el mapa.
Solo se amplía para inspección humana/VL con `--sheet-scale`, y se señala que es un
artefacto de presentación.

## 4. PNG válidos e INDEXADOS para el flujo
`pngjs` no escribe PNG con paleta (re-encodía a RGBA). Usar el encoder propio
(PLTE+IDAT+crc32) y verificar round-trip: colorType=3 y 0 píxeles distintos. El `.h` es
donde van los datos hardware-friendly (índices), no los PNG.

## 5. Cada umbral expone su pérdida
Cualquier fusión (--ehb-merge, --similar) debe reportar: n.º de tiles ANTES→DESPUÉS y el %
de índices que siguen coincidiendo con el original cuantizado. Decidir sin cifras es la
puerta a lo absurdo.

## 6. Método antes que código
Antes de implementar el siguiente paso (parse-tmx, planos EHB, demo 201): escribir estos
invariantes del paso, y el test que los verifica, ANTES de la solución.

## 7. Los índices van BASES-PRIMERO y el Amiga NO transforma píxeles
Los datos que se incbinan (tilebank) y la paleta del `.h` deben ir ya en la convención que
consume el chipset EHB: **COLOR00..31 = las 32 bases e índices 32..63 = half** (el half lo
genera el hardware como base/2; bit 5 del índice de 6 bits = plano 6). **Ninguna
transformación de índices se hace en la CPU del Amiga**: ni en arranque ni por frame; el
banco incbinado tiene que estar listo para el Blitter/Copper.

El slicer `slice-tiles.mjs` trabaja internamente con índices INTERCALADOS
`[base0, half0, base1, half1, ...]` (índice par 2k = base k, impar 2k+1 = half k), que es su
forma natural de emparejar cada base con su half al cuantizar y comparar (assert COMPARAR al
100%). Pero **al exportar** (paso 6) el propio slicer reindexa todo a bases-primero con el mapa
`expIndex`:
```
e = (v>>1) | ((v&1) << 5)
   base k (v=2k)   -> e = k       = v>>1
   half k (v=2k+1) -> e = 32+k    = (v>>1) | 0x20
```
Esta conversión es una permutación **biyectiva** (1:1) de 0..63: se hace en el HOST dentro de
`slice-tiles.mjs` (no hay script aparte — la construcción vive en
`tools/ehb/ehb-export-map.mjs`) y deja el banco, la paleta del `.h`, `tiles.json` y los PNG
exportados en convención bases-primero. La demo lee el byte del banco **directo** como índice
EHB (`e = v`) y carga en `COLOR00..31` solo las 32 bases (`palette[i] = kEhbPalette[i]`,
índices 0..31). Ver el uso real en `demos/201_ehb_map/src/main.cpp` (`fill_planes` + carga de
paleta) y la implementación del reindexado de export en `tools/ehb/slice-tiles.mjs` (paso 6:
`expPalette`/`expIndex`, probado por `tools/ehb/test-expindex.mjs`).

> Nota: `expIndex` es biyectiva pero **NO idempotente** — solo se aplica una vez a los índices
> intercalados internos en el export; re-ejecutarla sobre datos ya bases-primero degradaría.

## Pipeline canónico (verificado)
```
quantize-ehb.mjs <origen>.png            # -> palette.json (32 bases EHB)
slice-tiles.mjs <origen>.png --palette out/ehb/palette.json [--ehb-merge F]
  # 1) cuantiza el original a EHB  2) extrae únicos  3) (fusión)  4) reconstruye
  # 5) assert COMPARAR == 100% (sin fusión)  6) .h + tiles.json + PNG indexados
  #    (el paso 6 exporta ya en convención BASES-PRIMERO, regla 7: expIndex)
emit-const-201.mjs                     # renombra tilebank_indexed.h -> const_game_201.h
  # (solo demo 201) ensamblado determinista de una sola fuente: paleta + mapa +
  #   externs del banco incbin, sin edición manual.
```

**Regeneración real de la demo 201** (origen: mapa de `The Fan-tasy Tileset`):
```
SOURCE="C:/Users/dvdjg/Documents/programa/Assets/2D/The Fan-tasy Tileset (Free)/Tiled/Tilemaps/Beginning Fields.png"
node tools/ehb/test-expindex.mjs                                    # test host de expIndex
node tools/ehb/quantize-ehb.mjs "$SOURCE" --out out/ehb          # -> palette.json
node tools/ehb/slice-tiles.mjs "$SOURCE" --palette out/ehb/palette.json --out out/ehb
node tools/ehb/emit-const-201.mjs                                 # -> const_game_201.h
bash ./tools/build/build-demo.sh demos/201_ehb_map --debug --clean
bash ./tools/run/run-demo.sh demos/201_ehb_map
```
<hr/>
**Nota (2026-09-02)**: los datos actuales de la demo 201 ya están en bases-primero
(`out/ehb/tilebank.raw.bin`, `out/ehb/const_game_201.h`). Desde esta fecha el reindexado a
bases-primero lo hace el propio `slice-tiles.mjs` en su export (paso 6), y `const_game_201.h`
se genera con `emit-const-201.mjs` (renombrado determinista, sin ensamblado manual), por lo
que cualquier regeneración futura respeta la regla 7 de forma automática y no hace falta un
paso adicional. La fuente `Beginning Fields.png` NO se perdió: está en `programa/Assets/2D/...`
(fuera del repo); conserva la ruta si mueves el equipo.