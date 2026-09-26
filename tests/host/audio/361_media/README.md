# HOST-361: interfaz de medios (`eng::audio::media`)

Test host del punto único de despacho por **contenedor** y **códec**: reconoce PCM crudo o AUZX
y decodifica por **chunks**.

## Qué comprueba

1. **AUZX**: `open()` rellena `Info` (contenedor, códec, rate, chunks, muestras) y
   `decode_chunk(i)` devuelve el PCM de cada chunk (construido con Delta+RLE del engine).
2. **PCM crudo**: sin cabecera → contenedor `Pcm`, códec `None`, `decode_chunk(0)` = copia.
3. **Rechazos**: blob vacío y AUZX corrupto.

## Salida de referencia

```
OK: interfaz de medios (dispatch AUZX/PCM y decode por chunk).
```

## Ejecutar

```bash
CXX="/c/.../mingw64/bin/g++.exe" bash tools/run-host-tests.sh tests/host/audio/361_media
```
