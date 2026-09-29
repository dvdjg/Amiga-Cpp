# Demo 278: streaming de audio comprimido desde disco (AUZX)

Camino completo de streaming digital: lee un fichero **AUZX** de `DH1:` (melodía de dominio
público comprimida con **Fibonacci Delta**), lo reconoce con `eng::audio::media`, lo streamea con
**`PcmStream<3>` (triple buffer)** desde un chunk elegido con **`seek`** (mitad de la melodía,
saltando por el índice del AUZX) y Paula lo reproduce por DMA; la IRQ de audio solo cambia el
puntero. La CPU descomprime cada chunk directo a Chip (en ASM en m68k).

## Pipeline de PC

`make-volume.mjs` genera `data/audio/melody.auzx` (melodía de dominio público) en el volumen
compartido `out/fs/content`, que el runner monta en `DH1:`. Usa el packer Node `pack-auzx.mjs`
(port fiel de `host-tools/pack-pcm`: mismo layout AUZX y mismo encoder Fibonacci Delta), así que
no necesita compilar C++ ni Python:

```bash
node tools/fs/make-volume.mjs --no-adf   # deja melody.auzx en data/audio/ del volumen (DH1:)
```

Para regenerarlo a mano: `node tools/audio/gen-melody.mjs out/tmp/melody.raw` y
`node tools/audio/pack-auzx.mjs out/tmp/melody.raw out/tmp/melody.auzx 1024 8000`.

## Ejecutar

```bash
bash tools/build/build-demo.sh demos/techniques/amiga/audio/278_stream_disk --release --clean
bash tools/run/run-demo.sh demos/techniques/amiga/audio/278_stream_disk --warp
```

## Verificación

`detail = (irq << 16) | swaps`; con triple buffer y `seek` a la mitad, medido `0x260017` → **38
IRQs, 23 swaps, 0 underruns** (23 = los chunks de la segunda mitad). El log del emulador
(`audio-stream-irq-rate.md`) sirve de contraste.
