# INTENT - 217_sprite_template_actor

## Qué hace

Tutorial de la **plantilla de franjas** por el camino de actores ("chasing the raster"): el juego declara **una** `ActorDesc` cuyo `Visual` referencia una `HwSpriteTemplate` (bitmap + 4 segmentos + switches de paleta); el engine proyecta las franjas a intents **encadenados** (`sprite_template_to_intents` — todas exigen el MISMO canal y el `SpriteAllocator` lo reserva para el rango completo), publica **una placement por franja** (mismo canal, `vstart` con gap ±1), entrega la **paleta por franja** como `SpritePaletteEvent` (0-based, línea absoluta) y `SpriteManager::emit_placements_into` arma la primera franja en una línea temprana y **rearma** el canal en cada franja, intercalando la paleta en orden de línea. El juego no escribe `SPRxPT`/`SPRxPOS` ni conoce canales.

## Qué pretende demostrar

- Que **un solo canal** sirve a un objeto de varias franjas (el "tótem" de 4 segmentos con forma de losange), con paleta por franja, sin que el juego toque registros.
- Que la plantilla recorre **toda la pantalla en x e y** manteniendo el encadenado (la reserva del canal cubre el rango completo de franjas).

## Elementos en pantalla

- **Fondo estructurado**: bandas horizontales suaves (contexto del sprite) sobre navy.
- **Tótem de 4 franjas** (anchos 4/16/16/4, cuerpo en COLOR17 con switches de paleta por franja: rojo/naranja/verde/cian), 99 líneas de alto, recorriendo x = 144..398 e y = 44..186 con onda triangular.
- **Depuración**: ventana de debug (run-status `canal << 16 | sprites << 8 | degradados` + contadores `fps10`, `comp_ok`, `comp_bad`).

## Evolución temporal

1. **Init**: hoja del sprite + fondo + plantilla (4 segmentos) + actor + composición + copperlist; READY con `canal << 16 | sprites << 8 | degradados`.
2. **Frames**: el actor avanza su onda triangular en x e y; `compose` re-proyecta las franjas (intents encadenados + placements + eventos de paleta) y el copper se re-emite con el rearme por franja; swap de COP1LC.
3. **Cada composición**: self-check de memoria — `sprites_hw + degradados == 4` (una placement por franja; `comp_bad` debe quedarse en 0) y `fps10` cada segundo.
