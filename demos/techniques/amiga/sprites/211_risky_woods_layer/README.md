# 211_risky_woods_layer — fondo de 3 franjas con `effects::RiskyWoodsLayer` (alto nivel)

**Equivalente a la [208_risky_woods](../208_risky_woods/README.md) pero sin Copper a mano**: cada
franja es una `eng::effects::RiskyWoodsLayer` y el juego solo **describe las capas** (canales,
patrón, paleta por banda, reset); la capa resuelve el reparto de canales, el `WAIT`/head-start, la
reposición de `SPRxPOS`, la paleta por banda y el reset. Entra por `eng/api/api.hpp` (la fachada ya
incluye `effects.hpp`).

## Contrato que ilustra (uso de alto nivel)

```cpp
eng::effects::RiskyWoodsLayer::Config cfg {};
cfg.first_line = 48u; cfg.lines = 80u; cfg.channel_first = 0u; cfg.channels = 8u;
cfg.column_width = 16u; cfg.screen_width = 320u;
cfg.attach = false; cfg.burst_no_wait = false;      // par attached -> attach + burst_no_wait
cfg.palette = pal; cfg.palette_first_reg = 16u;     // paleta por banda (COLOR16..)
cfg.reset_at_end = true;                            // sin columna fantasma hacia la siguiente
cfg.dma_data = tiles; cfg.dma_stride = kStride;     // estructuras DMA (cabecera + DATA)
layer.attach(cfg);
layer.emit_into(sched);                             // la capa emite su tramo de Copper
```

Tres capas (A: 8 canales/128 px · B: 6/96 · C: 4 pares *attached*/64), con **una línea de guarda**
entre bandas (`first_line` 48/129/210) y **paleta por banda** (A y C). Los objetos irían por la vía
de actores (`SpriteScene` → `compose_sprites`) en los canales libres.

## Estado

- Las **3 franjas renderizan por la capa** (colina, valle, anillos *attached*) **a ancho completo**
  (validado por visión). Ajuste: `screen_width` = **borde derecho** (`128 + 320 = 448`),
  `arm_hpos=0x30`, `head_start=32` (1.ª columna en X=128).
- **Pendiente**: la franja C (*attached*) deja un hueco en el centro de la banda; y el **scroll**
  (la capa `RiskyWoodsLayer` solo tiene `set_scroll`+re-emit; le falta un `bind`/`patch` ~0 CPU).

## Lanzar

```bash
bash ./tools/build/build-demo.sh demos/techniques/amiga/sprites/211_risky_woods_layer --debug
bash ./tools/run/run-demo.sh demos/techniques/amiga/sprites/211_risky_woods_layer --keep-running
```
