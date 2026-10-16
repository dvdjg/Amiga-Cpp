# 213 — render roto en release (`-O2`): *strict aliasing*

**Estado:** resuelto (aliasing). **Reabierto (2026-10):** el init de `A500_release` vuelve a fallar,
ahora de forma determinista; ver la nota al final.

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

## Reapertura (2026-10-16): init release falla en la carga de sprite/música

`A500_release` (config por defecto de la 213) **no alcanza READY** en la revisión actual: el runner
aborta con `La demo informo FAILED por canal lateral: detail=0x06CBF005`. El detalle decodifica como
`mark_mem_failed` del grupo de sprite/música (`base=0x05`), con causa `0` y la sonda de banco
`{libres ≈ 191 KB, usados ≈ 108 KB}`: el **banco Chip rechaza la reserva con hueco libre** —
fragmentación o presupuesto del bloque, no un fallo de geometría.

Evidencia de atribución:

- **Reproducible en el engine sin los cambios del turno** (`git stash` de `engine/`): mismo
  `detail=0x06CBF005` en `A500_release`.
- La config con flags de A/B (`-DK_STREAM_BOBS=0`) falla aún antes (copia de fondo,
  `detail=0x21304`), también con y sin los cambios del turno.
- En **debug** (`A500_debug`, `-O1`) el init completa y la demo renderiza (verificado con capturas
  completas y visión).

**Deuda abierta** (ajena al hilo de las rachas de Blitter): o el presupuesto de memoria del perfil
release de esta demo no cabe con la fragmentación por orden de reservas, o `configure_game_memory`
reserva menos Chip del que el manifiesto necesita. Próximo paso: leer `bank.snapshot()`/
`g_mem_probe` del caso release (el detalle ya empaqueta libres/usados) y comparar el orden de
reservas debug vs release.
