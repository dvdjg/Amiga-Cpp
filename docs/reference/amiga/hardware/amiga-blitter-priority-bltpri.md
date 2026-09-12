# Prioridad del Blitter — `BLTPRI` (blitter *nasty*)

`BLTPRI` (bit 10 de `DMACON`, alias **`DMAF_BLITHOG`**, "blitter nasty") decide si el Blitter cede o no ciclos de **Chip RAM** a la CPU mientras trabaja. Es la palanca que regula el reparto de bus entre CPU y Blitter en OCS/ECS.

## Qué hace exactamente

- **`BLTPRI = 1` (nasty ON)**: el Blitter tiene **prioridad absoluta** sobre la CPU en los ciclos de Chip RAM disponibles. La CPU casi no recibe ciclos de bus mientras el Blitter trabaja (salvo algunos huecos internos del propio Blitter según los canales usados).
- **`BLTPRI = 0` (nasty OFF)**: el hardware deja pasar un ciclo a la CPU cada cierto tiempo (aproximadamente cada 3–4 ciclos de Blitter). La CPU sigue avanzando (sobre todo si ejecuta desde Fast RAM o hace trabajo interno).

En ambos casos, el DMA de **display, audio, sprites y disco tiene prioridad más alta que el Blitter**.

## Registros

| Dirección | Nombre | R/W | Descripción |
|-----------|--------|-----|-------------|
| `$DFF096` | `DMACON` | W | Bit 10 = `BLTPRI`/`DMAF_BLITHOG`. Con `SETCLR` (bit 15): `$8400` activa, `$8400` sin el bit 15 (`$0400`) lo limpia. |
| `$DFF002` | `DMACONR` | R | Bit 6 = `BBUSY` (Blitter ocupado); bit 14 = `BBUSY` también en algunas fuentes/`BLTZERO`. Usar `BBUSY` para el *WaitBlit*. |

## Contextos en los que interesa (Blitter = cuello de botella, CPU sin trabajo útil)

1. **Muchos blits grandes o encadenados** (restaurar fondo + dibujar BOBs, rellenar columnas de tiles): el Blitter termina antes y luego empieza el siguiente frame.
2. **Esperas activas al Blitter (`WaitBlit`)**: encender *nasty* **solo durante el bucle de espera** evita que el propio *polling* de `BBUSY` robe ciclos al Blitter.

```asm
; Activar nasty
move.w  #$8400,DMACON
.wait
btst    #6,DMACONR          ; BBUSY
bne.s   .wait
; Desactivar nasty
move.w  #$0400,DMACON
```

3. **Secuencias densas** `blit1 → blit2 → … → blitN` sin cálculos útiles entre medias.
4. **Efectos/demos que exprimen el Blitter**: rellenos grandes, *clears*, copias de buffers.

## Cuándo NO conviene

- **La CPU tiene trabajo útil que solapar** (lógica de enemigos, posiciones, input, preparar la siguiente lista de blits): dejar `BLTPRI = 0` y solapar CPU+Blitter casi siempre gana.
- **Fast RAM + CPU 020/030**: la CPU avanza bastante incluso con el Blitter activo en modo no-nasty.
- **Latencia de interrupciones** sensible (menos crítico en un juego *bare metal* de A500).

## Estrategia práctica (la que usan los engines serios de A500)

1. `BLTPRI` **apagado** la mayor parte del tiempo → la CPU trabaja en paralelo.
2. **Encenderlo solo** durante un `WaitBlit` o una ráfaga de blits críticos sin trabajo de CPU.
3. **Apagarlo** en cuanto termina la secuencia crítica.

Resumen: *nasty* interesa **si y solo si** el Blitter es el recurso limitante **y** la CPU no tiene trabajo productivo que hacer al mismo tiempo.

## En el engine

- `MinimalBackend::set_blitter_priority(bool)` activa/desactiva el bit (sin tocar `MASTER`/`BLITTER`).
- **Medido en `080_fire_rgb`** (fire+blitter, `bus-bound`): activar *nasty* **empeoró** el frame (**12.44** vs **13.55** fps) porque la CPU (el fuego) sí tiene trabajo de Chip RAM que solapar; el bus es el cuello. Se deja como **palanca de diagnóstico** (`-DK_BLIT_NASTY=1`), no como default. Confirma la regla: con CPU útil que solapar, `BLTPRI = 0`.

## Referencias

- AHRM, capítulo del Blitter y `DMACON`/`DMACONR`.
- Ficha relacionada: [cpu-blit-assist.md](../techniques/cpu-blit-assist.md) (repartir trabajo CPU/Blitter en paralelo).
- Aplicación real: [C2P_BLITTER.md](../../../engine/architecture/C2P_BLITTER.md) §5 (cadena de blits del C2P).
