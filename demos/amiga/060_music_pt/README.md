# Demo 060: music pt — música Protracker (.mod) por PTPlayer (Frank Wille)

Demuestra la capa de música con un módulo Protracker generado en memoria (sin
activo binario): un tono cuadrado en bucle sobre una nota C-2, reproducido por
`eng::audio::PtPlayer` (modo CIA, reproductor de Frank Wille).

## Verificación

`mark_ready` se emite solo cuando el DMA de audio de la música está activo
(tras ~100 frames, cuando la interrupción CIA-B ha disparado `_mt_dmaon`).
`run-report.json` debe mostrar `DMACONR` con `AUD0EN` (bit 0) activo (el módulo
solo usa el canal 0).

```bash
tools/build/build-demo.sh demos/amiga/060_music_pt --clean
tools/run/run-demo.sh       demos/amiga/060_music_pt
```

## Módulo Protracker mínimo

`build_mod()` construye en memoria un módulo `M.K.` con 1 muestra (onda
cuadrada en bucle) y 1 patrón de 64 filas (nota C-2, período 428, canal 0). Es
autocontenido: no depende de activos binarios.

Ver `docs/engine/architecture/MUSIC_PLAYER.md`.
