# 220 — Bending por línea (Copper dobla sprites altos con una tabla de seno)

Demo de la técnica **F4.2** (`docs/guides/roadmap/ROADMAP_SPRITES_COMPLETO.md` §3.3): dos columnas de 16×64 (un canal cada una, 2 y 3) se dibujan con la X reescrita **en cada línea** por el Copper (`drivers::emit_sprite_bend_into`). Al escribir `SPRxCTL` con `VSTART = línea`, el registro **rearma el DMA** del canal y la fila sale con la X de la tabla de seno (`../WinUAE-DBG/custom.cpp:4078-4090`); el walk aporta una fila de DATA por línea (`custom.cpp:12012-12023`). Las dos columnas ondulan en oposición (fases 0 y 128).

- **Emisión intercalada por línea**: un solo `WAIT` por línea compartido por los dos canales (emitir cada canal en bloque desordena los `WAIT` — lo cazó HOST-436).
- **Anti-*Phantom Sprite***: la DATA de cada columna lleva terminador `0,0` (`winuae/sprite-dma.md`); tras la última fila el walk queda en el terminador.
- **Armado**: `arm_object` (patrón validado 214) en la línea 32; el bending sobreescribe `POS/CTL` de 40 a 103 con `VSTOP = 104`.
- **Pacing**: es la misma carrera contra el haz que Jim Power (demo 215); la altura se queda en 64 líneas y se mide (telemetría de copperlist incluida).

## Medidas (A500_debug)

- `measure-fps.mjs`: **49.97 fps**, 1.00059 campos/frame (sin tirones).
- Telemetría (`g_eng_frame_telemetry.copper_words`): **764 words** de copperlist (bending de 2×64 líneas = 640).

## Validación

Visión local (Ollama `qwen3-vl:8b`), secuencia de 3 capturas: «dos columnas… **curvadas/onduladas**… la onda se mueve… **sin cortes, saltos, basura ni líneas sueltas**». Detalle en [`VALIDATION.md`](VALIDATION.md).

## Ejecución

```bash
bash tools/build/build-demo.sh demos/techniques/amiga/sprites/220_sprite_bend --debug
bash tools/run/run-demo.sh   demos/techniques/amiga/sprites/220_sprite_bend --keep-running
node tools/debug/measure-fps.mjs 220_sprite_bend A500_debug --json
```

## Referencias

- `engine/include/eng/graphics/drivers/sprite_bend.hpp`; HOST-436 (`tests/host/graphics/436_sprite_bend`).
- `docs/reference/emulators/winuae/sprite-dma.md`; AHRM 3.ª cap. 4.
- Demo 215 (Jim Power) para el pacing de la misma familia.
