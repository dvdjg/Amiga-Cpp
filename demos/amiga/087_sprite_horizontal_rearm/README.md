# Demo 087: rearmado horizontal de sprite (multiplexado por línea)

> **Estado: NO VERIFICADA (2026-09-18).** La API de emisión está implementada y probada
> en host (HOST-089), y la técnica está **confirmada contra la copperlist real** (ver
> abajo), pero **la demo aún no dibuja**: no encuentro la secuencia exacta de emisión que
> reproduce el efecto en WinUAE. Pendiente de depurar con la receta de abajo.

## Qué pretende mostrar

`Scheduler::emit_sprite_horizontal_reposition` (+ `emit_sprite_horizontal_rearm`): un
**mismo canal** de sprite reaparece **varias veces en la misma línea** con la misma imagen
(patrón repetido), porque el Copper mueve `SPRxPOS` mientras el haz barre. Es la base de
los fondos continuos tipo **Risky Woods** / R-Type 2 / Jim Power
(ver `docs/reference/amiga/techniques/sprite-horizontal-multiplex.md`).

Objetivo visual: el canal 0 dibuja 4 tramos de 16 px en la misma línea (48, 80, 112, 144
px), en una banda de `kLineas` líneas.

## Receta confirmada (copperlist real de Risky Woods, codetapper)

El truco **NO** reescribe DATA ni CTL por tramo: **solo `SPRxPOS`**. La copperlist real
repite, por cada scanline de la banda:

```
Wait vpos>=0x38 hpos>=0x30
SPR0POS := 0x3848      ; mueve a la 1ª posición (0x48)
SPR1POS := 0x3848
SPR2POS := 0x3850      ; +8 = 16 px respecto a la anterior
...
SPR0POS := 0x3868      ; segunda pasada del mismo canal, 0x20 más a la derecha
...                     ; (se repite hasta cubrir el ancho)
```

- **DMA de sprites ON**: los `SPRxPT` se programan **una vez** al inicio de la frame; el
  DMA carga POS/CTL/DATA y **arma** el sprite. El Copper luego **solo** mueve `SPRxPOS`.
- **No hace falta reescribir CTL/DATA**: el armado persiste y el patrón se repite.
- **Headstart**: el primer WAIT es a `hpos=0x30` (24 px) y los sprites empiezan en `0x48`;
  el Copper "corre por delante" del haz.
- **Reset**: al final de cada línea, resetear las posiciones al principio (izquierda).

## Qué se ha verificado

- **Emisión correcta** (HOST-089, verde): codificación AHRM de `SPRxPOS`/`SPRxCTL`,
  secuencia WAIT+POS+CTL+DATA+DATB del rearm, `reposition` (solo POS), no toca `SPRxPT`,
  y orden por `hpos` de la lista.
- **La técnica existe y es la de arriba** (AHRM cap. 4 "Manual Mode" + copperlist
  desensamblada de Risky Woods).

## Diagnóstico abierto

Con la receta de arriba la demo **no dibuja** todavía. Hipótesis por probar, en orden:

1. **Reproducir primero la 053** (`053_sprite_multiplex`, que **sí dibuja**) y cambiar
   **una sola cosa**: pasar de `emit_template_into` (que usa `wait_line` solo-V) a
   reposicionar con `wait_position` (V+H). Aislar si el fallo es el WAIT H.
2. **Unidades del WAIT H**: `SPRxPOS` codifica `HSTART[8:1]` (pasos de 2 px) y el WAIT
   compara en color-clock/2; comprobar si el `hpos` que se pasa al WAIT debe ser el doble
   (o si `& 0xfe` lo desalinea).
3. **VSTOP/VSTART de la semilla**: que `kVStart`/`kSpriteHeight` del seed cubran la banda
   entera (el sprite debe quedar armado en todas las líneas del efecto).
4. **Orden DMACON/seed**: la 053 resetea POS/CTL a 0 **antes** de SPREN; verificar esa
   secuencia con el reposicionamiento.

## Ejecutar

```bash
bash ./tools/build/build-demo.sh demos/amiga/087_sprite_horizontal_rearm --debug --clean
bash ./tools/run/run-demo.sh demos/amiga/087_sprite_horizontal_rearm
```
