# 305 - hud_overlay

Demo del **H4 de `ROADMAP_HUD.md`**: **overlay sobre un juego en movimiento** con las dos políticas de restauración (`GameRedraws` en el panel A; **`Backing`** con buffer Chip en el panel B), pintado con las **operaciones del modelo** (`apply_hud_ops` sobre superficies de la escena), **skins** de medidor/contador (H6) y **degradación por presupuesto** medida sobre la sección HUD.

Ver `src/INTENT.md` (qué hace y su evolución temporal) y `VALIDATION.md` (medidas, evidencia de memoria y cautelas de captura).

## Compilar y ejecutar

```
bash ./tools/build/build-demo.sh demos/features/ui/amiga/305_hud_overlay --debug
bash ./tools/run/run-demo.sh demos/features/ui/amiga/305_hud_overlay --keep-running
```

## Verificación

```
bash demos/features/ui/amiga/305_hud_overlay/analyze-sequence.sh --warp
node tools/analyze/verify-hud-overlay.mjs --image <screenshot.png>
```

El gate combina **memoria** (`fps10`, `ov_bad`/`bk_bad`/`blit_bad` = 0, `deg_changes >= 2`) y **visual** (paneles pintados + movimiento del campo; el verificador detecta el vídeo congelado del entorno —DT-023/035— y lo declara no evaluable).

## API que ilustra

- `eng::hud`: `HudRegionSpec` con `HudOverlayRestore::GameRedraws`/`Backing` (+ `budget.max_backing_bytes`), `Meter`/`Counter` con `set_meter_style`/`set_counter_style` (skins), `HudDrawOp` + `apply_hud_ops` sobre cualquier `Surface`, y los contadores de telemetría.
- `eng::Screen`: `bitmap` (ventana con shift fino y copias desde buffers Chip) y `fill` para la franja estática.
