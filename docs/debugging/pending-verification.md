# Repaso pendiente: generalidad de interfaces y modelado del engine

Notas de deuda de diseño detectadas al trabajar en el modelo de escena, para una
**batida posterior** (no son bugs inmediatos).

## 1. Interfaces elementales / wrappers redundantes (regla nueva)

- **Regla**: no crear funciones "preset" elementales que solo rellenan 2-3 campos de un
  struct de configuración (p. ej. los antiguos `planar4`/`canvas`/`ham`/`ehb`). Desde fuera
  sugieren implementaciones muy distintas y en realidad son *vanilla*. Preferir **una
  función paramétrica** bien documentada, y dejar los **escenarios de uso en comentarios**.
- **Aplicado**: los cuatro presets se sustituyeron por `scene::planar(width, height, planes)`
  con escenarios en el doc-comment (EHB = 6 planos, HAM/cuadruplicado = `rows` + `row_repeat`,
  canvas = `layout = Interleaved`, doble buffer = `buffers = N`).
- **Batida pendiente**: revisar el resto del engine por el mismo patrón (funciones que solo
  rellenan un struct, nombres que describen *un caso* y no el mecanismo, `*4`, `*6`, `*_320x256`,
  `make_*` triviales). Aplicar la regla de que **los nombres de métodos y clases sean genéricos
  y describan lo que hacen**, no una configuración concreta.

## 2. Código de bajo nivel en la aplicación (modelado incompleto)

- En HOST-067 el test tenía que recorrer la copperlist con `u16*` para comprobar a qué bitmap
  apuntaba el `BPLxPT` de un plano. **Señal de que faltaba modelar el estado observable del
  display**: si la única forma de verificar/consultar algo es leer palabras crudas, falta una
  abstracción.
- **Aplicado**: `Scene::display_plane_address(p)` (dirección efectiva del `BPLxPT`) y
  `Scene::display_plane_uses(p, buffer)` (¿el registro `p` apunta al plano del buffer `index`?).
  El código de aplicación ya no necesita `active_words()` ni `u16*`.
- **Bug real corregido de paso**: `reverse_ptrs` + doble buffer no permutaba al repuntar en
  `commit` (el registro `p` mostraba el plano `p` en vez de `planes-1-p`). Ahora
  `Scene::set_plane_patch_source(p, source, patch)` guarda la permutación y `commit` la respeta.
- **Batida pendiente**: buscar otros lugares donde la app/test tenga que leer palabras de
  copperlist, punteros o registros para *observar* estado; añadir la consulta tipada que falte.
  Revisar si `Plan::active_words()` debe seguir siendo público o quedar interno/de depuración.

## 3. Publicación de la copperlist en `Scene`

- `Scene::commit()` parchea los `BPLxPT` pero **no reinstala** la copperlist; ya no existe
  `Scene::install(backend)`. En demos estáticas (040/050/051/052/060/100_virtual) se eliminó el
  `install()` por frame. **Verificar en WinUAE** que el display sigue correcto y, si hace falta
  publicar cambios estáticos una vez, decidir dónde (`takeover` ya publica; documentar la
  semántica exacta de "estático vs por-frame").

## 4. Validación de copperlist (dinámica en setup, no en hot path)

- El modelo ya valida presupuesto **por línea** (`copper::Timeline`/`ScheduleReport`:
  `heavy_palette_zones`, `timeline_over_budget_lines`, `has_visible_timeline_spill`) y
  **overflow** de capacidad (`Plan::overflow`/`ok`), ambos en `materialize`/`end_frame` (una
  vez por frame, no por MOVE). La emisión (`move`/`wait`, `always_inline`) no valida nada por
  diseño (hot path).
- **Aplicado (estático)**: `scene::row_repeat_words(rows, repeat, first_line)` (huella
  `constexpr` de la etapa) y `scene::copper_word_budget(res)` permiten `static_assert` en
  compilación; HOST-016 fija el valor (2050 palabras para 64×4 desde `0x2c`) y comprueba que
  coincide con `scheduler().words_used()` antes/después de la etapa. Regla: **estáticas
  siempre; dinámicas solo en setup/cierre de frame**, nunca en la emisión por MOVE.
- **Pendiente**: cubrir con la misma huella otras etapas de forma conocida (`display`,
  `palette`, `palette_zones`) y estudiar si `end_build()` debe rechazar/reportar el desborde
  en vez de solo marcarlo.

## 5. Mínimo ancho por número de planos (DMA de Agnus)

- **Reinterpretado con fuente**: no existe un **ancho mínimo de fetch** ligado al nº de
  planos; el ancho de fetch depende de `DDFSTRT/DDFSTOP`, no del *depth*. Lo que sí cambia con
  los planos es el **coste de bus** (slots/línea que compiten con la CPU). Fuente:
  `amiga-bootcamp/01_hardware/common/dma_architecture.md` (20 palabras/plano a 320 px; a 6
  planos el display usa ~65 % del bus).
- **Aplicado**: `scene::DmaCost` + `scene::dma_cost(res, limits, fw)` (informativo, **no**
  rechazo) y `scene::FetchWidth` (1×/2×/4×, `FMODE` en AGA). HOST-216 fija los valores
  (4 planos OCS = 80 slots de bitplane/119 de CPU; 6 = 120/79; AGA 8 a 4× = 40/159).

## 6. ECS/AGA: perfiles reales

- **Aplicado**: `aga_a1200` pasa a 8 planos (256 colores), HAM8 (`max_planes_ham=8`), DPF 4+4
  y `fetch_width_max=4` (`FMODE` 4×); `ecs` queda igual que OCS en **lores** (mismos planos,
  modos y fetch). Fuente: `aga_a1200_a4000/{chipset_aga,aga_display_modes}.md` y AHRM
  Apéndice C. Verificado por HOST-216.
- **Pendiente**: ECS añade SuperHires (1280 px, ≤ 2 planos) y `DIWHIGH` (rangos de ventana
  mayores); AGA añade hires/superhires y 24-bit. Están fuera del modelo lores actual
  (`SceneResources` solo describe lores) y se añadirán cuando haya consumidor.

## 7. `compose_unchecked` / `init_unchecked`: retirados

- Se comprobó que **ningún test ni demo** los usaba (solo el propio `compose.hpp` y la doc).
  No aportaban una vía real (el perfil ya es obligatorio), así que se eliminó
  `compose_unchecked` y `Scene::init_unchecked` pasó a `init_raw` **privado**. La única vía
  pública de construcción es `init`/`compose` con `DisplayLimits`.



