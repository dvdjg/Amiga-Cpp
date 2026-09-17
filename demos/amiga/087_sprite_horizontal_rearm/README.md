# Demo 087: rearmado horizontal de sprite (multiplexado por línea)

> **Estado: NO VERIFICADA (2026-09-18).** La API de emisión está implementada y probada
> en host (HOST-073), pero **la demo no reproduce todavía el multiplexado horizontal
> correcto**: con el canal en modo manual sin sprite DMA aparece **una barra verde
> vertical** (el armado persiste y el reposicionado `SPRxPOS` no toma), no los tres
> tramos de 16 px en la misma línea. Pendiente de depurar antes de dar la técnica por
> validada en hardware. Ver "Diagnóstico abierto".

## Qué pretende mostrar

`Scheduler::emit_sprite_horizontal_rearm` / `graphics::SpriteHorizontalRearm`: un **mismo
canal** de sprite reaparece **varias veces en la misma línea** con imagen distinta, porque
el Copper reescribe `SPRxPOS`/`SPRxCTL` y recarga `SPRxDATA`/`SPRxDATB` mientras el haz
barre. Es la base de los fondos continuos tipo **Risky Woods** / **Free Form Sprite
Layer** (ver `docs/reference/amiga/techniques/sprite-horizontal-multiplex.md`).

Objetivo visual: el canal 0 dibuja 3 tramos de 16 px en la misma línea (izquierda, centro,
derecha) con imágenes distintas (bloque lleno, damero, rayas), en 3 líneas delgadas a
distintas Y.

## Qué se ha verificado

- **Emisión correcta** (HOST-073, verde): codificación AHRM de `SPRxPOS`/`SPRxCTL`,
  secuencia WAIT(posición)+POS+CTL+DATA+DATB, no toca `SPRxPT`, orden por `hpos` de la
  lista.
- **En hardware**: la demo alcanza READY, la copperlist se ejecuta (10 WAITs = 3 líneas ×
  3 rearms + el final), y los registros de sprite responden (el sprite se dibuja).

## Diagnóstico abierto

1. **Con sprite DMA ON**: el DMA recarga POS/CTL/DATA de `SPRxPT` en cada H-Blank y
   **pisa** las escrituras manuales del Copper → no se ve nada.
2. **Con sprite DMA OFF** (modo manual, AHRM cap. 4): el sprite se **arma** con `SPRxDATA`
   pero aparece como **barra vertical** en la izquierda: el `SPRxPOS` del rearm no
   reposiciona y el armado persiste entre líneas.
3. Hipótesis a probar: (a) el modo manual exige **desarmar** entre tramos
   (`SPRxCTL` con VSTOP=VSTART) y volver a armar; (b) la secuencia real de la fuente usa
   **DMA para la primera pasada y luego rearms**, no DMA OFF total; (c) el orden de
   escritura (¿`SPRxDATB` antes que `SPRxDATA`?) o el timing del WAIT respecto a la
   posición del haz.

## Ejecutar

```bash
bash ./tools/build/build-demo.sh demos/amiga/087_sprite_horizontal_rearm --debug --clean
bash ./tools/run/run-demo.sh demos/amiga/087_sprite_horizontal_rearm
```
