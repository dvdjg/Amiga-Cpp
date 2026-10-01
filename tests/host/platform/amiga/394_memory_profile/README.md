# HOST-394 — perfiles de memoria de aplicación

Valida `eng/platform/amiga/memory_profile.hpp`: pools recomendados A500/A1200, override de producto con Fast RAM opcional y decisión pura de si los mayores bloques contiguos reportados pueden satisfacer una política. La disponibilidad es un preflight; en Amiga, Exec vuelve a decidir al ejecutar `AllocMem`.

```bash
bash tools/run-host-tests.sh tests/host/platform/amiga/394_memory_profile
```
