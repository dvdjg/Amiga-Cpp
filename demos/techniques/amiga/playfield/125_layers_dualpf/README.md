# 125 - layers_dualpf

Porte de `demoscene-repo-orig/effects/layers/layers.c`.

## Que hace el efecto

Dos imagenes pre-renderizadas (`background` 384x384 y `foreground` 384x384, 3 planos
cada una) se muestran **a la vez** en un **dual playfield** OCS de 6 planos:

- `background` -> PF1 (planos 1, 3, 5)
- `foreground` -> PF2 (planos 2, 4, 6)

Cada capa se desplaza de forma **independiente** con punteros (`BPLxPT`), scroll fino
(`BPLCON1`, un nibble por playfield) y modulo (`BPL1MOD`/`BPL2MOD`). Los colores cambian
por el Copper cada **8 lineas** leyendo dos gradientes verticales
(`bg-gradient`/`fg-gradient`). El wrapping vertical se resuelve con cambios de modulo
sincronizados con el barrido (`BPL1MOD/BPL2MOD`), no redibujando.

## Contrato de display (fidelidad al original)

```
DIWSTRT=0x2c81  DIWSTOP=0x2cc1  DDFSTRT=0x0030  DDFSTOP=0x00d0
BPLCON0=0x6600  BPLCON2=0x0024  BPLCON3=0x0c00
BPL1MOD=BPL2MOD=(384-(320+16))/8 = 6
scroll: bg_x/bg_y/fg_x/fg_y = normfx(SIN/COS(frameCount*12) * half) + half
```

## Decisiones de ingenieria (lesiones aprendidas)

- **Assets DMA a CHIP**: los bitplanes importados se copian a un bloque CHIP del engine
  antes de mostrarlos. Un asset leido por DMA fuera de CHIP se ve como basura (bug
  documentado en `docs/demos/effects/dx39-layers-original-analysis.md`).
- **Copper via `eng::copper::Scheduler`**: la demo expresa intenciones (MOVE/WAIT +
  `wait_line_safe`, port de `CopWaitSafe`), no escribe registro crudo ni maneja la
  codificacion de WAIT ni el overflow de vpos > 255.
- **Doble buffer de copperlist**: se reconstruye la lista de *back* cada frame y se
  instala (commit) una vez por frame; nunca se parchea la lista activa.
- **Archivos de asset** `*.c` importados tal cual con shims de tipos/section (como 116/117).

## Build / run

```
bash ./tools/build/build-demo.sh demos/techniques/amiga/playfield/125_layers_dualpf --debug
bash ./tools/run/run-demo.sh demos/techniques/amiga/playfield/125_layers_dualpf --warp
```

## Rendimiento

Medido con `tools/debug/measure-fps.mjs 125_layers_dualpf A500_debug`:
**~32.5 fps (1.5 campos)**, ~218k ciclos/frame. El coste dominante es la reconstruccion
de la copperlist por frame (~900 palabras) escribiendo en chip RAM durante el display.

Optimizaciones ya aplicadas:
- lista emitida solo donde hay banda (`!(y&7)`) o wrap, no por linea;
- `move32` (escritura de 32 bits, 1 store por MOVE) en los cambios de color.

Para llegar a **50 fps (1 campo, <142k ciclos)** falta bajar el coste de escritura de la
copperlist. Caminos concretos:
- precomputar la ESTRUCTURA de la lista una vez y **parchear solo los datos** que cambian
  por frame (colores/ punteros / modulos) via `move_at`/`patch_data`;
- o construir la lista durante el VBlank (triple buffer de copperlist) para no competir
  con el bus durante el display.

## Pendiente
- Comparar la captura con el original y anotar cualquier desviacion de paleta/fase.
- Cerrar el 50 fps con el esquema de parcheo/construccion en VBlank.

