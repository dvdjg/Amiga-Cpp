# Validación 207 — capas vivas y cook-plan

## Estado

Parcial. La composición viva y el contrato están implementados; el presupuesto de 50 fps no se mantiene con toda la zona visible animada. El caso completo (`kBudgetedAnimations=false`) queda en ~22–25 fps de media.

## Medidas

| Build | fps | ciclos/frame | campos/frame | Notas |
|---|---:|---:|---:|---|
| `A500_release` live | 22,71 | 312 315 | 2,20 | media de 20 s; arranque 12→32 |
| `A500_release` baked previo | 49,97 | 141 961 | 1,00 | referencia T5 anterior |

La ventana live tiene dispersión alta porque el arranque incluye la composición inicial y la política de prioridad tarda en observar fps. No se declara 50 fps.

## Contrato

- `tools/tiled/validate-cook-plan.mjs` valida `render.baked_through`, modos, `animations` y occluders. Test: `node tools/tiled/test-cook-plan.mjs`.
- `HOST-499` valida `TileLayerStack` (modos, `live_count`, `is_live`, prioridad).
- Plan live: `tools/tiled/fixtures/beginning-fields.live.cook-plan.json`.
- Assets: `out/assets/tiled/beginning-fields-live/selected/` (base limpia, 30 grupos, 136 frames, 165 celdas).

## Secuencia

`A500_debug` con pre-scroll: 4 frames a 600 ms. Se observan agua, flores cocidas y fogata sin huecos evidentes; la animación del agua/fogata avanza. La revisión por modelo local (Ollama) queda pendiente; el entorno es rico y se exige secuencia, no una captura aislada.

## Deuda

- La prioridad congela el reloj de capa, no elimina overlays; falta medir el ahorro real de la banda con presupuesto.
- `Flowers` viva requiere overlays estáticos de capas superiores; no se fuerza.
- `Occluder` tras actores está en la API, sin demo 208.
