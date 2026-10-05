---
name: amiga-chipset-debug
description: Use when debugging Amiga chipset/hardware behavior (Copper, sprites, Blitter, DMA, collision, custom registers) or when a demo's visual output doesn't match intent and the cause might be hardware. Forces reading the WinUAE-DBG emulator source and the AHRM before any experiment, and citing file:line.
---

# Depurar comportamiento de chipset (Amiga)

Regla dura (AGENTS §0 y §1.12): **no redescubrir por prueba y error lo que está escrito en la fuente.** La implementación del emulador es la referencia de facto.

## Disparador

En cuanto un mecanismo de hardware **no cuadre** —píxel o color que no sale, sprite tapado, elemento desplazado, fila/columna con basura, parpadeo, glitch de Copper—, **PARAR** antes de construir diagnósticos o experimentos.

## Procedimiento

1. **Leer la fuente local primero:** `../WinUAE-DBG/` — `custom.cpp` (registros custom: handlers de escritura/lectura), `drawing.cpp` (render por píxel/línea: sprites, colisión, playfield), `include/custom.h` (mapa de registros), `cfgfile.cpp` (preferencias). Localizar con `grep -rnE '<REG>|<término>'`.
2. **Consultar el índice «síntoma → fichero» y las fichas por tema:** `docs/reference/emulators/winuae/README.md`.
3. **Contrastar con el AHRM** (`docs/reference/ahrm/`) y anotar la discrepancia en `ERRATA_Y_NOTAS.md` o la ficha.
4. **Citar `fichero:línea`** en el comentario del código y en el commit. Sin `fichero:línea` el hallazgo **no está hecho**.
5. **Validar en emulador** con una demo (caso positivo **y** negativo).

## Índice síntoma → fuente (ampliar al encontrar más)

| Síntoma | Fuente |
|---|---|
| Prioridad de sprites / selección de color | `drawing.cpp:4226-4266` (`denise_render_sprites`), `sprite_offs` |
| ATTACH (bits altos que aporta el canal impar) | `drawing.cpp:2722`, `4239` |
| Reuso de sprite dentro de la línea | `drawing.cpp:2656-2664` |
| Estructura de sprite / cabecera / columna fantasma | `sprite-dma.md`, `custom.cpp:10055-10120` |
| Colisión de sprites | `custom.cpp` (`CLXCON`/`CLXDAT`), `collision.md` |
| Copper: autoridad de escritura | `copper.md` (`COPCON`/`CDANG`) |

## Al terminar

- Ficha nueva o ampliada en `docs/reference/emulators/winuae/<tema>.md`, indexada en su `README.md`.
- Enlazar la fuente desde el comentario del código (`fichero:línea`).
