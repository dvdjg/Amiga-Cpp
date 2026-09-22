# Demo 217: ejemplo de juego completo (A6)

Ejemplo mínimo del ciclo de vida del audio con la **superficie estable** (`GameAudio` sobre el
`AudioSystem` del backend) y los **modos** (`AudioMode`):

| Fase | Frames | Audio |
|---|---|---|
| BOOT | 0-19 | silencio |
| TITLE | 20-59 | música Protracker en bucle, modo `Game` (AUD1..AUD3) |
| GAMEPLAY | 60-99 | música + SFX por input (beep con cooldown; alarma con ducking en AUD0) |
| PAUSE | 100+ | `AudioMode::Silent` (corte ordenado) |

Es el patrón de uso: `attach` → `play_music` → `init` (mixer) → `set_music_channel_mask` → banco
de SFX, y por frame `update()` (poda/ducking) + `update_music()`. Los cambios de fase usan
`set_mode` y `stop_music` (sin tocar registros).

## Evidencia

`mark_ready` al cerrar el ciclo; el `detail` guarda `DMACONR`.

Ver `docs/engine/architecture/GAME_AUDIO.md` §2/§7 y `docs/guides/roadmap/ROADMAP_AUDIO.md` (A6).

## Uso

```
bash ./tools/build/build-demo.sh demos/amiga/217_audio_game_example --debug
bash ./tools/run/run-demo.sh demos/amiga/217_audio_game_example --warp
```
