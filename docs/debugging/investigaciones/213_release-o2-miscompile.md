# 213 — render roto en release (`-O2`) y OOM intermitente

**Estado:** abierto. Bloquea declarar 50 fps con imagen correcta en `--release`.

## Síntoma

La demo 213 (`Bartman Abyss`) en `A500_release` (`-O2`) dibuja mal la ristra de 16 BOBs: faltan la
mayoría o aparecen desplazados, de forma **no determinista** entre builds. En `A500_debug` (`-O1`)
la imagen es correcta. Medida objetiva (píxeles coloreados en la banda de juego, `out/tmp/bandcount.mjs`):

| build | píxeles coloreados |
|---|---|
| debug (`-O1`, todas las unidades) | 6623 (correcto, 16 BOBs) |
| release (`-O2`, todas) | ~1750 (roto) |
| release con `DEMO_OPT=-O1`, engine `-O2` | ~3241 (a medias) |

Relacionado: el init falló de forma intermitente con `detail=0x21305` (add del sprite con
`OutOfMemory`), y la sonda de memoria reportó `status=BankAbsent` **con 51 KB libres** en el mismo
banco (`MemBank::Snapshot` inconsistente): corrupción de estado del `BlockPool`, propia de UB.

## Aislado

- **Preexistente**: `git stash` de todos los cambios de optimización (camino de frame a coste cero)
  reproduce el render roto → no lo introdujo la optimización.
- **Depende del nivel de optimización**: `-O1` correcto, `-O2` roto. Afecta tanto a la unidad de la
  **demo** como a la del **engine** (con la demo a `-O1` y el engine a `-O2` sigue a medias), lo que
  apunta a UB en código **inlined into both** (cabeceras del camino de BOB) o a un *miscompile* de
  gcc 15 m68k a `-O2`.

## Contexto del repo

`tools/build/build-demo.sh` documenta un bug de codegen de gcc 15 m68k a `-O1` (la 212 fija
`DEMO_OPT=-O2` en su `build.args` para evitarlo). Aquí el problema es a `-O2`.

## Líneas de ataque (siguiente sesión)

1. Bisecar por **función**: `build.args` con `ENGINE_OPT`/`DEMO_OPT`/`C_OPT` por unidad ya permite
   acotar; afinar por fichero/tramo con copias temporales a `-O1`.
2. Revisar UB candidata en el camino de BOB: `BlobBatch::write_ptr` (`reinterpret_cast<volatile u32*>`
   sobre registros `volatile u16*`), campos de `BlitJob` sin fijar en la construcción in situ
   (ya se fijaron los que lee el encoder), y solapes/alias de los arrays del plan.
3. Validar con `heap`/relleno de patrones: reservar con patrón distinto de cero para cazar lecturas
   de campos no inicializados (la ranura estable reutilizada es el sospechoso principal).
4. Comprobar si el `BlockPool` recibe un OOB (el `status=BankAbsent` con bytes libres delata el
   contador de slots o el tamaño de la base).

## Nota

Mientras esté abierto, la medida de fps de `--release` **no es fiable** para el objetivo de 50 fps
(el render no está completo). El `-O0`/`-O1` (debug) sí renderiza bien.
