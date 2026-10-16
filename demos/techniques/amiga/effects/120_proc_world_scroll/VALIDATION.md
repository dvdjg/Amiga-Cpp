# VALIDATION — Demo 120 (`120_proc_world_scroll`)

Informe de validación (F4/F5 de `docs/guides/methodology/PROCEDIMIENTO_DEMOS_Y_JUEGOS.md`).

**Estado: PARCIAL — render/scroll/intregración VERIFICADOS; presupuesto de 50 fps NO
VERIFICADO (deuda del camino de composición del engine, compartida con la 201).**
La demo recorre el mundo de biomas 40×28 con el corkscrew X-Limited: la captura on-target
muestra el mapa scrolleando (cámara desplazada respecto al origen), el HUD con nombre +
versión + contador de frames, y la escena instala sin fallos. Lo que falta es el contrato
de cadencia: 35,2 fps release / 34,1 debug (~1,42 campos/frame) por el coste del camino de
plan/composición del scroll en el engine actual.

Intención esperada: pantalla 320×256 (208 de campo + 48 de HUD) con el mundo cenital
desplazándose horizontalmente 1 px/frame de forma continua (sin costuras ni medio-frame),
banco de ~160 tiles de 16×16 en 5 planos (32 colores), y la franja con la identidad.

## Técnica vigente

- Mundo 40×28 generado **en compilación** (`constexpr`; mismas semillas que la 119/offline:
  huellas FNV cotejadas en `HOST-445`).
- Banco por `indexed_tiles` (builder genérico en planos — se levantó la restricción
  histórica `planes==6`, que era del pipeline EHB; ver `xlimited_scene.hpp`).
- Scroll por `XlimitedScene`+`TileLayerMap` toroidal (semántica probada en `HOST-447`) con
  sub-pasos atómicos de 1 px; HUD de la misma profundidad que el campo (zona BPLxPT-only).
- Bucle por **sondeo** (`run_frames_polling`, modo estable del doc 212).

## Mediciones

| Medida | Comando | Resultado | Lectura |
|---|---|---|---|
| fps (release, sondeo) | `node tools/debug/measure-fps.mjs 120_proc_world_scroll A500_release --json` | `35.23 fps`, `1.419 campos/frame`, `201356 ciclos/frame` | **NO VERIFICADO** (objetivo 50): deuda del camino de composición del engine |
| fps (debug, sondeo) | idem `A500_debug` | `34.08 fps`, `1.467 campos/frame`, `208157 ciclos/frame` | -O0 apenas añade coste ⇒ el exceso es trabajo real del planner/compositor |
| Referencia (201, debug) | `measure-fps 201_ehb_map A500_debug` | `22.59 fps`, `2.213 campos/frame` | la ralentización es **del engine** (la 201 es más pesada por 6 planos); nuestra integración rinde mejor que la referencia |
| Captura | `run-demo --settle-ms 4000` | mapa desplazado + HUD `PROC WORLD SCROLL v1.0.3444  F …` | scroll real (no frame 0) y franja legible |

## Prompts y respuestas (crudos)

Pendiente de la pasada de visión del vídeo/secuencia (el estado del render con la misma
paleta y el mismo mundo ya está validado en la 119 y en los dumps host `v6`).

## Conclusiones

1. **Integración verificada**: el mundo procedural se publica con el driver de scroll real
   (banco indexado + corkscrew + `TileLayerMap` toroidal), con HUD y sin fallos de
   instalación; el builder indexado pasó a ser genérico en planos (usado aquí a 5).
2. **Rendimiento**: ~200 k ciclos/frame por el camino de plan/composición del engine
   (la misma regresión afecta a la 201: 314 k). **NO VERIFICADO el 50 fps**; la optimización
   de ese camino es trabajo del engine (no de la integración), con esta medida como
   referencia.
3. **SIN VERIFICAR**: secuencia de visión del scroll en movimiento; NTSC; release en
   máquina distinta de A500.
