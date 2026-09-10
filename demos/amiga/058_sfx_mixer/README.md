# Demo 058: sfx mixer — efectos de sonido por el Audio Mixer 3.7

Demuestra el soporte nativo del engine para el **Audio Mixer 3.7** (Photon):
`eng::audio::SfxMixer` envuelve el mixer ASM de `support/audio_mixer/`
(ensamblado con VASM a ELF en `build-demo.sh`).

Qué suena: una "alarma" en bucle por la voz 0. FIRE (o tecla 1..9) dispara un
"beep" corto de prioridad alta. Arriba/abajo cambia el volumen maestro (barra
cian). Las muestras se generan ya preprocesadas (amplitud ±24, múltiplos de 4).

## Verificación

- `mark_ready` guarda en `detail` el `DMACONR` (bits altos) y el nº de canales
  del mixer (bits bajos). Con `run-report.json`: `AUD0EN` (bit 0) y `DMAEN`
  (bit 9) activos, y `totalChannels = 4`.
- El `MixerGetTotalChannelCount()` = 4 confirma que `MixerSetup()` arrancó.

```bash
tools/build/build-demo.sh demos/amiga/058_sfx_mixer --clean
tools/run/run-demo.sh       demos/amiga/058_sfx_mixer
```

Nota: el sonido real no se puede comprobar de forma autónoma (requiere oído);
la evidencia es el arranque correcto del mixer, el DMA de audio activo y las 4
voces disponibles.

Ver `docs/engine/architecture/AUDIO_MIXER.md` para los requisitos y la API.
