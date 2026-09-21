# HOST-259: decodificación MFM del disquete

Test host del decodificador MFM de `eng/os/floppy.hpp` (la parte **pura** del módulo de disquete a
bajo nivel; el backend con DMA + CIA-B se prueba en la demo `214_floppy_raw`).

## Qué comprueba

1. `mfm_decode_long` invierte el encoder (dodd/deven) para varios valores.
2. Construye una pista AmigaDOS sintética con el **mismo layout que el encoder del emulador**
   (`WinUAE-DBG/disk.cpp:2185-2260`: sync `$4489`×2, cabecera, etiqueta, checksums, datos con dodd
   y luego deven) y `floppy_find_sector` recupera los **11 sectores**: cabecera (format `0xFF`,
   track, sector, to_gap) y los **512 bytes** de datos intactos.
3. Negativos: sector inexistente, `dst` pequeño y pista sin sync → `false`.

> El test usa una pista **alineada a palabra**. La pista real tiene huecos que no son múltiplo de
> 16 bits, así que el decode necesita además una búsqueda de sync **bit a bit** (pendiente; ver
> `demos/amiga/214_floppy_raw/README.md`).

## Salida de referencia

```
OK: decodificacion MFM de pista AmigaDOS validada.
```

## Ejecutar

```bash
bash tools/run-host-tests.sh tests/host/259_floppy_mfm
```
