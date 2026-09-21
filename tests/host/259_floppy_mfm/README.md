# HOST-259: decodificación MFM del disquete

Test host del decodificador MFM de `eng/os/floppy.hpp` (la parte **pura** del módulo de disquete a
bajo nivel; el backend con DMA + CIA-B se prueba en la demo `214_floppy_raw`).

## Qué comprueba

1. `mfm_decode_long` invierte el encoder (dodd/deven) para varios valores.
2. `mfm_decode_long` **ignora los bits de reloj** de las posiciones impares (máscara `0x5555`).
3. Construye una pista AmigaDOS sintética con el **mismo encoder que el emulador**
   (`WinUAE-DBG/disk.cpp:2168-2277`): sync `$4489`×2, cabecera, etiqueta, **checksums hck/dck**
   (XOR de los longs crudos), datos con dodd y luego deven, y `mfmcode` que OR-ea los relojes.
   `floppy_find_sector` (con `verify_checksums = true`) recupera los **11 sectores**: cabecera
   (format `0xFF`, track, sector, to_gap), checksums válidos y los **512 bytes** intactos.
4. **Desplazamiento de bit**: con todo el flujo desplazado 3 bits (sync no alineado a palabra) el
   decode sigue encontrando el sector (búsqueda bit a bit).
5. **Un solo sync**: si el sector aparece con un único `$4489` (el otro lo consume la DMA), el
   decode prueba la cabecera a 1 palabra y recupera igualmente cabecera y datos.
6. Un sector con un dato corrupto se rechaza con `verify_checksums = true` (y se acepta sin
   verificar), y los demás siguen validando.
7. Negativos: sector inexistente, `dst` pequeño y pista sin sync → `false`.

## Salida de referencia

```
OK: decodificacion MFM de pista AmigaDOS validada.
```

## Ejecutar

```bash
bash tools/run-host-tests.sh tests/host/259_floppy_mfm
```
