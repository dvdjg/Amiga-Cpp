# Validación — 099 Star Fox travel

## Estado

Render **plano por caras** con tres planos dinámicos (8 colores) y una luz común. La escena muestra tres cazas (jugador teal, dos enemigos salmon/granate) con caras de distinta intensidad, fondo espacial estático, estrellas y disparos. La validación visual es correcta, pero el rendimiento queda **por debajo del suelo de 25 fps**: 19,57 fps de media. Se documenta como excepción por el cambio visual solicitado; la variante de silueta de dos tonos a 26,42 fps está en el commit `fd04dc87`.

## Comandos

```bash
export AMIGA_BIN_PATH="$USERPROFILE/Documents/programa/AI/Amiga/vscode-amiga-debug/bin/win32"
export AMIGA_WINUAE_PATH='C:\Users\dvdjg\Documents\programa\AI\Amiga\WinUAE-DBG\bin'

./tools/build/build-demo.sh demos/techniques/amiga/3d/099_starfox_travel --release
./tools/run/run-demo.sh demos/techniques/amiga/3d/099_starfox_travel --config A500_release --settle-ms 1000
node tools/debug/measure-fps.mjs 099_starfox_travel A500_release --json

./tools/run/run-demo.sh demos/techniques/amiga/3d/099_starfox_travel --config A500_release --sequence-frames 8
node tools/analyze/verify-3d-sequence.mjs out/run/099_starfox_travel/A500_release/sequence --min-frames 8 --require-motion --require-distinct
./tools/test-regression.sh --flicker --require-flicker-ok --demo demos/techniques/amiga/3d/099_starfox_travel
```

## Rendimiento

La medida final se tomó en `A500_release` con la geometría engordada (hy 10/9/8), la paleta invertida para que la cara clara use un solo bit y el sol lateral.

| Versión | fps | Ciclos/frame | Campos/frame | Mínimo | Estado |
|---|---:|---:|---:|---:|---|
| **Caras reales (entrega)** | **19,57** | **362.399** | **2,554** | **18,9** | Bajo el suelo |
| Silueta de dos tonos (commit `fd04dc87`) | 26,42 | 268.504 | 1,893 | 25,21 | Cumple |
| Caras con `fill_screen_polygon` (variante antigua) | 16,87 | 420.472 | 2,963 | — | Descartada |
| Todo menos sólidos | 49,92 | 142.102 | 1,002 | — | Referencia |
| Solo sólidos (geometría hy 7) | 26,64 | 266.254 | 1,877 | — | Referencia |

El coste de los sólidos es ~220k ciclos/frame; el resto (clear de 3 planos, estrellas, balas, motor y HUD) ~142k. El render por caras es ~2x la silueta de dos tonos; por eso no cabe en 25 fps con este tamaño de nave. La ruta de menor coste para conservar las caras es pre-renderizarlas como BOBs/sprites.

## Técnica y paleta

- Tres planos dinámicos (0/1/2) y un plano estático (3). Los colores 0..7 son de las naves y la luz; 8..15 son fondo/HUD.
- Gamas: jugador teal claro/medio/oscuro (1/2/3), enemigo salmon/ladrillo/granate (4/5/6), luz cálida 7. Los índices iluminados usan un solo bit y las sombras dos, para reducir escrituras de plano.
- El sol está en `(-240, -170, -90)` y se calcula su dirección por objeto; la normal local de cada cara se rota con la nave y el producto elige nivel 0/1/2. El mismo sol afecta a las dos naves.
- El rasterizado de cada cara usa el paso 16.16 por arista y OR de los bits de color. El corredor se limpia en los 3 planos; los objetos se pintan de lejos a cerca. Las estrellas se pintan después y se saltan si el píxel no es negro, para no aparecer encima de una nave.

## Secuencia y movimiento

Secuencia de 8 frames (756x576, overscan PAL) en `out/run/099_starfox_travel/A500_release/sequence/`:

```text
frame_000 ... frame_007
7/7 cambios, 0 frames estáticos
áreas: 73964, 74808, 75576, 73992, 74240, 72604, 72960, 73480
```

`verify-3d-sequence --require-motion --require-distinct` exige cambios de píxeles y áreas distintas; ambas condiciones se cumplen. La inspección de los frames y la hoja de contacto confirman el desplazamiento de los enemigos, el cambio de orientación de las caras y el avance de las estrellas.

## Visión

Evaluación canónica: [`src/VISION.md`](src/VISION.md), generada por `tools/analyze/vision-final.mjs` con el prompt canónico y 3 frames (`frame_000`, `frame_004`, `frame_007`).

- Describe el objeto principal como triangular azul claro **con un borde más oscuro**, y los dos objetos rojizos; ve el movimiento entre imágenes y los contornos limpios, sin halos ni restos.
- Confunde los fps del HUD (`81.6`, `21.4`) y solo comenta dos de las tres imágenes; la medida real es `measure-fps`, no el texto.
- **Contraprueba con montaje** ([`out/vision-review/099_starfox_travel/099_montage_0_4_7.png`](../../../../../out/vision-review/099_starfox_travel/099_montage_0_4_7.png)): el mismo modelo responde que las naves se mueven, que cada nave tiene su color propio y que las caras muestran distintas intensidades por iluminación; contornos limpios y sin mezclas raras.

No se edita `VISION.md` a mano: es la respuesta cruda. La evidencia de intención es la secuencia + `verify-3d-sequence` + montaje.

## Flicker

El gate de flicker pasó con la nueva secuencia:

```text
Regression report: out/regression/20261025-014840/regression-report.md
099_starfox_travel | build ok | run ok | analyze ok | flicker ok
candidatos 16 / bloques 52  <= baseline 36 / 55
```

La baseline no necesitó recalibración. El render por caras añade más bloques que la silueta (52 frente a 49), pero sigue dentro del umbral.

## Host y regresión

- `plane_or_span` conserva la ruta por palabras en Amiga big-endian y la ruta por bytes en host; equivalencia cubierta por **HOST-217/474/479/481/482**.
- `./tools/test-regression.sh --flicker --require-flicker-ok --demo demos/techniques/amiga/3d/099_starfox_travel` → **Regression OK**; build, run y analyze OK, flicker OK. El aviso `raw-text-buffer: green=0` es el analizador genérico sin `vision-points.json`, no un fallo de la demo.
- Build release y debug sin errores; los warnings son los globales del engine (`volatile`) y el símbolo de audio no referenciado del support.

## Conclusión

La demo cumple la intención visual: caras reales, cada nave con su gama y una luz común que elige intensidad por ángulo, con una paleta corta y desaturada. **No cumple el presupuesto**: 19,57 fps frente al suelo de 25. La entrega se commitea por petición explícita y queda marcada como **no verificada en rendimiento**; la variante de dos tonos a 26,42 fps sigue disponible en el historial. La siguiente ruta para conservar caras y presupuesto es pre-renderizar las caras como BOBs/sprites.
