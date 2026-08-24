# Herramientas de verificación

Baterías y comprobaciones de regresión/determinismo sobre demos del engine.

## `verify-determinism.mjs` — smoke test de determinismo

Ejecuta la demo **dos veces** con `run-demo` (captura secuencia) y compara los
frames emparejados por número de frame del run-status, usando **cross-correlación
con desplazamiento** para tolerar el lag del display entre capturas.

```
node tools/verify/verify-determinism.mjs [demos/<demo>] [--threshold 8]
```

- MAE bajo tras el mejor shift → el contenido es el mismo (determinista).
- MAE alto aunque el shift sea pequeño → render NO determinista (investigar).

Hallazgo al usarlo con `101_ehb_tile_scroll_driver` (2026-08): los frames
tempranos son deterministas (MAE 0), pero los posteriores al primer upload de
tiles por Blitter difieren entre ejecuciones — posible no-determinismo del
prefetch por blitter (o artefacto del timing de captura). Es un diagnóstico a
seguir investigando.
