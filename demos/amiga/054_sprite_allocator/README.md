# Demo 054: SpriteAllocator — asignación de canales con overflow → BOB

Consume la capa de objetos del engine (`docs/engine/architecture/OBJECT_SYSTEM.md`): la escena
describe actores (`ActorDesc`) y `compose_sprites` ordena, reparte canales, publica los
`SpritePlacement` (que `SpriteManager::apply` materializa en `SPRxPOS/CTL/PT`) y dibuja como BOB los
que no caben.

## Qué muestra

9 actores de 16×16 en fila horizontal (y=100). Los 8 primeros caben en los canales 0..7 (parejas de
color: rojo, verde, azul, amarillo); el noveno no cabe y queda `as_bob`. El reparto se publica en
`g_eng_run_status.detail` (`sprites << 8 | degradados`).

**`hpos` debe caer DENTRO de la ventana de display.** El display empieza en `DIWSTRT` (x≈128); los
sprites situados a la izquierda de ese punto se dibujan en el **borde** y no se ven. Con
`kHpos0 = 16` solo se veían los dos últimos pares que entraban en la ventana (y en azul, porque
eran los canales 4/5, cuyo par es el azul); con `kHpos0 = 144` se ven las tres primeras parejas.
Un SpriteManager o un canal que no dibuje en la ventana no es un defecto de emisión: las sondas
`tools/debug/probe-sprite-emission.mjs` (lista decodificada + registros + DATA en una sola
ejecución) confirman que la copperlist, la paleta, `DMACON` y la DATA son correctos.

## Sondas de depuración

```bash
node tools/debug/read-sprite-regs.mjs       054_sprite_allocator   # registros de sprite
node tools/debug/decode-copper.mjs          054_sprite_allocator   # lista desde COP1LC
node tools/debug/probe-sprite-data.mjs      054_sprite_allocator   # DATA + paleta + display
node tools/debug/probe-sprite-emission.mjs  054_sprite_allocator   # todo, en una sola ejecución
```

## Build & run

```bash
tools/build/build-demo.sh demos/amiga/054_sprite_allocator --clean
tools/run/run-demo.sh       demos/amiga/054_sprite_allocator
```
