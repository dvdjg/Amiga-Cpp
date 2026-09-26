# HOST-334 — ergonomía de un solo elemento (`Device::blitter_or_bobs`)

Respalda el overload `eng::Device::blitter_or_bobs(const graphics::OrBob&, …)` (`eng/api/device.hpp`):
azúcar del que toma `Span<const OrBob>`, para no envolver a mano `Span<const OrBob>{&bob, 1u}`
en el llamador (como hacía la demo 204). Con un backend falso comprueba que un solo `OrBob` se
reenvía como un lote de **1** entrada.

Decisión de diseño (ver `ROADMAP_API_COHERENCE.md`): **no** se añade un constructor implícito de
un solo elemento a `Span` (tipo central; promocionaría objetos a spans accidentales y agravaría
el *dangling* con temporales). La ergonomía se pone **en el dominio** (overload o helper
`single`/`span_of`).

```bash
CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/core/334_device_single
```
