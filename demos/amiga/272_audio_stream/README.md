# 272_audio_stream — streaming PCM desde RAM (A5, WIP)

Ejercita en hardware la maquinaria de streaming de A5 **sin depender del disco** (que cuelga en
`td_open`): la fuente es una melodía sintetizada en el propio 68000, troceada en chunks y comprimida
con el codec real (Delta+RLE). Dos buffers PCM en Chip; el bucle principal sintetiza + codifica y
rellena el buffer libre (`PcmStream::provide`), y la **IRQ de audio (nivel 4, voz 3)** llama a
`advance()` y reprograma `AUDxLCH/LCL/LEN` con el nuevo buffer (`PaulaAudio::set_buffer`).

## Estado: **no verificado (WIP)**

La infraestructura funciona (el servicio de nivel 4 se instala, la IRQ dispara y el ISR reprograma la
voz), pero la IRQ dispara ~34× más rápido que la duración del buffer → *underruns* y comportamiento
errático. Detalle, lo descartado y los bugs corregidos de paso:
[`docs/debugging/investigaciones/audio-stream-irq-rate.md`](../../../docs/debugging/investigaciones/audio-stream-irq-rate.md).

El informe objetivo (frame 240) es `irq > 0 && swap > 0 && underrun == 0`; hoy no se cumple.

## Ejecutar

```bash
bash tools/build/build-demo.sh demos/amiga/272_audio_stream --debug
WINUAE_SIDE_CHANNEL_PORT=2421 bash tools/run/run-demo.sh demos/amiga/272_audio_stream --warp --wait-port 300
```
