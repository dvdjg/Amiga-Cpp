# HOST-372 · audio_feeder (feeder IRQ-apto de audio)

Test host de `eng/audio/audio_feeder.hpp`: el **feeder** que se llama desde la IRQ
de Paula (nivel 4), avanza el stream (repone buffer) y lleva la cuenta
`irq`/`swaps`/`underrun`.

## Qué valida

- Alimentado a tiempo: `irq == swaps` y `underrun == 0` (`healthy()`), `detail = (irq<<16)|swaps`.
- Si la IRQ pide buffer y no hay (ni fin de stream): cuenta *underrun* y deja de ser `healthy`.
- Al final del stream, `advance() == false` es **fin normal**, no underrun.

## Por qué

Recoge la lección de [`audio-stream-irq-rate.md`](../../../docs/debugging/investigaciones/audio-stream-irq-rate.md):
los *underruns* de la demo 272 no eran de la IRQ sino del **feeder CPU-bound**; con el buffer ya
codificado, `irq == swaps` y 0 underruns. El feeder es **IRQ-apto** (estado trivial, sin heap ni
locks); postear un `Msg` de completación es cosa del **drenaje** (bucle), no de la ISR.
