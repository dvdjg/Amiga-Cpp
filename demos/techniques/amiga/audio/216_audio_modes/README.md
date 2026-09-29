# Demo 216: modos de audio y eventos (A0/A2)

Ejercita en hardware la superficie de **A0** (modos de audio / reparto de canales) y **A2**
(eventos de audio) **sin módulo de tracker** (solo SFX del mixer):

- `eng::audio::AudioSystem::init(memory, cfg)` con `AudioMode::GameSfxOnly` (los 4 canales para el
  mixer) y un SFX en bucle.
- `set_mode(Silent)` a mitad de ejecución (A0).
- `tick_frame(port)` cada frame; `notify_underrun()` provoca **un** `MsgType::AudioUnderrun` (A2),
  que la demo recibe por el puerto.

## Evidencia

`mark_ready` se emite cuando el puerto recibió el `AudioUnderrun` tras `notify_underrun()`
(uno por evento); el `detail` guarda `DMACONR`.

Ver `docs/engine/architecture/GAME_AUDIO.md` §7-8 y
`docs/guides/roadmap/ROADMAP_AUDIO.md` (A0/A2); tests `HOST-269` (modos) y `HOST-270` (eventos).

## Uso

```
bash ./tools/build/build-demo.sh demos/techniques/amiga/audio/216_audio_modes --debug
bash ./tools/run/run-demo.sh demos/techniques/amiga/audio/216_audio_modes --warp
```
