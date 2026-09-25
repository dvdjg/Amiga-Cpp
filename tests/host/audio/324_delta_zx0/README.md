# HOST-324: Delta + ZX0 (sin pérdida)

Test host del esquema **sin pérdida** para audio: diferenciar el PCM
(`D_n = S_n - S_{n-1}`) y comprimir las diferencias con ZX0; al descomprimir, integrar.

## Qué comprueba

1. **Capa delta**: `integrate_deltas(differentiate(x)) == x`.
2. **End-to-end** con un **flujo ZX0 real** del compresor de referencia (vector de HOST-271):
   `pcm_codec::decode(..., Codec::DeltaZx0)` coincide byte a byte con
   `integrate(zx0::decompress)`.
3. El `encode` de DeltaZx0 devuelve `-1` (el flujo ZX0 lo produce la herramienta host, no el
   engine, que es la parte de decodificación).

## Salida de referencia

```
OK: Delta+ZX0 (capa delta e integracion sobre flujo ZX0 real).
```

## Ejecutar

```bash
CXX="/c/.../mingw64/bin/g++.exe" bash tools/run-host-tests.sh tests/host/audio/324_delta_zx0
```
