# HOST-377: backend de stream por media

Valida que una `StreamIntent` se resuelve mediante `media::Info` antes de reservar buffers. El adaptador no conoce Paula ni mixer y rechaza chunks incompatibles.

```bash
bash tools/run-host-tests.sh tests/host/audio/377_media_stream_backend
```
