# 213 — render roto en release (`-O2`): *strict aliasing*

**Estado:** resuelto. Causa: **aliasing estricto** (type-punning) en el engine a `-O2`. Fix:
`-fno-strict-aliasing` en `tools/build/build-demo.sh`.

## Síntoma

La demo 213 en `A500_release` (`-O2`) dibujaba mal la ristra de 16 BOBs (faltaban la mayoría o
aparecían desplazados), de forma no determinista entre builds. En `A500_debug` (`-O1`) era correcta.
Medida objetiva (píxeles coloreados en la banda, `out/tmp/bandcount.mjs`):

| build | píxeles coloreados |
|---|---|
| debug (`-O1`, correcto) | ~6600 |
| release (`-O2`, roto) | ~1200–3700 |
| release **con `-fno-strict-aliasing`** | **~6700 (correcto)** |

Relacionado: el init falló de forma intermitente con `detail=0x21305` (`OutOfMemory` fantasma) y la
sonda de memoria reportó `status=BankAbsent` con bytes libres: todo corrupción por el mismo aliasing.

## Causa y arreglo

El engine hace *type-punning* por diseño: accede a registros custom `volatile u16*` como `u32`
(`write_ptr`/`write_custom_pointer`), y reinterpreta buffers tipados (`Block`, bancos, `Address`,
vistas desde almacenamiento). Con aliasing estricto, GCC asume que punteros de tipos distintos no se
solapan y **reordena/elimina** accesos que el hardware sí comparte → el blit sale mal.

`-fno-strict-aliasing` (opción recomendada por GCC para código de bajo nivel que reinterpreta
memoria) lo corrige. **El «50 fps» que se veía en release era falso**: el miscompile se saltaba
BOBs (menos blits → más rápido). Con la imagen correcta, el release real es **38 fps**.

## Detalle del engine (además del flag)

Se escriben los punteros de Blitter (`BLTxPTH/PTL`) con **dos stores de 16 bits** en vez de un
`reinterpret_cast<volatile u32*>` sobre registros `volatile u16*` (mismo patrón que el original). El
flag cubre el resto de *punning* del engine.

## Rendimiento (tras el arreglo)

- Con **Blitter instantáneo** (`run-demo --immediate-blits`): **50 fps** (1 campo) → el CPU cabe en
  un campo. El Blitter real añade ~0,3 campo (~44 k ciclos, los mismos 17 blits que el original).
- El original da 50 fps porque su Blitter corre con el bus libre (lanza los blits en el blanking) y
  su CPU por frame es pequeña (tiene ciclos de sobra, `WaitLine` = espera activa). La nuestra gasta
  ~40–50 k ciclos más de CPU (construir el plan por BOB + ejecutarlo) — **pendiente de reducir**.
