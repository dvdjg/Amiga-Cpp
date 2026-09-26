# HOST-362: descompresor aPLib

Test host de `engine/include/eng/audio/aplib.hpp`: port freestanding del descompresor aPLib de
`apultra` (Emmanuel Marty, zlib), sin diccionario ni flags.

## Qué comprueba

1. **Vector de referencia**: 96 bytes de patrón periódico comprimidos a **14** por `apultra`
   (`emmanuel-marty/apultra`) se descomprimen byte a byte al original.
2. **Dispatch** `Codec::APLib` de `pcm_codec`.
3. **Rechazos**: destino pequeño y flujo vacío.

## Salida de referencia

```
OK: aPLib (vector del compresor apultra de referencia).
```

## Ejecutar

```bash
CXX="/c/.../mingw64/bin/g++.exe" bash tools/run-host-tests.sh tests/host/audio/362_aplib
```
