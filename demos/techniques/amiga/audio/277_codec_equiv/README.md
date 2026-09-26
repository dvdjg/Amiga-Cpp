# Demo 277: equivalencia de los descompresores ASM 68000

Gate de equivalencia de las rutinas ASM de `support/codec_asm.s` frente a la **referencia
C++** del engine, ejecutado **en el Amiga** (patrón del c2p de la demo 061). En `init` se
generan flujos de prueba con los codificadores C++ y se decodifican con las dos vías,
comparando **byte a byte**.

Publica en `g_eng_run_status.detail`:

```
bit 0 = Fibonacci Delta difiere (ASM vs C++)
bit 1 = IMA ADPCM difiere
bit 2 = integracion delta difiere
bit 3 = ZX0 difiere
bit 4 = Delta+ZX0 difiere
bit 5 = fallo al codificar los flujos de prueba
```

`detail == 0` (READY = `0x00040000`) = los descompresores ASM son idénticos a la referencia.

## Rutinas verificadas

`eng_fib_delta_decode`, `eng_ima_adpcm_decode`, `eng_delta_integrate` (integración delta de
Delta+ZX0 / Delta+RLE) y `zx0_decompress` (depacker ZX0 de Emmanuel Marty, `support/dzx0_68000.s`,
licencia zlib; ABI `a0`/`a1`).

## Ejecutar

```bash
bash tools/build/build-demo.sh demos/techniques/amiga/audio/277_codec_equiv --release --clean
bash tools/run/run-demo.sh demos/techniques/amiga/audio/277_codec_equiv --warp
```

## Resultado

`state=3`, `detail=0x00040000` (m_detail = 0): **equivalencia total**.
