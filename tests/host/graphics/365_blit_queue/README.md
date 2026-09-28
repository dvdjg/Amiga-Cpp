# HOST-365 — blit_queue

Test host de `eng/graphics/blit_queue.hpp`: el **API de Blitter por intención** (cola FIFO con
feeder por poll).

- `BlitOp` lleva `src`/`dst` como **`ChipView<PlaneTag>`** (memoria con el banco Chip en el
  **tipo**): el Blitter es DMA y solo ve Chip RAM. Un `ChipView` **no** se construye desde un `u8*`
  ni desde una vista agnóstica (inherente, §232) → pasar pila/Fast al Blitter no compila.
- `BlitQueue<N, Executor>`: el desarrollador **encola** (`fill`/`stamp`/`all`) y **no espera**;
  `pump()` avanza mientras el Blitter está libre; `flush()`/`wait()` son los puntos de dependencia.

Valida con un **doble de blitter** (`FakeBlitter`): asincronía (encolar no ejecuta), **FIFO**
(`Fill` antes que `Stamp`), drenado por `pump`, `wait()` y **array de golpe** (`all`).

Diseño: `docs/engine/architecture/BLITTER_INTENT_QUEUE.md`.
