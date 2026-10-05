# 204 — scroll de tiras declarativo por `App`

Tutorial de la **§7** de `ROADMAP_GAME_API`: el juego **no ve** el compositor, los buffers ni los
`BPLxPT`. Monta una `field::StripScrollLayer` y la registra con `app.add_scroll_layer(layer)`; el
`App` la **arranca** (memoria + backend) y la **conduce por frame**.

```bash
bash ./tools/build/build-demo.sh demos/techniques/amiga/playfield/204_app_strip_scroll --release
bash ./tools/run/run-demo.sh demos/techniques/amiga/playfield/204_app_strip_scroll
```

## Variantes de scroll (X, Y y XY)

El bitmap es **ancho de mapa + solape** (X, por tira) y **alto del bitmap** (`RingLines`): el scroll
X pinta la columna entrante (tira, Blitter); el scroll **Y es solo mover la punta de fila** (el
bitmap ya tiene las filas, sin split) → el MISMO camino hace X e Y.

La cámara es `ScrollRoute` (local): una **ruta de fases con tabla de seno fina (256 muestras) cuyo
índice avanza CADA frame**, de modo que **cada frame es una imagen distinta** (comprobado: 0 frames
idénticos en 900 consecutivos).

| Fase | Movimiento |
|---|---|
| 0 | horizontal (→, 1 px/f) |
| 1 | vertical (↓/↑, 1 px/f, con rebote) |
| 2 | diagonal (x avanza, y rebota) |
| 3 | **circular** (cos/sin de la tabla fina, radio 96) |
| 4 | **Lissajous 1:3** |

> `eng::scene::RouteCamera` (demo 101) **cuantiza el ángulo** a 1/64 de vuelta repartido en 512
> frames = **8 frames por paso**: el mismo píxel 8 frames seguidos → saltos. Por eso aquí se usa una
> tabla de 256 muestras que avanza en cada frame.

**Alto del bitmap**: `kRingLines = 448` (Y de 192 px) = recorrido vertical/diagonal. Anillo ≈ 158 KB
+ banco del atlas (111 KB) ≈ 270 KB. El mapa entero (640) serían 225 KB de anillo → **A1200/1 MB**.

## Qué demuestra

- **Capa declarativa**: `StripScrollLayer<Geom, TilemapView, Backend>` agrupa buffers +
  `StripScrollController` (CPU/Blitter) + `StripComposer` (Copper). El juego solo describe.
- **Asset de tilemap**: `field::TilemapView` (banco de tiles + mapa de ids + paleta) se liga con
  `layer.set_tilemap(view)`; **no** hay adaptador `tile_at` escrito a mano.
- **Conducida por `App`**: `app.add_scroll_layer(layer)` la arranca y `App` la conduce
  (`pump_scroll_layers` tras el `update`) leyendo la variable de scroll que el juego avanza
  (`layer.track_scroll(&m_scroll)`). El display lo posee la capa (su copperlist): el juego no llama
  `present()`.

## Puntos clave

- El display base de `App` (1 plano) sobra: la capa hace su propio `takeover`. El presupuesto de
  memoria se declara explícito (`configure_memory`) para que quepan **display + banco + anillo**.
- El anillo se dimensiona con `MapWords` (periodo del mapa) — ver demo 128 / HOST-244.

## Validación

Renderiza el pueblito y **desplaza en horizontal suave, sin huecos ni saltos** (validado con Ollama
sobre una secuencia).
