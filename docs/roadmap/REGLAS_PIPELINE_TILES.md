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

## Pipeline canónico (verificado)
```
quantize-ehb.mjs <origen>.png            # -> palette.json (32 bases EHB)
slice-tiles.mjs <origen>.png --palette out/ehb/palette.json [--ehb-merge F]
  # 1) cuantiza el original a EHB  2) extrae únicos  3) (fusión)  4) reconstruye
  # 5) assert COMPARAR == 100% (sin fusión)  6) .h + tiles.json + PNG indexados
```
Estas dos herramientas son las **rutinas reutilizables para extraer tiles de un bitmap
EN CRUDO (sin metadatos)**: no requieren Tiled ni .tmx; sirven para cualquier asset futuro.
Los metadatos de Tiled (parse-tmx) son un extra opcional cuando existen. Una nota práctica:
un PNG de un MAPA RENDERIZADO tiene ~1100 patrones distintos; un TILESET real tiene decenas;
si se parte la imagen equivocada la única fuente de verdad es este contrato + el assert.