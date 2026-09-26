# Demo 279: banco de los descompresores de audio (ASM vs C++)

Mide **muestras/s** de las rutinas ASM (`support/codec_asm.s`) y de la referencia C++, con el
reloj TOD de la CIA-A (50 Hz), y comprueba que la salida coincide. Las tasas se dibujan en
pantalla; publica en `detail` un bit por códec si **difiere** (0 = idénticos).

```
bit 0 = Fibonacci difiere    bit 1 = IMA difiere    bit 2 = integracion delta difiere
```

## Ejecutar

```bash
bash tools/build/build-demo.sh demos/techniques/amiga/audio/279_codec_bench --release --clean
bash tools/run/run-demo.sh demos/techniques/amiga/audio/279_codec_bench --warp
```

## Verificación

READY con `detail = 0x00027900` (m_detail = 0): ASM == C++ en los tres. La pantalla muestra las
tasas `asm`/`cpp` de Fibonacci, IMA y la integración delta.
