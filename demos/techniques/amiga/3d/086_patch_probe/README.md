# Demo 086 — sonda de parcheo de copperlist

Sonda de diagnóstico que reproduce el parcheo de datos de una copperlist por frame, como la ruta de display de la demo 080. Verifica que los `MoveSlot` guardados durante `Scene::compose` siguen apuntando a los opcodes correctos después de materializar el plan; `g_eng_run_status.detail` publica el número de slots comprobados, discrepancias y ciclos de parcheo.

El modo predeterminado actualiza los punteros de bitplanes y `BPLCON1` sin contenido gráfico: sirve para aislar la operación y su temporización. `PATCH_PROBE_MODE=8` combina el mismo parcheo con un rectángulo de sala tramado y paneles sólidos, para observar la ventana y el relleno en una secuencia.

```bash
DEMO_FLAGS="-DPATCH_PROBE_MODE=8" bash tools/build/build-demo.sh demos/techniques/amiga/3d/086_patch_probe --debug --clean
bash tools/run/run-demo.sh demos/techniques/amiga/3d/086_patch_probe --settle-ms 1000 --sequence-frames 8 --sequence-interval-ms 500
```

La intención del efecto y la evaluación canónica de visión están en [`src/INTENT.md`](src/INTENT.md) y [`src/VISION.md`](src/VISION.md).

## Estado de verificación

El gate de documentación está cubierto, pero la ejecución actual no está verificada: el runner recibe `FAILED` antes de READY y la captura observa una ventana AmigaDOS inactiva, no la escena del modo 8. `src/VISION.md` conserva esa observación literal; no se presenta como evidencia de que el parcheo de copperlist haya funcionado.
