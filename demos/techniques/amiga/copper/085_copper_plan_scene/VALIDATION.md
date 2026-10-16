# VALIDATION — Demo 085 (`copper_plan_scene`)

Informe de validación (F4/F5 de `docs/guides/methodology/PROCEDIMIENTO_DEMOS_Y_JUEGOS.md`).
**Estado: VERIFICADA** — DT-007 cerrada (2026-10).

Intención esperada: escena con `copper::Plan`: cielo de 16 franjas de `COLOR00` en degradado que
cicla y un BOB (disco) que se mueve en Lissajous aportando color anclado a su Y (arcoíris por
scanline), con lista estática + parcheo por slots.

## Técnica vigente

La lista de Copper se construye **una vez por bitmap** en el setup (doble buffer del `Plan`, una
lista por bitmap: solo cambian los punteros `BPLxPT` del display) y por frame **solo se parchean
palabras de DATO**:

- cielo: 16 slots del efecto (`RasterGradientEffect::bind_slots` + `patch_words`) — la fase cicla;
- BOB: intents `PaletteLine` de `COLOR01` para **todas** las líneas posibles del disco
  (y ∈ [38, 218] → líneas [16, 240], posiciones fijas); por frame se parchea el rango
  actual ∪ anterior (arcoíris dentro del disco, color base fuera);
- disco: **dos blits** (borrado de la caja anterior con copia desde una fila de ceros repetida
  con módulo `-8` + copia de la variante pre-shifteada de 16 offsets de palabra), sobre bitmaps
  en doble buffer (el trasero se pinta mientras el delantero se muestra).

La ruta previa (re-emitir + re-ordenar las ~85 intenciones cada frame y dibujar el disco por CPU
byte a byte) costaba 426.7k ciclos/frame (3 campos); la vigente cabe en 1 campo.

## Mediciones

| Medida | Comando | Resultado | Lectura |
|---|---|---|---|
| fps debug (vigente) | `node tools/debug/measure-fps.mjs 085_copper_plan_scene A500_debug --json` | **49,92 fps · 1,00 field · 142.102 ciclos/frame** | 50 fps, presupuesto cumplido |
| perfil por secciones | `node tools/debug/profile.mjs 085_copper_plan_scene A500_debug` | blits del disco **20,6k** (12,9%) · parcheo **17,0k** (10,7%) · flip+commit 0,5k · resto (espera) 116k | trabajo real ≈ 38k de 142k → holgura amplia |

### Atribución histórica (ablaciones F1, ruta previa)

| Ablación | fps | fields | ciclos/frame | Atribución |
|---|---|---|---|---|
| baseline (ruta previa) | 16,62 | 3,01 | 426.733 | — |
| fase del cielo congelada | 16,62 | 3,01 | 426.733 | `rebuild()` del cielo **no** era el hotspot |
| sin intents del BOB | 24,99 | 2,00 | 283.920 | el BOB ≈ 143k; la base (materialize/commit) ≈ 284k |

Perfil por secciones de la ruta previa (con las secciones del engine dentro de `materialize`):
`sort_lines` 37,1k + `sort_prio` 32,7k + `emit` 123,7k = **194k de materialize para ~85
intenciones** (~2,3k/intención), más ~119k del resto de `build_frame` y ~101k del dibujo del
disco por CPU en Chip (contención). Los tres focos se eliminaron: materialize/sort solo en el
setup, parcheo por slots y disco por Blitter.

## Prompts y respuestas (crudos)

### A · Inventario sin contexto, una imagen (frame 4, ruta previa)

**Prompt:** «Es UNA captura de una animacion. No conoces el programa […]: 1) Inventario de elementos (forma, color, tamaño, cuántos). 2) ¿Algo que parezca basura, ruido, corte o zona incoherente? 3) ¿Hay un objeto nítido con forma propia sobre el fondo? Descríbelo. No inventes coordenadas.»

**Respuesta** (cruda, extracto literal):
> Bandas horizontales de color, al menos 12 […]. Un círculo blanco con borde blanco, ubicado en la esquina superior derecha, que contiene 8 franjas horizontales multicolores […] y un pequeño círculo rojo en el centro. […] **Sí. En la zona superior derecha, justo en el borde del círculo, hay una pequeña mancha de color blanco irregular que parece un artefacto de pixelización o un error de renderizado.** También […] el círculo no está alineado perfectamente con las bandas horizontales.

**Contraste:** acierta el inventario (franjas + disco multicolor con centro rojo). La «mancha
blanca» era el **ciclo de paleta del disco** (con fase congelada el conteo objetivo queda
constante), no corrupción.

### B · Secuencia vigente (3 frames separados 1 s)

**Prompt:** «…debe mostrar: fondo con degradado en bandas horizontales (cielo azul → violeta →
naranja) que cicla; disco con borde blanco, brillo arriba-izquierda e interior con arcoíris suave,
moviéndose. Para cada imagen: ¿degradado + disco? ¿la posición cambia? ¿artefactos (restos del
disco anterior, basura, bandas rotas, disco cortado)?»

**Respuesta** (cruda, extracto literal):
> IMAGEN 1: Fondo degradado y disco con borde blanco e interior multicolor están presentes. El disco se mueve hacia abajo. No hay artefactos visibles.
> IMAGEN 2: Fondo degradado y disco con borde blanco e interior multicolor están presentes. El disco se mueve hacia la derecha. No hay artefactos visibles.
> IMAGEN 3: Fondo degradado y disco con borde blanco e interior multicolor están presentes. El disco se mueve hacia la izquierda. No hay artefactos visibles.
> VEREDICTO: OK

Nota de proceso: la primera pasada de visión de esta tanda leyó una secuencia **vieja** del
directorio (`frame_NNN_fNNNN.png` de una captura anterior) y reportó un artefacto inexistente;
se re-capturó (`--sequence-frames 6 --sequence-interval-ms 500`) antes de validar.

## Conclusiones

1. La escena es **visualmente coherente**: degradado que cicla + disco con borde blanco, brillo
   e interior arcoíris, en movimiento y sin restos (visión OK sobre secuencia fresca).
2. **VERIFICADA por rendimiento**: 49,92 fps / 1,00 field (era 16,62 / 3,01).
3. La técnica vigente es el patrón de referencia para escenas de Copper animadas: lista estática
   (por bitmap) + `bind_slots`/`patch_words` para colores + BOB por Blitter con variantes
   pre-shifteadas. Ver `docs/guides/optimization/ATRIBUCION_DE_CICLOS.md`.

## Deuda registrada

DT-007 cerrada. DT-003/§6.4 (límites del montaje vs imagen única) sigue vigente como nota de
metodología.

## Defecto conocido (fuera del alcance de lo verificado arriba, ABIERTO)

Capturas frescas (2026-10, verificación del refactor tipado de copper) muestran el disco en
**negro** en parte de los frames (conteo objetivo de píxeles negros interiores alternando
6076/0/6076/3124). Evidencia de runtime (WinUAE-DBG): el bloque de copperlist que el Copper
ejecuta (`COP1LC = 0x25060`) cae **dentro del rango del bitmap 1** (`[0x1F060, 0x26860)`), de
modo que las palabras de la lista se leen como bits de plano y los píxeles del disco pasan a
colores negros (`COLORxx = 0x000`). Es **preexistente** (mismo defecto con el binario de `HEAD`
en un worktree limpio) y **no** lo introduce el refactor. La hipótesis del "4.º plano leyendo la
copperlist" quedó descartada (con 3 planos persiste; el demo usa 3 planos y `BPU=3` por
corrección semántica). Detalle, evidencia y plan de bisectado del allocator:
`docs/debugging/investigaciones/085-copper-solapa-bitmap.md`. La demo lleva en su `init` una
validación `RegionTable::validate_disjoint` de sus bloques (hoy pasa; deberá cazar el solape
cuando el allocator se corrija). Hasta cerrar la deuda, la demo queda **parcialmente
verificada**. El pixel-contract de 101 (defecto aparte, también preexistente) está anotado en su
propio `VALIDATION.md`.
