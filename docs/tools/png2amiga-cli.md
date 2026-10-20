# png2amiga: conversión de imágenes a Amiga desde línea de órdenes

`png2amiga` es la herramienta externa de conversión PNG/JPEG/WebP → gráficos de plataformas retro que usa este proyecto para preparar assets Amiga (planos, paletas, Copper, dithering). Vive en el repo hermano `C:\Users\dvdjg\Documents\programa\AI\Amiga\png2amiga`; este documento resume **qué utilidades expone y cómo invocarlas** desde Amiga-Cpp. El catálogo completo de flags es el `--help` del binario y el `README.md` del repo.

## Binario y compilación

En este equipo la build operativa es la de MSVC (VS 2026), generada con el preset `msvc`:

```powershell
# Binario canónico para las tools del repo.
C:\Users\dvdjg\Documents\programa\AI\Amiga\png2amiga\build-msvc\png2amiga.exe

# Recompilar (y correr la suite de 625 tests) tras actualizar el repo.
cmd /c '"C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul && cmake --preset msvc && cmake --build build-msvc'
ctest --test-dir build-msvc --build-config Release --parallel --output-on-failure
```

La build habilita todas las rutas rápidas del proyecto: `/O2`, `/arch:AVX2`, `/fp:contract` y `__AVX2__`/`__FMA__` (kernels vectorizados de cuantización y SSIMULACRA2). El preset `default` (GCC 15/16) no sirve con el MinGW instalado: su libstdc++ no declara `std::aligned_alloc` (`src/aligned_vector.hpp` solo usa `_aligned_malloc` bajo `_MSC_VER`).

`--help` imprime a **stderr**; los errores usan códigos `sysexits.h` (0 ok, 1 interno, 64 uso, 66 sin entrada, 73 sin salida). El encoding es **determinista**: mismo input + mismos flags → mismo byte a byte.

## Sintaxis básica

```bash
png2amiga <input.png|jpg|webp> -o <output> [opciones]
```

Ejemplos mínimos:

```bash
# Amiga lores, 5 planos (32 colores), paleta automática OCS y Floyd-Steinberg.
png2amiga fondo.png -o fondo.iff --mode lores --depth 5

# EHB (32 base + 32 half-brite), sin sliced.
png2amiga fondo.png -o fondo.iff --mode ehb

# Dual playfield de 3 planos (PF2) con sliced + mid-line swaps; solo .png/.h/.cpp/.c/.idx.
png2amiga fondo.png -o fondo.png --mode lores --dpf --sliced --strips --dither opt-checker

# HAM6 con prior de suavidad: en degradados prefiere transiciones de un paso
# en vez de saltos a un color SET lejano (0 = off; útil 0.25-0.5).
png2amiga fondo.png -o fondo.png --mode ham6 --dither atkinson --ham-smooth 0.5

# HAM6 base con métrica que prioriza el brillo (mejor S2 en degradados).
png2amiga fondo.png -o fondo.png --mode ham6 --dither none --ham-metric oklab-lum

# Preview PNG de cualquier modo (no escribe asset, es la simulación hardware del resultado).
png2amiga fondo.png -o preview.png --mode ham6 --sliced
```

## Modos Amiga y cuándo usar cada uno

| Modo | Planos | Colores base | Uso típico |
|---|---|---|---|
| `lores` | 1-6 (`--depth`) | 2-64 | Bitmap indexado general; `--depth 5` = 32 colores. |
| `hires` | 1-4 | 2-16 | 640 px; con `--sliced` es la ruta DHIRES (paleta por línea). |
| `ehb` | 6 (fijo) | 32 + 32 half-brite | Extra Half-Brite OCS; `--depth` se ignora. |
| `ham6` / `ham8` | 6 / 8 | 16 / 64 | Hold-And-Modify OCS/AGA con DP beam search (`--ham-beam`, `--ham-fast`). |
| `lores --dpf` | 3 (OCS) / 4 (AGA) | 8 / 16 | Dual playfield (PF2); CAMG con `DBLPF`, ideal con `--strips`. |

`--chipset ocs|aga` fija la precisión de paleta (12/24 bits); HAM7/HAM8 fuerzan AGA. `--interlace` marca `LACE` en CAMG. `--native-par` letterboxea/pillarboxea al buffer del hardware; `--no-scale` desactiva el escalado (el original se usa tal cual). El alto se calcula del aspecto del origen salvo `--width`/`--height` explícitos.

## Estrategias de paleta

- **Automática**: `--quantizer auto` elige `ocs-optimal` (histograma + k-means sobre las 4096 OCS) o `ocs-bruteforce`; en AGA usa median-cut. Es el defecto.
- **Externa / compartida**: `--palette <file>` fuerza una paleta (`.gpl`, CMAP IFF, hex, `.json`); `--quantize-from <file>` la entrena con otro archivo; `--joint-input <file>` (repetible) entrena una paleta común a varias imágenes — el camino para que N salas o N sprites compartan CMAP.
- **Fijar índices**: `--lock-index <id> <hex>` (el cuantizador puede usar el color), `--reserve-range <rango> <hex>` (no puede), `--pin-index-at <id> <x> <y>`. Útil para reservar color 0 transparente o slots de HUD.
- **Sliced palette** (`--sliced`): el Copper reescribe registros en el hblank de **cada línea**; la paleta efectiva pasa a ser por fila. Presupuesto real OCS de 14 MOVEs/línea; `--slice-changes N` lo sobrescribe (puede salirse de hardware real).
- **Strip palette** (`--strips`): añade swaps **a mitad de línea** sobre la base sliced. Solo OCS lores, sin interlace: `lores --depth 1-4`, `lores --dpf`, `ehb`, `ham6`. Cada modo tiene su rejilla calibrada (8 px en 1-4 planos, 16 px en 6 planos). **Restricción de salida**: `--strips` solo admite `.png`, `.h`, `.cpp`/`.c` y `.idx`; el IFF no puede almacenar los MOVEs de Copper, así que para uso real se genera `.h`/`.cpp`.

Combinación habitual para fondos DPF de aventura: `--mode lores --dpf --sliced --strips` (PF2 de 3 planos + paleta por línea + swaps a mitad de línea).

## Dithering

`--dither <método>` (catálogo en `--list-dithers`), `--dither-strength 0.0-2.0`, `--error-clamp` (máximo error por canal), `--refine <0-32>` (iteraciones de refinado de paleta), `--best` (multi-restart con `--best-metric`, ~20-30× más lento).

| Familia | Métodos representativos | Cuándo |
|---|---|---|
| Error diffusion | `floyd-steinberg` (defecto), `atkinson`, `sierra-lite`, `stucki`, `jarvis`, `gilbert`, `riemersma` | Gradientes suaves y máximo detalle global; Atkinson preserva bordes con más contraste. |
| Palette-aware ordered | `opt-checker`, `opt-line`, `opt-vline`, `tri-tone`, `knoll`, `yliluoma1/2` | Cuando importa el error real contra la paleta final; patrones estructurados, no ruido. |
| DBS | `dbs` | Óptimo perceptual muy lento; referencia de calidad. |
| Structure-aware | `structure-fs`, `contrast-fs`, `zhoufang` | Preservar estructura/contraste local en la imagen. |
| Bayer / noise / halftone | `bayer4x4`, `blue-noise`, `void-cluster`, `halftone8x8`, … | Texturas concretas, estilos de trama, evitar banding en tramos muy planos. |

La fuerza por defecto está afinada por método (p. ej. Floyd-Steinberg arranca en 0.90 en DPF strips y 0.72 en lores 32); no conviene sobrescribirla sin medir. Como guía general: Floyd-Steinberg suele dar el mejor PSNR/S2 global, Atkinson conserva mejor los bordes y `opt-checker` uniformiza el color a costa de PSNR/S2 en imágenes de aventura con gradientes suaves.

### Métrica de color para HAM (`--ham-metric`)

Aclaración: el codificador **no** usa una distancia RGB con pesos iguales. El valor por defecto `oklab2` es OKLab² (perceptual): la luminosidad L ya es el eje dominante y la crominancia (a/b) va separada. `srgb-mse` es la distancia sRGB sin pesos (solo para maximizar PSNR). Se han añadido dos métricas para comparar la hipótesis de que conviene priorizar el brillo: `abc342` (pesos empíricos de Leonard/abc: 3·dr² + 4·dg² + 2·db² sobre deltas de 4 bits, `abc-bitmap-converter/src/color.cpp:35-41`) y `oklab-lum` (OKLab² con L×2, que prioriza una rampa de brillo suave sobre la crominancia; equivalente práctico al enfoque HSV sin convertir cada candidato). Resultados en las 86 salas: `abc342` mejora el PSNR pero empeora S2 (base −3,66; atkinson −1,34) → opción de comparación, no recomendada como defecto; `oklab-lum` da **+2,32 S2** de media en HAM6 base (mejora en 77/86 salas) y +0,21 con atkinson (49/86) a costa de PSNR (37,97 vs 38,99 en base), y es la mejor de las probadas para banding de luminancia. En `room_87` base: S2 63,86→67,52 con `oklab-lum`; con atkinson 71,73. Uso: `--ham-metric oklab-lum`. El grid de 12 bits sigue siendo el límite duro, así que la métrica se combina con dither cuando la fuente ya está tramada.

## Métricas que reporta

La salida de estado (stdout) termina con una línea del estilo:

```text
Encoded:  6 bitplanes, disk: 51.2 K, chip: 50.8 K, 170 colors, 14.9 avg swaps/line, error: 34.8160, PSNR: 36.43 dB, S2: 65.38
```

| Campo | Significado |
|---|---|
| `colors` | Colores distintos realmente presentes en el render (en strips, únicos del frame). |
| `avg swaps/line` | Cambios de paleta por línea que ejecutará el Copper (solo sliced/strips). |
| `error` | Error OKLab acumulado antes de normalizar. |
| `PSNR` | PSNR en dB contra el origen; `inf` en imágenes planas (1 color). |
| `S2` | SSIMULACRA2 (calidad perceptual); puede ser negativo en imágenes casi vacías. |

`--score-vs <ref>` puntúa sin reencodificar (admite `.png` o `.idx` + `--palette`). `--json` **solo** emite `{"input","output","status"}`, no las métricas: para automatizar hay que parsear el texto de estado o usar `--score-vs`.

## Formatos de salida

| Extensión | Contenido | Cuándo |
|---|---|---|
| `.png` | Preview 24-bit de la simulación hardware | Inspección visual y verificación con Ollama. |
| `.iff` / `.ilbm` | ILBM con BMHD/CMAP/CAMG/BODY (ByteRun1) | Asset Amiga estático (sin strips). |
| `.h` | Arrays `UWORD` de planos + paleta + copper list | Inclusión en demos/juegos C/C++; único formato de strips con datos Copper. |
| `.cpp` / `.c` | Viewer Amiga compilable (`build-amiga.sh`) o viewer DOS | Reproducir sliced/strips tal cual en máquina o emulador. |
| `.raw` / `.bin` / `.bpl` | Planos crudos (layout `auto/interleaved/standard`) + `.pal` | Bootblocks, ensamblador, pipelines propios. |
| `.idx` | Índices chunky 1 byte/píxel | Herramientas propias que indexan la paleta. |
| `.pal` | Paleta OCS big-endian `0x0RGB` | Acompaña a `.raw`. |
| `.pi1`/`.pi2`/`.pi3`, `.prg`, `.koa`, `.hir`, `.k7` | Formatos Atari/C64/TED específicos | Solo con sus modos (`--mode stf-low`, `c64-*`, …). |

## Nombres auto-descriptivos y lote

`out/README.md` exige nombres que declaren qué son y con qué parámetros se generaron. La receta usada en la prueba MI1 (y repetible) es: convertir a un nombre temporal, **parsear la línea `Encoded:`**, y renombrar el archivo con variante + cuantizador + dither + fuerza + PSNR + error + S2 + colores + swaps. Ejemplo real generado:

```text
room_1__dpf3-sliced-strips__noscale-320x144__q-ocs-optimal__dither-opt-checker__st1.00__psnr36.43dB__err34.8160__s2-65.38__swap14.9__170c.png
room_12__lores32-plain__noscale-960x144__q-ocs-optimal__dither-opt-checker__st0.70__psnr33.64dB__err182.1740__s2-50.65__32c.png
room_1__ham6-atkinson__noscale-320x144__q-median-cut__dither-atkinson__st0.80__psnr39.50dB__err13.8819__s2-73.18__590c.png
```

Reglas de parseo que conviene respetar en cualquier script de lote: `PSNR: inf` (imágenes planas) y `S2` negativos son válidos; `colors` es el primer `N colors` de la línea `Encoded:` (la línea `Palette:` puede traer otro número). El script de la prueba vive en `out/tmp/png2amiga-mi1-batch.ps1` (efímero) y el manifiesto `manifest.csv` de `out/playground/png2amiga-mi1/` guarda una fila por conversión con todos los campos.

Coste medido: 258 conversiones (86 salas × 3 variantes) en **1,0 min** con esta build, ~0,23 s por imagen de 320×144. Con `--no-scale`, una panorámica de 960×144 tarda algo más pero conserva su tamaño de codificación; el PNG de preview se escribe al doble (2×2 en lores/HAM), así que 960×144 se ve como 1920×288 sin cambiar los datos codificados.

## Aplicación real: salas de Monkey Island 1

La prueba de conversión de las salas decodificadas del pipeline de aventura (`out/adventure/mi1/decoded/png/rooms/`) generó cinco familias sobre las 86 salas:

| Variante | Comando base | Qué demuestra |
|---|---|---|
| `dpf3-sliced-strips` | `--mode lores --dpf --sliced --strips --dither opt-checker` | Fondo DPF de 3 planos con paleta por línea y swaps a mitad de línea. |
| `lores32-plain` | `--mode lores --depth 5 --dither opt-checker` | Bitmap indexado de 32 colores, sin Copper. |
| `ham6-plain` | `--mode ham6 --dither none` | HAM6 base: 16 colores + MODIFY, sin sliced ni dither (el algoritmo tal cual). |
| `ham6-lum` | `--mode ham6 --dither none --ham-metric oklab-lum` | HAM6 base priorizando luminancia (mejor S2 en degradados; sin dither). |
| `ham6-atkinson` | `--mode ham6 --dither atkinson` | HAM6 con error diffusion (sin sliced). |

Todas las invocaciones añaden `--no-scale` para conservar las dimensiones y el aspecto del original, incluidas las panorámicas (960×144, 1008×144, etc.). Dither: `opt-checker` en dpf3 y lores32; ninguno en las dos bases HAM6 y `atkinson` en la variante difuminada. Resultados (medias sobre 86 salas, excluidas las planas con PSNR `inf`):

| Variante | Dither | PSNR media | Error medio | S2 medio |
|---|---|---:|---:|---:|
| dpf3-sliced-strips | opt-checker | 38.03 dB | 63.87 | 57.63 |
| lores32-plain | opt-checker | 39.18 dB | 40.01 | 65.78 |
| ham6-plain | none | 38.99 dB | 25.15 | 69.07 |
| ham6-lum | none | 37.97 dB | 34.79 | 71.39 |
| ham6-atkinson | atkinson | 41.58 dB | 19.13 | 72.51 |

Artefactos de la muestra: `out/playground/png2amiga-mi1/` (gitignored) con `manifest.csv`, `manifest.md`, `verification.md`, los PNG por variante en subcarpetas y hojas de contacto por sala en `contact-sheets/` (original + las 5 conversiones). La verificación con Ollama (`qwen3-vl:8b-instruct-q8_0`) cubrió las 86 salas en las 5 variantes: **430 comparaciones, 0 DEFECTUOSO, 357 RECONOCIBLE y 73 IGUAL**. Por variante: dpf3 7 IGUAL / 79 RECONOCIBLE, lores32 9 / 77, HAM6 base 22 / 64, HAM6+oklab-lum 13 / 73 y HAM6+atkinson 22 / 64; HAM6+atkinson sigue siendo la más fiel perceptual (mejor PSNR y S2 medios), `oklab-lum` la mejor de las bases sin dither por S2 (71,39 vs 69,07) aunque cambia más el color (menos casos IGUAL), lores32 la siguiente y DPF la más limitada por sus 8 colores base. El modelo menciona bandas horizontales percibidas en 24 casos de DPF, 25 de lores32, 19 de la base HAM6, 25 de `oklab-lum` y 15 de HAM6+atkinson; no hubo desplazamientos de contenido.

### Bandas de HAM6 base en degradados verticales (room_87)

Se ha investigado la hipótesis de que el cuantizador HAM6 discretiza los colores y no crea transiciones suaves. Resultado: **no se ha encontrado ningún error de algoritmo**; la limitación es la rejilla de 12 bits de OCS y que la propia fuente ya está tramada.

1. **La fuente ya está tramada**: en `room_87`, la fila y=12 del cielo alterna (100,156,204) 100 píxeles y (108,164,208) 92 píxeles (std por píxel ≈6). Su aspecto «suave» es una mezcla espacial ordenada de dos tonos, no un degradado continuo; un HAM sin dither no puede reproducir una mezcla que no existe en ningún píxel.
2. **Techo de OCS**: 12 bits (4 por canal) = 16 niveles por canal con pasos de 17/255. El mejor color posible por fila (equivalente a una paleta de cobre por línea) da solo **10 tonos distintos en las 72 filas** del cielo, con mesetas de hasta 13 filas; cualquier pantalla OCS (HAM, cobre, lores) tiene esa granularidad por línea. HAM8 (base de 24 bits y MODIFY de 6 bits) sí queda liso (53,03 dB / S2 90,37).
3. **El encoder está cerca del óptimo**: su error por fila es ~0,18× el del mejor color uniforme por fila (mediana 0,16; peor 0,33), porque aprovecha la variación horizontal; la distancia de las medias de fila al original es 2,76/255 en la base, 0,99 con atkinson, 1,45 con blue-noise (peor caso 2,68) y 0,33 en HAM8. Subir el beam a 4096 no cambia nada, una paleta externa con los tonos ideales del cielo tampoco (2,74) y `--sliced` sin dither apenas mejora (2,75). No hay inversiones de luminancia (0 pares con signo opuesto); lo que se percibe como inversión es el escalonado asíncrono de canales (G y B en meseta mientras R sube), que cambia el tinte dentro de la banda y salta al siguiente nivel.
4. **Corrección práctica**: como la fuente está tramada, el camino correcto es error diffusion. En este cielo `--dither atkinson --dither-strength 1.0` da 44,66 dB / S2 71,84 (mejor que el 0,80 por defecto: 43,71/71,81) y `blue-noise` tiene el mejor peor caso por fila (2,68) aunque peor S2 (45,87). No se cambia el default global de fuerza: subir atkinson a 1.0 mejora +0,14 S2 de media en las 86 salas pero regresiona `room_99` (−7,44). `--ham-smooth` es un prior horizontal y no actúa sobre bandas verticales.

**Métricas alternativas**: se implementaron `abc342` (pesos 3,4,2 de abc) y `oklab-lum` (OKLab² con L×2). `abc342` mejora PSNR (`room_87` base 38,83; atkinson 44,44) pero empeora S2 (56,71 base; 70,96 atkinson) y en las 86 salas pierde −3,66/−1,34 S2 → queda como opción de comparación, no como recomendación. `oklab-lum` sube S2 en la base (`room_87`: 63,86→67,52; 86 salas: +2,32, mejora en 77/86) y queda casi igual con atkinson (71,73), a costa de PSNR; es la mejor opción sin dither para banding de luminancia, aunque el grid de 12 bits sigue siendo el límite duro sin mezcla espacial.

Evidencia: `diagnostics/room_87_sky_variants_original_vs_ham6_vs_ham8.png` (original, base, atkinson 0.80/1.00, blue-noise, HAM8), `room_87_sky_ham6_base_vs_dither.png`, `room_87_sky_diff_ham6-plain.png`, el montaje de métricas `room_87_metrics.png` y las variantes completas de `room_87` (incluidas `oklab-lum` y `abc342`). La visión confirma el patrón de dos tonos en el original, los escalones en HAM6, que atkinson 1.00 se acerca más y que HAM8 queda liso.

### SHAM5b (flicker, 31 niveles)

Extraído de `abc-bitmap-converter`: cuantizar a **RGB555** con dithering (Sierra), partir cada canal de 5 bits en dos valores **RGB444** (suelo/techo) con intercambio de tablero `(x^y)&1`, codificar **ambos frames como SHAM** (HAM6 + paleta por línea) y alternarlos a 50 Hz. El ojo integra el par y percibe ~31 niveles por canal (~29.791 colores) en una máquina OCS de 12 bits.

La herramienta es `png2amiga/tools/sham5b.py` (delega los dos encodes SHAM al binario) y escribe `<prefijo>-a.png`, `<prefijo>-b.png`, `<prefijo>-avg.png` (promedio por píxel, lo que percibe el ojo), `<prefijo>-flicker.gif` y `<prefijo>-flicker-60hz.png` (APNG). El GIF guarda retrasos en centésimas, así que 16,666 ms no es representable: el GIF se redondea a 20 ms y el APNG lleva los 16,666 ms (17 ms) de la previsualización a 60 Hz; `--frame-ms` permite ajustarlo (con 10 ms el GIF corre tan rápido como permita el monitor). **Respuesta a la duda de un PNG o dos**: por duplicado; un PNG estático no puede contener la alternancia temporal, y el promedio es solo una aproximación de la percepción. Para Amiga, los dos frames son dos buffers que se intercambian en el VBlank (o una imagen SHAM de doble altura, como hace abc).

Resultados en las 86 salas (`out/playground/png2amiga-mi1/sham5b/`): el promedio temporal da **PSNR 40,01 dB / S2 78,81** (los frames individuales, 38,4 / 67,6), frente a 41,58 / 72,51 de HAM6+atkinson: **+6,3 S2**, el mejor perceptual de toda la matriz. La verificación estática con Ollama (prompt neutro, sin mencionar flicker) da **51 IGUAL / 35 RECONOCIBLE / 0 DEFECTUOSO**; con un prompt que menciona el flicker el modelo inventa defectos temporales en una imagen fija, así que la comparación válida es la estática. Coste: la diferencia entre A y B es ~5,96/255 de media (~30% de píxeles con al menos medio paso de 4 bits), es decir, **flicker a 25 Hz** visible en zonas planas; es el precio de los 31 niveles. Hay hojas de contacto propias en `sham5b/contact-sheets/` (original + A + B + promedio) y GIF por sala.

### Por qué HAM6 sin dither bandea (y por qué con atkinson no)

En `room_1` (cielo y agua con degradados suaves) HAM6 sin dither aplana las transiciones: en la franja superior (filas 0-47) solo conserva 34 colores únicos frente a 76 del original, con PSNR 36.36 dB y S2 71.27. No es un problema de búsqueda: subir `--ham-beam` a 128 y `--ham-triple` a 32 deja el resultado prácticamente igual (36.41 dB / S2 71.25). Es la ausencia de error diffusion: sin ella el beam search elige la operación más cercana por píxel y no sintetiza los tonos intermedios. Con `--dither floyd-steinberg` sube a 38.27 dB / S2 73.07 y con `--dither atkinson` a 39.50 dB / S2 73.18; en la misma franja, la versión con atkinson conserva 58 colores únicos y `--best` con FS llega a 68 (37.93 dB / S2 74.11, a ~20-30× coste). HAM8 sin dither en la misma sala da 49.88 dB / S2 89.04: confirma que el techo de HAM6 es la precisión (paleta base de 12 bits más MODIFY de 4 bits), no el codificador; en OCS la solución práctica es HAM6 con error diffusion.

Frente a la impresión de que «HAM6 con dither no se diferencia mucho de 32 colores»: sobre las 86 salas, HAM6+atkinson supera a lores32+opt-checker en S2 en 71/86 salas (media +6.73) y en PSNR en 69/86; las excepciones son salas concretas con mucho patrón (room_73, -11.87 de S2). La ganancia se concentra en degradados (room_76 +23.15, room_11 +21.22). La evidencia visual está en `out/playground/png2amiga-mi1/diagnostics/`: recorte 3× `room_1_sky_ham6_compare.png` (original, none, FS, atkinson, best-FS y lores32) y las variantes completas de `room_1` (incluida HAM8 AGA como diagnóstico de precisión).

### Suavizado de transiciones (`--ham-smooth`)

Para el problema de los saltos bruscos en degradados (la arena de `room_1`), png2amiga expone un argumento adicional de línea de órdenes: `--ham-smooth <0-8>` (0 = off). Añade un prior de suavidad al DP de HAM: cada operación candidata paga una penalización `w · max(0, d(salida, salida_anterior) − d(origen[x], origen[x−1]))`, con las distancias en la métrica activa y la referencia tomada de la **scanline original pre-dither**; así el ruido del dither no anula el prior. Está **gated**: solo actúa donde el origen es plano en una ventana de 5 píxeles, de modo que bordes, texto y texturas no se tocan. En degradados el codificador prefiere cadenas de un paso MODIFY frente a saltos a un color SET lejano — son los «píxeles de transición».

Medido en `room_1` (franja de arena, filas 96-144): los saltos de salida >0.05 OKLab sobre zonas suaves bajan de 1131 (sin prior) a 1009 (0.25), 607 (0.5) y 397 (1.0). Coste sobre las 86 salas: S2 medio 72.51 → 72.44 con 0.25 (casi neutro; pierde en 51/86) → 71.44 con 0.5 (−1.07; pierde en 68/86); en `room_1` el PSNR pasa de 39.50 a 38.81 (0.25), 37.64 (0.5) y 35.98 (1.0). La visión local confirma que 0.5 suaviza la arena conservando textura y que 1.0 ya aplana. **Rango recomendado: 0.25-0.5** cuando la escena tenga degradados amplios. Requiere el encoder DP: se rechaza con `--ham-fast` y con los dithers palette-aware, y no aplica a HAM6+`--strips` (error explícito). Evidencia: `diagnostics/room_1_sand_hamsmooth_compare.png` y las variantes `hamsmooth0.25/0.5/1.0` de `room_1`.

Notas de la fuente: las salas originales miden entre 8×144 y 1008×144 (42 son 320×144; hay panorámicas de 960/1008 px) y ahora **todas conservan su tamaño** con `--no-scale`. El PNG de preview se escribe al doble (2×2 en lores/HAM), así que una sala de 960×144 produce un preview de 1920×288, pero los datos codificados siguen siendo 960×144. Las salas casi vacías (22, 58, 88, 90 y las de 8 px) dan PSNR `inf` y S2 100 porque el origen tiene un solo color.

## Limitaciones y trampas

- `--strips` **no escribe `.iff` ni `.raw`**: los MOVEs de Copper no caben en ILBM; usa `.h` o `.cpp`.
- Sliced y strips son **OCS lores sin interlace** (excepto EHB/HAM6 que tienen su tabla propia); en AGA el presupuesto de swaps es mucho menor.
- `--json` no lleva métricas; para lotes que necesiten PSNR/S2 se parsea la salida de estado.
- Los assets generados van a `out/` (gitignored); un resultado canónico de texto (informe, tabla) va a `docs/` o `artifacts/`. La salida de experimentos de calidad va a `out/playground/<experimento>/`.
- No sustituye al pipeline de tiles: para EHB/tiles hay reglas propias en `REGLAS_PIPELINE_TILES.md`.

## Referencias

- `C:\Users\dvdjg\Documents\programa\AI\Amiga\png2amiga\README.md` (catálogo completo de flags y técnicas) y el `--help` del binario.
- Pipeline de tiles/EHB del repo: `docs/guides/roadmap/REGLAS_PIPELINE_TILES.md` y `docs/demos/tile-pipeline/PIPELINE_TILES_EHB.md`.
- Importación de aventuras clásicas: `docs/guides/roadmap/ROADMAP_ADVENTURE_IMPORT.md` y `tools/adventure/WORKSPACE.md`.
