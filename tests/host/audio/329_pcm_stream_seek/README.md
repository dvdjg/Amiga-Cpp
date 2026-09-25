# HOST-329: `PcmStream` con triple buffer y `seek`

Test host de `eng/audio/pcm_stream.hpp`: confirma que `PcmStream<3>` (triple buffer) funciona y
que el nuevo `seek(chunk)` **reposiciona** el stream en un chunk (aprovechando el índice del
AUZX) reiniciando el estado de buffers.

## Qué comprueba

1. **Triple buffer**: 3 buffers libres al arrancar y `provide` para los tres.
2. **`seek`**: tras consumir chunks, `seek(6)` deja `next_chunk() == 6`; el siguiente `provide`
   usa ese chunk y el flujo continúa por el 7.

## Salida de referencia

```
OK: PcmStream triple buffer + seek.
```

## Ejecutar

```bash
CXX="/c/.../mingw64/bin/g++.exe" bash tools/run-host-tests.sh tests/host/audio/329_pcm_stream_seek
```
