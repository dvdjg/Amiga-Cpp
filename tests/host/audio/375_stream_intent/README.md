# HOST-375: intención de streaming

Valida la intención de reproducción de sample largo (`StreamIntent`) y su adaptador de backend. El juego solo proporciona un identificador de recurso y parámetros de reproducción; la elección Paula/mixer y los buffers quedan en el backend.

```bash
bash tools/run-host-tests.sh tests/host/audio/375_stream_intent
```
