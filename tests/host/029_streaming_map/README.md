# Test HOST-029: mundo disperso con streaming (prefetch + solo-residentes)

Respalda `eng::field::StreamingWorldMap` (`engine/include/eng/field/streaming_map.hpp`):
un mundo disperso por **chunks** cuya ventana visible se **precarga** antes de dibujar,
de modo que el acceso durante el dibujo (`tile_at`) solo consulta chunks residentes y
nunca dispara una carga.

Se comprueban: lectura no residente como `empty_tile`, `prefetch` que cubre la ventana
(carga los chunks que tocan), chunk ausente que queda residente como vacío y no se
recarga, `prefetch` repetido sin recargar, y evicciones LRU al superar la capacidad del
pool del llamador.

```bash
bash tools/run-host-tests.sh tests/host/029_streaming_map
```

Contexto: `docs/engine/architecture/CONTENT_AND_TILEMAP.md` §2.
