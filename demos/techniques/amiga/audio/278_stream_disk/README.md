# Demo 278: streaming de audio comprimido desde disco (AUZX)

Camino completo de streaming digital: lee un fichero **AUZX** de `DH1:` (melodía de dominio
público comprimida con **Fibonacci Delta**), lo reconoce con `eng::audio::media`, lo streamea con
**`PcmStream<3>` (triple buffer)** desde un chunk elegido con **`seek`** (mitad de la melodía,
saltando por el índice del AUZX) y Paula lo reproduce por DMA; la IRQ de audio solo cambia el
puntero. La CPU descomprime cada chunk directo a Chip (en ASM en m68k).

## Pipeline de PC

```bash
node tools/audio/gen-melody.mjs out/tmp/melody.raw                 # Oda a la Alegria (8 kHz)
host-tools/pack-pcm out/tmp/melody.raw out/tmp/melody.auzx fib 8000 1024
node tools/fs/make-volume.mjs --add out/tmp/melody.auzx:data/audio/melody.auzx
```

## Ejecutar

```bash
bash tools/build/build-demo.sh demos/techniques/amiga/audio/278_stream_disk --release --clean
bash tools/run/run-demo.sh demos/techniques/amiga/audio/278_stream_disk --warp
```

## Verificación

`detail = (irq << 16) | swaps`; con triple buffer y `seek` a la mitad, medido `0x260017` → **38
IRQs, 23 swaps, 0 underruns** (23 = los chunks de la segunda mitad). El log del emulador
(`audio-stream-irq-rate.md`) sirve de contraste.
