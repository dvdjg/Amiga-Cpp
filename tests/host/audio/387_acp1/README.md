# HOST-387: parser y encoder ACP1 v1

Valida el parser sin heap de `eng/audio/acp1.hpp`, la lectura/mezcla de tracks desde `media`, deduplicación exacta de payloads AUZX y el baseline HPSS armónico/percusivo; también comprueba offsets, sincronía, truncados y referencias inválidas.

```bash
bash tools/run-host-tests.sh tests/host/audio/387_acp1
```
