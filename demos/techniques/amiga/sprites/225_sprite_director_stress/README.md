# 225 — SpriteDirector 50 Hz (CPU)

Demo de la fachada por intención `eng::scene::SpriteDirector` en su variante **CPU a 50 Hz PAL**. La escena completa de estrés no cabe en un campo; esta entrega conserva el camino CPU y reduce el resto de la carga, documentado en `VALIDATION.md`.

## Qué enseña

- `VisualStore`/`SceneAssets` con handles generacionales y `SpriteDesc` + `SpriteHint`.
- Materialización **CPU** (`SpritePolicy::Cpu`): copia planar 16-bit en el playfield, sin canal HW ni Blitter.
- HUD de host de una línea con nombre, frame y fps; margen seguro 24/8.
- La reducción de carga respecto a la escena de estrés completa y el motivo de cada recorte.

## Qué se ha reducido para llegar a 50 Hz

- Se conserva **una entidad CPU**; se retiran de la variante entregada la tira horizontal, el compuesto, el par *attached* y el BOB forzado.
- Se retira la `BandPalette` de la franja baja.
- La escena completa se midió a **26,46 fps** (268 129 ciclos/frame); no se publica como demo válida porque incumple el criterio de 50 fps.

La ruta BOB se valida en **126_fast_bobs** (49,92 fps) y la contabilidad de CPU en **HOST-491**.

## Ejecución

```bash
bash tools/build/build-demo.sh demos/techniques/amiga/sprites/225_sprite_director_stress --release
bash tools/run/run-demo.sh demos/techniques/amiga/sprites/225_sprite_director_stress --config A500_release
```

La evaluación visual está en `VALIDATION.md`; la contabilidad exacta, en HOST-491.
