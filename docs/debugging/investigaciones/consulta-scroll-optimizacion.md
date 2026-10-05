# Consulta técnica: optimización de los algoritmos de scroll por tiles en Amiga 500 para 50 fps

> Pregunta autocontenida para consultar el diseño óptimo (a Grok u otro). No requiere el repo.

## 1. Objetivo y criterio de aceptación

Motor 2D para **Amiga 500 (OCS, PAL)**. Necesito implementar scrollers por tiles que corran **holgadamente a 50 fps** (1 frame lógico = 1 VBlank PAL) con **uso de CPU ridículamente bajo en el hotpath** (idealmente < 5–10 % del frame; el resto del frame libre para la lógica del juego). Si un algoritmo no llega a 50 fps con CPU baja, **no vale**.

Requisitos concretos:
- **Todas las variantes "limited"** de scroll: X, Y, y XY (8 direcciones), con mapa acotado (clamp) y toroidal (wrap).
- **Tiles de 16×16 y de 32×32** (bitmap interleaved, 4–6 planos).
- **Pasos de scroll de 1 px/frame hasta 16 px/frame** (16 px = tile completo de 16, o medio tile de 32).
- Doble playfield **DPF 3+3** como opción (fondo + capa de objetos).

## 2. Restricciones de hardware (A500 OCS PAL)

- CPU **68000 a 7,09 MHz**; memoria **Chip RAM** (compartida con DMA) + Slow/Fast opcional.
- Frame PAL no entrelazado: **312 líneas**, ~**227 slots de bus por línea** → ~**70 800 slots/frame**.
- Los slots **impares** son DMA de tiempo real (refresh 4, disco 3, audio 1/canal, sprites 2/canal). Los **pares** los comparten **Copper > Blitter > CPU**.
- **Bitplanes**: cada plano lores de 320 px consume ~20 palabras/línea. 4 planos ≈ 80 slots/línea; **DPF 3+3 = 6 planos ≈ 120 slots/línea**, dejando ~107 para Copper/Blitter/CPU.
- **Copper**: ~2 slots/MOVE, 3/WAIT. Reprogramar `BPLxPT`/`BPLxMOD` es barato; **re-emitir** una lista larga no.
- **Blitter**: ~1 palabra/ciclo (con A/B/C/D puede ir más lento); **hay que esperar a que termine** antes de reprogramarlo. El Blitter pinta un destino planar interleaved como una columna "alta" (blit único de N palabras × M planelíneas).

## 3. Variantes de referencia (paquete ScrollingTricks de Steger)

Once variantes, con bloque 16×16, 4 planos, ventana 320×256, monobuffer, bitmap interleaved:

| Variante | Ejes | Bitmap | Fetch | Video-split |
|---|---|---|---|---|
| XUnlimited | X | 704 | 1x2x4x | no |
| XLimited / XLimited_64 | X | 352 / 384 | 1x2x / 1x4x | no |
| YUnlimited | Y | 320×576 | 1x2x4x | no |
| YUnlimited2 | Y | 320×288 | 1x2x4x | sí |
| XYLimited / _64 | XY | 352 / 384 | 1x2x / 1x4x | sí |
| XYUnlimited / _64 | XY | 352×289 / 384×289 | 1x2x / 1x4x | sí |
| XYUnlimited2 / _64 | XY | 352×289 / 384×289 | 1x2x / 1x4x | sí |

"Limited" = mapa acotado (clamp); "Unlimited" = toroidal (wrap). Las X usan el **anillo X** (bitmap = viewport + guarda de 32/64 px); las XY usan el **corkscrew** (anillo X + anillo vertical de altura `viewport_h + 2*tile_h` con split de Copper); las Y usan un anillo vertical.

## 4. Diseño actual (dos caminos)

**A) "Corkscrew" (port de Scroller_XYLimited) — `ScrollEngine` + `XLimitedPlayfield`.**
- Bitmap interleaved de ancho `viewport_w + extra (32/64)` y alto `display_height = viewport_h + 2*tile_h` (anillo vertical). El display recorre el anillo y, al llegar a `split_line`, el Copper re-apunta a la base (split).
- Por cada píxel de avance pinta la **columna entrante** como **bloques de 16×16**, uno por **blit** (plane-shift por el shifter del Blitter), colocados alrededor del anillo; una **palabra de costura (saveword)** se guarda/restaura al cambiar de dirección.
- Avanza en **sub-pasos de 1 px**; hay un modo "burst" que calcula la geometría del cruce una vez por tile (mismos blits).
- Constraint detectado: la coordenada de mapa `mapy` llega a `tile_w + 1`, luego exige `bitmap_blocks_per_col >= tile_w + 2` → con tile 16 y anillo 288 se cumple (18 ≥ 18); con **tile 32 exige anillo ≥ 1088 líneas** (inviable) → parece **escrito para tile 16**.

**B) "Tile scroll driver" — anillo de Copper (BPLxPT/módulo) + tiras.**
- Construye la copperlist una vez y por frame **parchea** `BPLxPT`/`BPLxMOD` (no re-emite).
- Rellena con Blitter las columnas/filas entrantes en una banda de staging.
- Cuadro de referencia: single-playfield a 50 fps.

## 5. Medidas actuales (WinUAE, A500 PAL, release)

| Camino | Demo | fps | campos/frame |
|---|---|---|---|
| Tile scroll driver X single (5 planos) | 100/101 | **49,9** | 1,00 |
| Tile scroll driver XY **DPF** | 105 | 30,3 | 1,65 |
| Corkscrew Y | 110 | 14,5 | 3,44 |
| Corkscrew XY + **DPF** | 111 | ~17 | 2,9 |

Desglose medido de la 111 (A/B): **streaming de mundo ≈ 1 campo + scroll ≈ 1 campo + base DPF ≈ 1 campo**. Es decir, con DPF 3+3 el bus queda tan saturado que hasta blits diminutos (1 bloque 16×16) cuestan ~1 campo. Blits por frame del scroll a 2 px: ~2–4 (diminutos).

## 6. Preguntas concretas

1. **¿Cuál es el mecanismo de display óptimo por variante** (anillo X, anillo Y, corkscrew XY, split por línea) para 50 fps en A500? ¿Conviene unificar en el anillo de Copper + tiras (que sí da 50 fps single) y descartar el corkscrew, o el corkscrew puede optimizarse hasta 1 campo?
2. **DPF 3+3 a 50 fps**: con 6 planos (~120 slots/línea) ¿es viable dejar CPU/Blitter suficientes? Si no, ¿cuál es el techo realista (25 fps?) y qué presupuesto de bus queda por línea?
3. **Pasos 2..16 px** (especialización): con tile 16×16 y 32×32, ¿cómo hay que pintar la franja entrante para que sea eficiente? Concretamente:
   - ¿un **px-burst** que pinte la tira de N px con la geometría del cruce calculada una vez (menos cálculos) basta, o hay que **fusión de bloques** (un blit por varias filas contiguas)?
   - Con tile 32×32 y paso de 16 px, el plane-shift **no es 0** (no cae en frontera de tile); ¿cómo dimensionar la guarda (en palabras) y cuándo emitir los blits para que nunca se revele un píxel sin pintar?
   - ¿conviene **pre-renderizar tiras** en la guarda y mover solo punteros (coste ~0 de Blitter/frame), y cómo se casaría con el split vertical del corkscrew?
4. **Hotpath**: qué precomputar en setup/compilación (tablas de punteros por columna/fila del anillo, geometría del cruce por tile, copperlist parcheable) para que el bucle por frame no haga más que unos pocos MOVEs de Copper y ≤ 1–2 blits por columna cruzada.
5. **Tile 32×32 en el corkscrew**: ¿cómo adaptarlo (el `mapy` que llega a `tile_w+1` parece atarlo a 16), o hay que hacer 32×32 solo en el camino de tiras?

## 7. Criterio

Para cada variante: build OK, **50 fps** (1 campo) en A500 PAL, **CPU baja**, sin huecos/tearing, con un test de continuidad a 1 px y a 16 px. Si algo es inherentemente imposible en hardware, documentar el límite y por qué, e implementar la mejor versión que sí alcance 50 fps.
