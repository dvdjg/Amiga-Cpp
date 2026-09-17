# Lecciones: cómo se hace una demo y cómo se depura

Notas de método del trabajo sobre la 086 (`demos/amiga/086_bob_objects`) y la 054. No es un
documento de arquitectura: es lo que hay que hacer y lo que no, con el porqué y las cifras reales.

## 1. Cómo se hace una demo

**Objetivo visual primero, y comprobable.** Una demo se define por lo que se ve: un efecto
identificable, con movimiento, que se pueda describir en una frase. Antes de escribir código hay que
poder decir qué debe verse y cómo se verifica (secuencia de frames + veredicto de visión, no una foto).

**Cada iteración se mide, no se supone.** El presupuesto de un frame PAL es **141.876 ciclos** (un
campo a 50 Hz). En cuanto la demo tenga objetos, blits o copper, medir en cada paso:
`node tools/debug/measure-fps.mjs <demo>` (total) y `node tools/debug/profile.mjs <demo>` (por
secciones). Una demo "que se ve" pero a 12 fps no es una demo: es un prototipo.

**Llevarla al límite y documentar el límite.** Meter carga (más objetos, más efectos) hasta que el
frame deje de caber, y anotar dónde está el techo y por qué. Ese dato (N objetos a 50 fps) es parte
del resultado, no un detalle.

**Cada objeto declara lo que necesita; no escribe registros.** Un efecto, un actor o un BOB aporta
*intenciones* (blits al `FramePlan`, `CopperIntent` al `Plan`); el compositor decide y materializa.

**Un registro (color, canal) por objeto.** Si dos objetos comparten el mismo `COLORxx` y coinciden en
una línea de raster, el compositor resuelve el conflicto por prioridad y **se ve el color del ganador
en los dos**. Si un objeto pide un degradado en su registro, ese registro tiene que ser suyo: hay que
dar a cada objeto un índice de color distinto (su propio dato de bitmap), no solo pedirlo.

**Nada de medias tintas.** Una demo se termina o se marca explícitamente **NO FINALIZADA** con el
motivo (fps, gate visual pendiente, assets). Dejarla "casi" y pasar a otra cosa es deuda que se paga
después con intereses.

## 2. Cómo se depura

**Medir antes de suponer.** El orden es: (1) total con `measure-fps`; (2) reparto por secciones con
`profile.mjs` (motor: `ENG_PROF_BEGIN/END`); (3) aislar la sección culpable con una matriz de
variantes; (4) leer el código de ESA ruta. Sin el paso 2 se acaba discutiendo hipótesis; con él, el
culpable aparece en una medida.

**Una variable por medida.** Cambiar una cosa (número de objetos, número de intenciones, una política
de borrado) y medir. El orden que funcionó: 1 BOB → 1 campo; 8 BOBs → 2; +256 intenciones → 9. De ahí
se deduce que el coste por BOB y el coste por intención son **dos** problemas distintos.

**Sondas de hardware cuando el código no explica nada.** `decode-copper.mjs` (lista real desde
`COP1LC`), `probe-sprite-emission.mjs` (lista + registros + DATA), `read-sprite-regs.mjs`. Reglas de
oro aprendidas a golpes:
- **No comparar lecturas de custom registers entre ejecuciones**: la base de la arena cambia por run.
  Todo lo que se vaya a cruzar, en **una sola ejecución**.
- `SPRxPT`/`SPRxPOS`/`SPRxCTL` son *write-only*: lo que se lee no es lo que está programado.
- El frame se **cuantiza a campos** (el polling acaba en la línea 311): cualquier trabajo que pase de
  un campo cuesta **dos**, y un poco más puede costar tres. Un "2,0 campos" exacto suele ser "trabajo
  algo mayor que un campo", no "el doble de trabajo".

**Cifras que conviene tener en la cabeza** (evitan buscar donde no está):
- Un cambio de color por línea de copper = **1 WAIT + 1 MOVE ≈ 14 ciclos CPU** (~3 % de la línea):
  el Copper **no** es una excusa de rendimiento, cabe en el HBlank.
- Un blit pequeño (16-32 px, 1-3 planos) con sus registros ≈ **cientos de ciclos**, no miles.
- Un objeto de bitmap con borrado por caja debería costar **unos miles de ciclos**, no decenas de
  miles.

**Trampas concretas encontradas (y su síntoma)**
- `emit_palette` recorta `count` a `colors.size() - first`: una intención con `first = 1` necesita una
  vista de **`first + count`** entradas. Con una vista de 1, el scheduler emite **cero** MOVEs y el
  objeto "pide" en vano (síntoma: el efecto de copper no se ve).
- La caja de borrado y el save-under deben cubrir **las mismas palabras que el dibujo**
  (`base + (shift != 0)`), porque el barrel shifter escribe una palabra extra; si no, queda un
  **rastro de hasta 15 px por fila** en el borde derecho.
- Los objetos deben colocarse **dentro de la ventana de display** (`DIWSTRT`): lo que cae en el borde
  no se ve (síntoma: "solo se dibujan 2 objetos" cuando en realidad se emiten los 8).
- La DATA de un sprite necesita **dos palabras a cero** de terminación (AHRM); el DMA de sprites es
  **un solo bit** (`SPREN`), no un bit por canal.

**Y la regla que resume todo**: cada cosa que se programa tiene un impacto en el rendimiento. Si no se
mide, no se sabe; y si no se sabe, no se puede afirmar que la demo funciona.
