# Lecciones del engine C++23 (math/util/efectos)

Bitácora de lecciones de proceso del desarrollo del engine C++23 (no son referencia de
diseño: describen errores y reglas para no repetirlos). Se enlaza desde
[README.md](README.md).

## 1. Un caso de test que falta esconde un bug (efecto de paleta)

`PaletteTransitionEffect` se dio por bueno con el modo vaivén (`ping_pong = true`, demo
040). El test que faltaba —una **sola pasada** (`ping_pong = false`)— destapó que
`update` hacía `pos %= frames` en vez de **clamp a `frames`**: al terminar la transición
volvía a negro en vez de quedarse en el destino. Regla: escribir primero el caso
**de ida y no vuelta** (0 → destino y se queda), no solo el oscilante.

## 2. `-Werror=narrowing` en los tests host

Activar `-Werror=narrowing` en `tools/run-host-tests.sh` encontró dos estrechamientos
reales que eran solo warnings:
- `scalar_traits<Fixed>::from_int` (`static_cast<R>(i) << E` → `int` a `R`);
- el braced-init de `pointer_offset` en `tile_scroll.hpp` (`long long` a `u32`).

Regla: un estrechamiento en un braced-init casi siempre es una **pérdida de precisión no
intencionada**; se corrige con un `static_cast` explícito (documenta la intención), no
silenciando el warning.

## 3. El compilador ya optimiza lo que creías que aportabas (`sincos`)

`rotate2(v, ángulo)` + `fixed_sincos` se introdujeron para “una sola lectura de tabla”.
Medido A/B a `-O2`/`-Os`: gcc **ya CSE-ea** el índice común de dos `scalar_sin`/`scalar_cos`
del mismo ángulo (96 B en ambos casos). El valor real es la **garantía estructural** y un
call-site más claro, **no** una reducción de código medible. Regla: medir A/B antes de
afirmar una mejora de rendimiento o tamaño.

## 4. No duplicar `constexpr` en el mismo namespace (`atan_d`)

`fixed_math.hpp` definía su propio `detail::atan_d` y `scalar_math.hpp` necesitaba otro
para `scalar_atan2<float>`: al ser el **mismo namespace** (`eng::math::detail`), era una
redefinición. Regla: antes de añadir un helper `detail`, comprobar si ya existe en otro
header del mismo namespace y **unificar** (el de `scalar_math` pasó a usarlo `fixed_math`).

## 5. Convención de un efecto: `runtime_palette()` se refresca en `apply_into`

`PaletteCycleEffect`/`PaletteTransitionEffect` recalculan su paleta runtime dentro de
`apply_fixed()`, invocado por `apply_into()` (no por `update()`). Un test que leía
`runtime_palette()` tras `update()` vio estado obsoleto. Regla: para el driver,
**actualizar y aplicar van juntos**; el puntero a la paleta runtime es estable para el
`scene_config`, pero su contenido es válido tras `apply_into()`.

## 6. Numeración de tests host: única y no reutilizable

`022_fast_div` y `022_rotozoom` coexistieron con el mismo `HOST-022` (uno sin catalogar).
Regla (ver `tests/host/README.md`): el número no se reutiliza; ante colisión se renumera
el test **más nuevo**. Hay un check de codificación/links, pero **no** automático de
numeración: conviene añadirlo.

## 7. Separar “reference” de “bitácora”

Los documentos de referencia describen el estado vigente; las lecciones de proceso (este
fichero) y las bitácoras viven aparte. Evita narrar historia en cabeceras y en documentos
de arquitectura.

## 8. No re-emitir el copper que no cambia (demo 086)

La demo 086 (BOBs) tenía el framerate clavado por el **copper**: 8 BOBs + cielo continuo
(256 intenciones) = 7,1 fps (994k ciclos/frame), y el perfil daba `copper` al 67%
(`materialize` 458k: emitir 288 intenciones ≈ 1.089 ciclos/intención; `sky` 107k por
recopiar la lista). Con el cielo **constante**, la lista entera (display + paleta + 256
intenciones) es idéntica cada frame: construirla **una vez** en `init` y saltar
`build_frame` en `update` bajó `copper` de ~68k a ~0 y cruzó de **2 a 1 campo** →
**49,92 fps**. Lección: antes de optimizar la emisión, preguntar **si hay que emitir**;
separar lo estático de lo dinámico. Corolario medido: con copper dinámico, `actors` +
`blits` de 8 BOBs ya superan un campo, así que 50 fps es inalcanzable en ese modo (el
techo real lo fija el número de objetos, no el copper).
