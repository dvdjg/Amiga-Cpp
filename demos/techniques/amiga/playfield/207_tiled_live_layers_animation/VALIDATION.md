# VALIDATION — 207_tiled_live_layers_animation

## Alcance

- Mapa `Beginning Fields` (40×40, 16×16) cocido con `cook-map` en perfil EHB
  (1183 tiles, banco 225 KB) y `animations.h` (44 grupos, 248 frames, 227 celdas,
  sheet 47,6 KB, máscaras 7,9 KB).
- Scroll **Y-only** a 1 px/frame, corkscrew XY con anillo 288 y ventana 208+HUD 48,
  cámara inicial en Y=300 y rebote cada 600 frames.
- Animación por parche de celdas visibles, repartida en 6 fases; base con el primer
  frame. **No** se compone overlay en la banda entrante ni se mueve X+Y a la vez.

## Medida

`node tools/debug/measure-fps.mjs 207_tiled_live_layers_animation A500_release --json`:

```json
{"demo":"207_tiled_live_layers_animation","config":"A500_release","date":"2026-10-25","commit":"c0b52fd3","emulatedFps":49.97,"hostFps":49.89,"cyclesPerFrame":141961,"fieldsPerFrame":1.0006,"frames":1011,"cycles":143523020,"measurementMs":20265,"detail":"0x20700000","watchdog":null,"stable":true,"quantizedFields":1,"minFps":49.66,"maxFps":50.32}
```

Regresión de la demo base: `206` sigue en **49,93 fps** (142 070 ciclos/frame) tras
recompilar con el mismo engine.

## Secuencia y visión

Secuencia de 5 frames cada 100 ms tras `pre_scroll` (fogata visible al arrancar):

- `out/run/207_tiled_live_layers_animation/A500_release/sequence/frame_000.png`
- ...
- `out/run/207_tiled_live_layers_animation/A500_release/sequence/frame_004.png`

Prompt enviado a `qwen3-vl:8b-instruct-q8_0` (Ollama local):

```text
Las imágenes adjuntas van en orden temporal. Describe la fogata (campfire), el agua
y las flores. Indica si la llama cambia de forma entre imágenes, si el agua/flores
están animadas, y si ves rectángulos, huecos, bandas o artefactos. Responde breve y
concreto.
```

Respuesta:

```text
- Fogata: La llama cambia de forma entre imágenes (más alta en la 2ª, más baja en la 3ª).
- Agua: No está animada; es un plano estático.
- Flores: No están animadas; son elementos estáticos.
- Rectángulos/huecos/bandas/artefactos: No se observan.
```

**No concluyente**: esta secuencia tenía cámara en movimiento. Una prueba posterior con cámara fija mostró que `T` avanza y `A` cambia, pero el contenido de píxeles no cambia (solo el HUD). El parche de animación no se materializa en la ventana; la animación runtime queda pendiente.

`flicker-check` reporta candidatos deterministas debidos al movimiento de cámara y a
los frames de animación; la capa de visión no confirma anomalía:

```text
- Anomalía: no
- Tipo: ninguno
- Zona relativa: arriba
- Confianza: alta
```

## Límites conocidos

- **X+Y simultáneo**: baja a ~25 fps (2 campos/frame). El recorrido entregado es
  Y-only para mantener 50 fps.
- **Overlay en banda entrante**: la base lleva el primer frame; las celdas nuevas no
  se recomponen con overlays hasta el siguiente cambio de grupo. El
  `TileLayerStack` completo sigue pendiente.
- **Ghosting**: los píxeles transparentes de frames posteriores pueden revelar el
  primer frame horneado; no se observó corrupción, pero es deuda del path de parche.
- **Animación runtime**: no verificada; el tick avanza pero el parche no cambia\n  píxeles visibles. Es el bloqueo abierto de T5a.
- La franja inferior de 48 px es HUD negro (no se dibuja contenido).
