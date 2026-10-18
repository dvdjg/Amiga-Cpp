# INTENT - 054_sprite_allocator

## Qué hace

Tutorial del **sistema de objetos** del engine (`OBJECT_SYSTEM.md`): la escena describe ACTORES (`ActorDesc`) y `compose_sprites` hace el trabajo — ordena por superficie y `z`, construye una `SpriteIntent` por actor, reparte canales con el `SpriteAllocator` (**multiplexado vertical**), publica los `HwSpritePlacement` que caben (el `SpriteManager` los materializa en `SPRxPOS/CTL/PT` dentro de la copperlist) y cuenta los que no caben (`as_bob`). Nueve actores de 16×16 con **animación de bitmap de 2 frames** (§7) y, desde la mejora de contenido, **movimiento lissajous con x e y** sobre un fondo estructurado.

## Qué pretende demostrar

- El **reparto por presupuesto** (8 canales HW, 4k palabras de BOB): con los actores alineados en la misma franja de líneas el noveno no cabe (8+1); al moverse por líneas distintas el allocator los **multiplexa** (9+0) — el invariante es la suma.
- Que la **animación de sprite** publica la DATA del frame vigente y el canal cambia de imagen solo, sin que el juego toque registros.

## Elementos en pantalla

- **Fondo estructurado** (pintado por CPU al init, índices que el copper no toca): suelo verde (6), horizonte, tres montañas (2) con cumbres claras (14), sol naranja (9) y árboles.
- **Nueve barras de sprite** 16×16 (DAT/DATB, animación de 2 frames: barra completa → barra central), cada par de canales con su color (rojo/verde/azul/amarillo), con lissajous de x e y.
- **Depuración**: ventana de debug (run-status `sprites << 8 | degradados` + contadores `fps10`, `comp_ok`, `comp_bad`, `split`).

## Evolución temporal

1. **Init**: fondo + hoja animada + 9 actores + composición + copperlist (reset de canales, DMACON, paleta); READY con `sprites << 8 | degradados`.
2. **Frames**: cada 2 frames los actores avanzan su animación y su lissajous, se **recompone** y se re-emite la copperlist; el resto de frames solo actualiza telemetría (la re-composición por frame duplicaba la espera de VBlank del `install_copper_list` en polling: 25,0 → 33,2 fps).
3. **Cada composición**: self-check de memoria — `sprites_hw + degradados == 9` (`comp_bad` debe quedarse en 0) y `split` publica el reparto vigente.
