# HOST-103 — utilidades con `MiniFloat16` y `q12`

Respalda que los contenedores y algoritmos de `eng::util` funcionan con los escalares de
16 bits del engine, sin especializaciones (son agnósticos del tipo).

## Qué cubre

- `StaticVector<MiniFloat16>`, `RingBuffer<q12>`, `FlatMap<u16, MiniFloat16>`,
  `PriorityQueue<MiniFloat16>` (max-heap), `Pool<q12>` (handles), `Optional<MiniFloat16>`.
- `algorithm::min_element`/`find`/`util::clamp` sobre `q12`.
- Los algoritmos `stats`/`dsp` escalar-genéricos ya tienen cobertura propia
  (HOST-093 y HOST-102, este último con `q12`).

## Por qué no hacen falta especializaciones

Los contenedores almacenan `T` (trivialmente copiable) y los algoritmos usan solo
`operator<`/`==`/`+`, que `MiniFloat16` y `Fixed` ofrecen; por eso el mismo código vale
para cualquier escalar. Las limitaciones (precisión de MF, saturación de `div_norm` en
fixed, ausencia de `sqrt`/`sin` en `Fixed`) se documentan en cada cabecera escalar.

## Ejecución

```bash
CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/103_scalar_util
```
