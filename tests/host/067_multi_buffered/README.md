# HOST-067: `MultiBuffered<Driver, N>`

Test host de `engine/include/eng/graphics/drivers/multi_buffered.hpp`: la abstracción que
generaliza el patrón «N buffers de display + swap de copperlist» que las demos 061/080
repetían a mano.

## Qué comprueba

1. `init` reserva N parejas (planos + copperlist) y enlaza cada driver (`bind`); los
   slots **no** comparten memoria.
2. Con `N>1` arranca escribiendo en el slot 1, para no dibujar el primer frame sobre lo
   visible (el slot 0 es el que muestra `takeover`).
3. `commit` instala la copperlist del buffer en escritura y rota (`0→1→0` con `N=2`).
4. `takeover` muestra el slot 0.
5. `N=1` equivale a un driver suelto: `back` siempre 0, sin flip efectivo.
6. **Integración real**: `MultiBuffered<HamScene, 2>` produce dos copperlists **distintas**
   (cada una con sus `BPLxPT`), lo que demuestra la separación «emisión de copperlist» /
   «bloque de planos» que introduce `HamScene::bind()`.

Todo con un backend de pega que solo registra qué copperlist se toma/instala: sin
hardware ni RAM Amiga.

## Ejecutar

```bash
CXX="/c/.../mingw64/bin/g++.exe" bash tools/run-host-tests.sh tests/host/067_multi_buffered
```
