# INTENT - 062_sprite_scene

## Qué hace

Demuestra la **fachada de sprites** (`eng::SpriteScene`): el juego da de alta actores (`ActorDesc`) y llama `emit`; el engine compone (sprites hardware + `SpriteAllocator` + degradado a BOB) sin que el juego declare buffers de trabajo ni un `FramePlan`. Nueve actores de 16×16 con **lissajous de x e y** (recompuestos cada 4 frames) sobre un fondo estructurado; el reparto HW/degradados se publica por contadores.

## Qué pretende demostrar

- Que la composición **reparte** los actores entre canales hardware y degradado a BOB según el presupuesto (8 canales, 4k palabras de BOB), y que el reparto se adapta al movimiento: con los actores alineados en la misma franja de líneas el noveno **no cabe** (8+1); al moverse por líneas distintas el allocator los multiplexa (9+0).
- Que el fondo estructurado (regla 25) no interfiere con los sprites (canales y paleta propios).

## Elementos en pantalla

- **Fondo estructurado** (pintado por CPU al init): cielo navy, suelo verde (índice 6), horizonte, tres montañas con cumbres claras (índices 2/14) y sol naranja (9) — el copper no toca esos índices.
- **Nueve bloques de sprite** 16×16 (DAT/DATB, terminador de canal), cada pareja de canales con su color (rojo/verde/azul/amarillo), con desplazamiento fino y lissajous.
- **Depuración**: ventana de debug (run-status `sprites << 8 | degradados` + contadores `fps10`, `comp_ok`, `comp_bad`, `split`).

## Evolución temporal

1. **Init**: fondo + hoja de sprites + 9 actores + composición + copperlist (reset de canales, DMACON, paleta, `emit_into`); READY con `sprites << 8 | degradados`.
2. **Frames**: cada 4 frames los actores avanzan su lissajous (x ±24, y ±18), se **recompone** y se re-emite la copperlist (las posiciones SPRxPOS/CTL viajan en ella). El resto de frames solo actualiza telemetría.
3. **Cada composición**: self-check de memoria — `sprites_hw + degradados == 9` (`comp_bad` debe quedarse en 0) y `split` publica el reparto vigente.
