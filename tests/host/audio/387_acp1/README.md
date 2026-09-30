# HOST-387: parser y encoder ACP1 v1

Valida el parser sin heap de `eng/audio/acp1.hpp` y el serializador host de la aplicación: cabecera, offsets exactos, payloads AUZX, pistas y eventos sincronizados para dos stems, además de truncados y referencias inválidas.

```bash
bash tools/run-host-tests.sh tests/host/audio/387_acp1
```
