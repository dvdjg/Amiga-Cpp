# 274_octamed_probe — repro de A1 (OctaMED `_startmusic`)

Repro mínima del bloqueo **A1**: arranca el playroutine OctaMED incrustado con
`AudioSystem` en modo `AudioMode::TitleOctaMED` → `play_music(..., MusicFormat::OctaMED)` emite
`jsr _startmusic`.

## Build (opt-in del playroutine MED)

```bash
EXTRA_DEFINES="-DENG_AUDIO_OCTAMED" bash tools/build/build-demo.sh demos/amiga/274_octamed_probe --debug
WINUAE_SIDE_CHANNEL_PORT=2421 bash tools/run/run-demo.sh demos/amiga/274_octamed_probe --warp --wait-port 300
```

Sin el `EXTRA_DEFINES` la demo **marca READY** sin ejercitar nada (`detail = 0x00027400`): el
reproductor MED no se compila (opt-in) y así la demo no rompe la regresión.

## Qué comprueba y qué encontró

- **Sonda previa** (`$dff007`, byte bajo de VHPOSR): confirma que el contador H **sí cambia**, así que
  el bucle `_Wait1line` del playroutine (que espera ese cambio) no es el que gira sin fin.
- **Arranque**: marca READY justo después de `play_music` → **`_startmusic` retorna** (el bloqueo
  documentado decía que no alcanzaba READY; con la repro mínima actual **sí**).
- **Con la comprobación a frame 30** (leer `DMACONR` y exigir los 4 canales de audio): **cuelga** antes
  del frame 30 → el cuelgue está **después** del arranque, no dentro de `_startmusic`.

Detalle, hipótesis descartadas y siguiente paso:
[`docs/debugging/investigaciones/octamed-startmusic-hang.md`](../../../docs/debugging/investigaciones/octamed-startmusic-hang.md).
