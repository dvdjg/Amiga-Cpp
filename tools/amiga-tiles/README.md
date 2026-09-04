# amiga-tiles — convierte un bitmap o atlas de tiles a tilebank indexado del Amiga

Un único script Node que toma un PNG y produce un **tilebank indexado** listo para
incbinar/incrustar en un programa de Amiga, junto con su paleta, su tabla de índices
y una reconstrucción comprobada. Es la evolución (todo en uno) del antiguo conjunto
`tools/ehb`, generalizada a **2..255 colores (64 = EHB)**, con o sin transparencia,
con varios dithering, redimensionado de calidad y varios criterios de paleta.

```
node tools/amiga-tiles/amiga-tiles.mjs <imagen.png> [opciones]
```

Ejemplos y cada opción abajo. El propio archivo es un tutorial; el código está
comentado en español.

## Índice de scripts (¿qué es cada uno y desde dónde se llama?)

| Script | Qué hace | Cómo se invoca | Quién lo llama |
|---|---|---|---|
| `amiga-tiles.mjs` | Cuantizador/tilebank (2..255 colores, EHB, dither, paletas, resize, empaquetado por bits) | `node tools/amiga-tiles/amiga-tiles.mjs <img> [opc]` | CLI directo; también **subproceso** desde `game-assets.mjs` (`--quantize`, con `--alpha`) |
| `extract-sprites.mjs` | Extracción de sprites por componentes (determinista) + agrupación opcional con IA | `node tools/amiga-tiles/extract-sprites.mjs <img> [opc]` | CLI propio; **exporta funciones** (`loadImage`, `detectBackground`, `extract`, `cropSprite`, `contactSheet`, `ask`, `extractJson`) que importa `game-assets.mjs` |
| `game-assets.mjs` | Pipeline único: fondos multi-zona → sprites → grupos (IA con fallback heurístico) → `--quantize` | `node tools/amiga-tiles/game-assets.mjs <img> [opc]` | CLI directo (entrada recomendada para preparar assets de un juego) |
| `run-demos.mjs` | Regenera `out/tile-demos/` (8 demos de cada capacidad) | `node tools/amiga-tiles/run-demos.mjs` | CLI directo (verificación/manual) |
| `run-vision-verify.mjs` | Verifica cada demo con ollama local (describe, corresponde, propone `--ops`) | `node tools/amiga-tiles/run-vision-verify.mjs [--resume] [--folder] [--all]` | CLI directo; sus `.ops.txt` los consume `amiga-tiles.mjs --ops` |

## Qué hace exactamente

1. **Lee el PNG** y detecta transparencia (píxeles con alfa < 128).
2. **Decide la profundidad** (auto o `--colors`): 4, 8, 16, 32 o 64 (EHB).
3. **Elige la paleta** (adaptada a la imagen, externa o fija, ver más abajo).
4. **Cuantiza** cada píxel al color representable más cercano, **opcionalmente con
   dithering** (error diffusion o matricial).
5. **Corta en tiles** (por defecto 16×16), elimina duplicados exactos (dedupe) o
   fusiona similares (`--merge`).
6. **Reconstruye la imagen solo con banco+tabla** y la **compara al 100%** con el
   original ya cuantizado (assert: sin fusión debe cuadrar exacta).
7. **Exporta**: `tilebank.h` + `tilebank.bin` (incrustables), `palette.json/.h`,
   `reconstruct.png` y `tilebank.png`, y opcionalmente el banco interleaved
   X-Limited.

## Uso rápido con el asset de ejemplo

El repositorio incluye `assets/Beginning Fields.png` (un atlas 640×640 = 40×40 tiles
de 16×16 sacado de *The Fan-tasy Tileset*). Con él:

```bash
# EHB (64 = 32 base + half), paleta kmeans half-aware, dedupe.
node tools/amiga-tiles/amiga-tiles.mjs tools/amiga-tiles/assets/Beginning\ Fields.png

# 16 colores con dithering Floyd–Steinberg.
node tools/amiga-tiles/amiga-tiles.mjs tools/amiga-tiles/assets/Beginning\ Fields.png \
  --colors 16 --dither floyd --out out/mi_map_16c

# 8 colores, dithering matricial (Bayer) y paleta median-cut.
node tools/amiga-tiles/amiga-tiles.mjs tools/amiga-tiles/assets/Beginning\ Fields.png \
  --colors 8 --palette mediancut --dither bayer --out out/mi_map_8c

# Imagen real: redimensionada a ~300 KB de índice y cuantizada a 31 colores.
node tools/amiga-tiles/amiga-tiles.mjs foto.png --max-ram 300000 --resample lanczos \
  --colors 31 --dither floyd --out out/foto
```

Salida por defecto en `out/` junto a la imagen, o donde diga `--out`.

## Salidas

Todos los archivos resultantes llevan **en el nombre las características de la
conversión** (sufijo `<colores>c_<técnica>_<dither>_<ancho>x<alto>`, p. ej.
`reconstruct_64c_ehb_floyd_592x496.png`, `tilebank_32c_kmeans_floyd_592x496.bin`),
y las mismas características quedan guardadas **como metadatos**: chunk `tEXt`
`conversion` en los PNG, línea `// Conversion:` en los `.h` y el campo `label` en
`palette_*.json`.

| Archivo | Contenido |
|---|---|
| `reconstruct_<sufijo>.png` | La imagen que se obtiene en el Amiga dibujando `banco[kIndexedMap]`. |
| `tilebank_<sufijo>.png` | Hoja de contacto con los tiles únicos (para inspección). |
| `tilebank_<sufijo>.bin/.h` | Índices por tile (`kTileIndexedMap`), paleta `kPalette` y metadatos `kTileBankStride`/`kTileBankBitsPerPixel`. **Empaquetado** por profundidad (1 B/px si `bits>4`; `ceil(tile²·bits/8)` B/tile en otro caso, bits LSB-first). |
| `palette_<sufijo>.json` | Metadatos (`label`) + `palette` + `bank` + `map` + `stats`. |
| `palette_<sufijo>.h` | Paleta en palabras Amiga `0x0RGB`. |
| `palette_chart_<sufijo>.png` | **Gráfico de uso de la paleta**: tira de rectángulos con todos los colores por slot, y debajo un histograma de barras con la frecuencia relativa de cada color en la imagen convertida. El slot 0 transparente se dibuja como tablero de ajedrez. PNG RGBA de inspección (no un activo de engine). |
| `palette_chart_<sufijo>.txt` | Métricas por color del gráfico (legibles sin abrir el PNG): `slotN=rgb(r,g,b) n=<píxeles> p=<%>`. |
| `tilebank.xlimited_*.bin/.h` | Solo con `--xlimited`: banco interleaved de 320 px para el engine X-Limited. |
| `source_resized_<W>x<H>_<resample>.png` | Con `--emit-source`: el origen redimensionado/recortado. |

## Modos de color y transparencia

- `--colors N` fija el número de colores de la paleta, **N en 2..255** (2, 3, 4, 5 o
  6 bits por píxel según `ceil(log2 N)`). Casos típicos: 4, 8, 16, 31 (reserva 1
  slot: 30 usados), 32, 64, 127…
- **Si `--colors N` es `2^n − 1` (3, 7, 15, 31, 63…)** el programa lo entiende solo:
  quieres `N` colores REALES con un **índice 0 transparente** (slots = `N+1` = 2^n).
  Ej.: `--colors 31` ⇒ 31 colores + transparente (5 bits/px). Para desactivar esa
  deducción usa `--no-alpha`. Con `--alpha` explícito el slot 0 también se reserva.
- Con **`--colors 64`** se activa **EHB**: se eligen 32 bases y el hardware genera
  los 32 half (base/2 cada componente). Con `--alpha`, el base 0 es transparente.
- Sin `--colors` se elige automáticamente según los colores únicos del original:
  ≤4 → 4, ≤8 → 8, ≤16 → 16, ≤32 → 32, si no → 64 EHB.
- `--alpha` / `--no-alpha`: reservan (o no) el **índice 0 para transparencia**. Sin
  flag se **autodetecta** (reserva si ≥0,5 % de píxeles transparentes).
  Con alfa, los colores reales son `colors-1` (EHB: bases 1..31 y sus halves).

**Adaptación de profundidad y empaquetado** (`--pack auto|on|off`, default `auto`):
si la imagen tiene pocos colores, la tool elige los bits mínimos y **empaqueta el
banco** (p. ej. 10 colores → 4 bits/px → 2 px/byte; 7 colores → 3 bits/px). `auto`
empaqueta cuando `ceil(log2 colors) ≤ 4` (16, 8, 4); stride por tile = `ceil(tile²·bits/8)`
y los bits van **LSB-first**. `palette_*.json` y el `.h` incluyen `kTileBankBitsPerPixel`
y `kTileBankStride` (desempaquetar antes de usar en el juego).

## Dithering

- `--dither none` (por defecto): estricto, cada píxel va al color más cercano.
  Es el que **mantiene el dedupe de tiles** (reconstrucción al 100%).
- `--dither floyd`: error diffusion de Floyd–Steinberg (bueno para fotos gradadas).
- `--dither atkinson`: Atkinson (textura con menos valores medios).
- `--dither bayer`: matricial 4×4 (rápido, patrón regular).
- **`--dither best`**: Floyd + **serpentina** + **deadband 14** + **clamp 16**.
  Recomendado para **fotos continuas**: en zonas de color casi uniforme (p. ej.
  cielos) el Floyd clásico oscila entre dos colores de brillo muy distinto y dibuja
  un punteado feo. `best` no difunde errores pequeños (zonas planas limpias) y limita
  el error acumulado; medido en un paisaje: cielo con píxeles aislados 9.7 % → 3.5 %
  y PSNR 25.5 → 27.7 dB frente al Floyd clásico.
- Fino: `--dither-threshold F` (deadband), `--dither-clamp N` (capa por canal),
  `--serpentine` (escaneo en zigzag, rompe gusanos) y `--dither-strength 0..1`.

El dithering rompe el "100 %" del dedupe en zonas que eran idénticas en el original
(cada píxel oscila entre dos índices). Es el compromiso esperado: **dithering para
fotos o degradados, `none` para tilemaps/pixel-art**.

## Paletas

`--palette <fuente>` acepta cuatro familias:

1. **Adaptativas (a la imagen):**
   - `kmeans` (por defecto): k-means. En EHB usa la distancia **half-aware**
     `min(dist(color), dist(half(base)))`, la variante clásica de cuantizar
     pensando en que existirán las versiones a media intensidad.
   - `mediancut`: algoritmo de Heckbert (cajas por rango de canal). Sólido para
     16/32 colores.
   - `bright`: solo EHB. Cuantiza la **mitad más brillante** de la imagen a las 32
     bases y deja que los half cubran la parte oscura (excelente para imágenes con
     alto rango dinámico).
   - `popularity`: los `N` colores más frecuentes del histograma (rápido, calidad
     baja).
2. **Externa:** cualquier archivo JSON `{"colors":[[r,g,b],…]}` (o array directo).
   Se ajusta a `colors` exactos; si trae más se recorta, si trae menos se rellena
   con grises.
   ```
   node tools/amiga-tiles/amiga-tiles.mjs img.png --colors 16 --palette mi_paleta.json
   ```
3. **Fijas** (no dependen de la imagen, tipo "halftone"):
   - `cube` (alias `halftone`): malla regular en RGB444 (q³ del cubo + grises +
     primarios), una "web-safe" adaptada a 4 bits por componente.
   - `grays`: rampa de grises.
4. `--sort none|luminance` ordena la paleta (por defecto por luminosidad; útil para
   encontrar índices a simple vista).

### Estrategia EHB en una línea

En EHB conviene elegir los 32 bases sabiendo que también existen sus versiones a
mitad de brillo. `kmeans` integra esa idea en la distancia; `bright` explota el
opuesto (bases = zona clara, half = sombras). Para 32/16 colores sin EHB,
`mediancut` o `kmeans` + `--dither floyd` dan muy buenos resultados.

## Verificación y evidencia

- El paso **COMPARAR** compara el original ya cuantizado contra la reconstrucción
  `banco[kIndexedMap]` **índice a índice** (mismo espacio). Sin fusión debe dar
  **100.00 %**; si no, la tool aborta. Con dithering el dato que se compara es la
  imagen cuantizada **con** dither, que es la que de verdad se dibujará.
- Los dos PNG se generan con **encoder indexado propio** (PLTE+IDAT+crc32) y se
  verifican con round-trip (`colorType=3`, 0 diferencias).
- Depuración visual: abre `reconstruct.png` (lo que verá el Amiga) junto a la
  fuente original.

## Integración en una demo (mínima)

```cpp
#include "tilebank.h"                     // kPalette, kTileBankStride, kTileIndexedMap
extern const unsigned char g_tilebank_bin[];   // incbin de tilebank.bin (o Copyload a Chip)

// Para dibujar la celda (tx,ty): el tile es kTileIndexedMap[ty*cols+tx] y sus
// índices están en g_tilebank_bin + tile_idx * kTileBankStride.
```
Cada start-up en el Amiga carga `kPalette` en los registros de color (0..colors-1;
en EHB solo las 32 bases porque los half los genera el hardware) y el banco en Chip
RAM con `incbin` en una sección `.MEMF_CHIP` (receta en `demos/201_ehb_map/src/main.cpp`).

## Redimensionado de calidad y recortes

Antes de cuantizar se puede **ajustar la resolución** y/o **recortar**:

- `--resize WxH` fija el tamaño exacto (se ajusta al múltiplo de tile más próximo).
- `--max-area N` / `--max-ram N` redimensionan a un ÁREA objetivo preservando el
  aspecto (para que un buffer de índices a 1 B/píxel quepa en N bytes de RAM, por
  ejemplo `--max-ram 300000` ≈ 300 KB).
- `--resample metodo` elige el algoritmo: **lanczos** (mejor calidad, por defecto),
  **area** (caja, ideal para reducciones), **bilinear** o **nearest**.
- `--crop X,Y,W,H` recorta una región (útil para trocear un fondo y cuantizar planos).
- `--emit-source` guarda solo la imagen de trabajo (post crop/resize) en
  `<out>/source_resized.png` y sale; sirve para inspeccionar/preparar la región.

## Parámetros finos (los que se usan menos)

- `--tile N` — tamaño de tile en píxeles (defecto 16; 8/32 funcionan igual).
- `--palette-k N` — K del k-means (defecto 3). Sólo afecta a `--palette kmeans`.
- `--sheet-scale N` — escala de la hoja de contacto `tilebank_<sufijo>.png` (inspección visual).
- `--help` / `-h` — imprime el uso y las opciones.

## Fondo continuo → bandas (Metal Slug)

- `--extract-bands DIR [--band-jump 60] [--band-step 32] [--band-align 16]` parte una
  imagen en BANDAS horizontales separando por saltos del color medio por fila (los
  "planos" de un fondo scrolling continuo). Escribe cada banda como PNG, `bands.json`
  con los rects y `bands_preview.png`. Útil para trocear un fondo largo y cuantizar
  cada plano por separado (ver `run-demos.mjs` demo 04/05).

## Descripción con ollama local (visión)

- `--describe` envía la imagen final de trabajo a un **modelo de visión local** y
  guarda la descripción en `<out>/image_description.txt`.
- `--model MODEL` (defecto `qwen3-vl:8b-instruct-q8_0`), `--ollama-base URL`
  (defecto `http://127.0.0.1:11434`) y `--ollama-tokens N`.

```bash
node tools/amiga-tiles/amiga-tiles.mjs imagen.png --colors 64 --describe --model qwen3-vl:8b-instruct-q8_0
```

## Recortar según propuestas del VLM (`--ops ops.txt`)

Ejecuta las propuestas del modelo de visión: lee el `<origen>.ops.txt` generado por
`run-vision-verify.mjs`, **extrae cada trozo** que el modelo propuso (coordenadas en %,
el parser tolera `0-15% (X), 0-30% (Y)` y `35%-65% en X, 15%-45% en Y`) y **aplica el
color transparente** sugerido (los píxeles de ese color pasan a alfa 0).

```bash
node tools/amiga-tiles/amiga-tiles.mjs source.png --ops source.png.ops.txt --out out/ops
# -> out/ops/extract/ops_00_<nombre>.png … + ops.json
```

## Pipeline único: `game-assets.mjs`

Con **una sola orden** extrae sprites/fondos de una ilustración y decide por sí mismo
(multi-fondo por bandas, componentes, agrupación con ollama local, transparencia y
cuantización EHB/32/16 por grupo):

```bash
node tools/amiga-tiles/game-assets.mjs arte.png --out out/mi_juego
#   detecta fondos (blanco+verde…), extrae piezas, agrupa (IA con fallback heurístico),
#   crea grupos/<nombre>/frame_NN_*.png + group.json (frames/offset/ancla) + TRANSPARENCIA.md
# Opciones: --tol N --min N --split --ai | --no-ai --tokens N --quantize N (64=EHB)
```

- Los PNG extraídos codifican el fondo como **alfa = 0**; convenio Amiga: índice 0 = transparente.
- `--background auto` detecta **blanco del borde + colores de croma**: los colores
  **croma** (saturados, o interiores aunque sean neutros) pueden ser una **hoja de
  sprites**. La tool es genérica ante cualquier imagen con una región de color sólido
  que contiene una animación en rejilla (verde, magenta, azul…):
  - Si dentro del color hay **separadores** (filas/columnas casi 100 % de ese color),
    `splitChromaGrid` parte la animación en **frames** y extrae CADA celda con
    transparencia SOLO contra ese color (el blanco del fogonazo se conserva). En la
    tira verde de Metal Slug saca **15 frames** (9 de la fila superior + 6 de la
    inferior) separados por la franja de verde.
  - Si no hay rejilla (una sola maraña), se fusiona en UN sprite con ese color transparente.
  - Los colores **neutros** del marco (p. ej. blanco) siguen extrayéndose por
    **componentes individuales** (así el camión no se rompe).
  Info en `sprites.json` (`chromaBoxes`, `gridFrames`, `merged`). Puedes forzar el
  color con `--background R,G,B`, elegir modelo de visión con `--model` y afinar
  con `--tol`.
- `--quantize N` cuantiza **cada GRUPO por separado**: cada grupo de frames tiene su
  **propia paleta** (`<grupo>/palette_*.json/.h`) y todos sus frames comparten esa
  misma paleta. `N=64` → EHB (32 bases + 32 half, índice 0 transparente); `N=2^n−1`
  (31, 15, 7…) → `N` colores reales + índice 0 transparente (deducido).
- Con **`--ai` la herramienta es SEMIAUTÓNOMA** con ollama local: tras la detección
  determinista, la IA **comprende** qué contiene cada caja de croma (nombre/tipo/frames)
  y la organiza en `<out>/<contenido>/` con ese nombre, y además **propone cajas de
  croma** que el determinismo no vio — el script las extrae al instante (crop +
  color refinado por histograma + sprite fusionado con transparencia contra ese
  color). Salidas extra: `chroma_boxes.png` (overlay con las cajas) y
  `vision_boxes.json` (entendimiento y propuestas de la IA). La comprensión de
  contenido depende del modelo (`--model qwen3-vl:8b-instruct-q8_0` por defecto);
  con un modelo de visión más capaz (p. ej. `gemma3:12b`) la interpretación mejora.
- `extract-sprites.mjs` queda como utilidad de bajo nivel (exporta sus funciones y
  solo ejecuta su CLI cuando se invoca directamente).

## Notas

- Dependencias: `pngjs` y `jpeg-js` (ya en `node_modules` del repo). La entrada puede
  ser **PNG o JPEG** (la decodificación JPG la hace `jpeg-js`, sin herramientas
  externas ni .NET).
- **Atkinson**: implementado con su reparto en 6 vecinos pero NORMALIZADO (suma de
  pesos = 1, div = nº de taps); el clásico pierde 1/4 del error y, con paletas
  escasas, puede quedar indistinguible de `none`. Aún así, con 16 colores en una foto
  compleja su efecto es sutil (reparte el error hacia abajo, poco acoplamiento
  hacia delante); para fotos usa `floyd`.
- Todo el código ES6/ESM, sin dependencias de TypeScript ni compilación.
- El asset `assets/Beginning Fields.png` es solo un ejemplo; la ruta original fuera
  del repo es `C:/Users/dvdjg/Documents/programa/Assets/2D/The Fan-tasy Tileset
  (Free)/Tiled/Tilemaps/Beginning Fields.png`.

## Demostraciones

`node tools/amiga-tiles/run-demos.mjs` regenera **`out/tile-demos/`**, una carpeta
por algoritmo con la imagen fuente, los resultados (imágenes, índices, paletas,
headers y binarios) y su `README.md`:

- `01_imagen_real_cuantizacion` — imagen real (JPEG) a 16 colores, dither none/floyd/atkinson.
- `02_redimensionado_calidad` — Lanczos vs area vs bilineal vs vecino.
- `03_tiling_dedupe` — tilebank por dedupe exacto y fusión 0.95.
- `04_metalslug_bandas` — extracción de planos del fondo de Metal Slug.
- `05_metalslug_cuantizacion` — EHB / 31 / 15 / 7 colores sobre dos regiones.
- `06_descripciones_ollama` — descripciones de los assets con el modelo de visión.
- `07_tiles_32x32` — tiles 32×32 y detección de "sin patrón de repetición".

## Verificación automática con IA de visión (ollama local)

`node tools/amiga-tiles/run-vision-verify.mjs` **detecta** si hay ollama local con un
modelo de visión instalado y, si lo hay, por cada demo hace SIEMPRE tres cosas:

1. **Describe** la imagen **origen** y cada **resultado** (`reconstruct.png`,
   `tilebank.png`, `bands_preview.png`, `source_resized.png`).
2. **Corresponde origen|resultado**: monta la imagen ORIGEN y el RESULTADO lado a
   lado y pregunta al modelo si se corresponden y qué diferencias/artefactos ve
   (el montaje evita que qwen3-vl funda dos imágenes en un solo mensaje).
3. **Propone operaciones de assets**: pide al modelo qué haría con el ORIGEN para
   preparar assets de juego — recortar y extraer trozos si ve varios dibujos
   independientes sobre un fondo común (coordenadas aproximadas en %), color para
   usar como transparente, paleta/EHB, remuestreo, tamaño de tile, separación de
   planos, offsets…

Guarda `<imagen>.vision.txt`, `<origen>.ops.txt`, `<resultado>.compare.txt`, un
`vision_report.md` por carpeta y el resumen global `VISION_SUMMARY.md`.

```bash
node tools/amiga-tiles/run-vision-verify.mjs            # auto: ollama+visión, describe, compara y propone
node tools/amiga-tiles/run-vision-verify.mjs --resume   # reusa lo ya generado
node tools/amiga-tiles/run-vision-verify.mjs --folder 03_tiling_dedupe
node tools/amiga-tiles/run-vision-verify.mjs --all --limit 20
```

Si no hay ollama o no tiene modelo de visión, el script avisa y termina sin inventar
nada (no gasta tokens).