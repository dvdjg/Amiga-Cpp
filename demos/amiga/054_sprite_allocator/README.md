# Demo 054: SpriteAllocator — asignación de canales con overflow → BOB

> **ESTADO: a revisar.** La demo destapa un bug latente en `SpriteManager::emit_into`
> (el camino de 8 canales, nunca ejercitado antes): los sprites NO se dibujan
> aunque la copperlist los emite (el `emit_template_into` de la demo 053, con
> `wait_line` por segmento, SÍ funciona). Queda pendiente diagnosticar por qué el
> camino de 8 canales (sin `wait_line`) no arma el DMA de sprites.

## Qué valida (el allocator sí funciona)

El `SpriteAllocator` (paso 4 de `ENGINE_DESIGN.md` §5) reparte `SpriteIntent`
entre los 8 canales hardware y decide el overflow (más de 8 sprites en la misma
franja → BOB). Su lógica pura está validada por el test host HOST-003; esta demo
intenta validar la integración allocator → `SpriteManager`.

Qué debería mostrar: 9 sprites de 16×16 en fila horizontal (y=100). Los 8
primeros caben en los canales 0..7 (parejas rojo/verde/azul/amarillo); el noveno
desborda a `as_bob`. El número de BOBs se publica en `g_eng_run_status.detail`
(byte alto) — la demo sí reporta `bob_count=1` y `copper_words=180` (los 8 sprites
sí se emiten), pero el DMA no los muestra.

## Build & run

```bash
tools/build/build-demo.sh demos/amiga/054_sprite_allocator --clean
tools/run/run-demo.sh       demos/amiga/054_sprite_allocator
```
