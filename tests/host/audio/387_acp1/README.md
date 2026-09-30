# HOST-387: parser, secuenciador y encoder ACP1

Valida parser ACP1 v1/v2 sin heap, lectura y mezcla de eventos por ventanas, `Acp1Stream` con feeder/triple buffer, deduplicación exacta de unidades AUZX y el baseline HPSS; también cubre offsets, secuencias, silencios, ganancia, truncados y referencias inválidas.

```bash
bash tools/run-host-tests.sh tests/host/audio/387_acp1
```
