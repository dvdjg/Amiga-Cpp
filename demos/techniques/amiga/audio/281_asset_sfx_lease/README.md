# Demo 281: SFX desde AssetCache con lease hasta fin natural

Carga un SFX CPU-usable desde `DH1:` y lo reproduce en `AudioSystem`. La demo intenta cerrar la caché mientras la voz (16 KiB) sigue leyendo: el teardown debe rechazarse. Tras el fin natural de la muestra, el cierre debe tener éxito y devolver el bloque. No se añade sondeo del owner por blit ni en el mixer IRQ: `AudioSystem::tick_frame` comprueba voces una vez por VBlank.

## Preparar y ejecutar

```bash
node tools/fs/make-volume.mjs --no-adf
bash tools/build/build-demo.sh demos/techniques/amiga/audio/281_asset_sfx_lease --debug --clean
bash tools/run/run-demo.sh demos/techniques/amiga/audio/281_asset_sfx_lease --keep-running
```

`mark_ready` se emite solo si el teardown falla mientras el mixer lee, el canal termina naturalmente y luego el cierre libera el slot Chip. `detail` codifica los frames transcurridos desde el comienzo de la voz. La reproducción audible requiere emulador/Amiga; el gate estructural comprueba la secuencia de estados y el pinning.

## Contrato ilustrado

`AssetCache::lease` conserva una lectura CPU en cualquier banco. `AudioSystem::play_sfx_asset` mueve esa lease a la ranura de voz y la libera al quedar inactiva. `docs/engine/architecture/GAME_AUDIO.md` §3 y `docs/engine/architecture/MEMORY_OWNERSHIP.md` describen la separación entre lectura CPU y DMA de Paula.
