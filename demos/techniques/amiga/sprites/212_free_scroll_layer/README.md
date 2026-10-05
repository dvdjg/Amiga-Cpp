# 212_free_scroll_layer — fondo LIBRE (320 px) con `effects::FreeScrollLayer` (app limpia)

**App de alto nivel**: usa la fachada **`eng/api/api.hpp`** y una **sola capa**
`eng::effects::FreeScrollLayer<20>` (una imagen de 320 px partida en 20 columnas de 16). Igual que
la [209_free_scroll](../209_free_scroll/README.md) (misma técnica, Copper a mano) pero **sin tocar
registros**: la capa resuelve el reparto de canales, el `WAIT`/head-start, la DATA por columna y el
reset; la app solo **genera la imagen** y compone.

## Uso (ladrillo)

```cpp
eng::effects::FreeScrollLayer<20>::Config cfg {};
cfg.first_line = 48u; cfg.lines = 80u; cfg.channel_first = 0u; cfg.channels = 8u;
cfg.column_width = 16u; cfg.screen_width = 320u; cfg.columns = 20u;
cfg.bplcon2 = 0x0008u;                 // fondo (sprites) detrás del playfield
cfg.tiles = tiles;                     // DATA de la imagen (columns*lines*2 palabras)
cfg.reset_at_end = true;               // sin columna fantasma
layer.attach(cfg);
layer.emit_into(sched);                // la capa aporta su trozo de Copperlist
```

La capa es **paramétrica por el nº de columnas** (disposición fija en compilación: sin heap, sin
cola, sin despacho) y cumple el contrato `Effect` (`band_scope`/`apply_into`/`effect_cost`) para
**combinarse** con otros efectos en el mismo plan de Copper. La DATA es **fuente de CPU** (se copia
inmediata a la Copperlist): no es DMA, puede vivir en ROM.

## Estado

- Renderiza la imagen de 320 px (una sierra no repetitiva) por la capa; **sin fantasma** (`reset_at_end`).
- **Pendiente**: el look fino de la figura (el perfil sale a trazos; revisar el troceado por columna)
  y el **scroll** (la capa aún no tiene `bind`/`patch`; `RiskyWoodsLayer` tampoco — es el siguiente
  ladrillo común).

## Lanzar

```bash
bash ./tools/build/build-demo.sh demos/techniques/amiga/sprites/212_free_scroll_layer --debug
bash ./tools/run/run-demo.sh demos/techniques/amiga/sprites/212_free_scroll_layer --keep-running
```
