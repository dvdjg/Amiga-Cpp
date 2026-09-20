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
- **Pendiente**: exponer esas validaciones de copperlist a la app de forma tipada (p. ej.
  `Scene::copper_report()` ya existe), añadir validación **estática** de la forma de las etapas
  que se conocen en compilación, y estudiar si `end_build()` debe rechazar/reportar el desborde
  en vez de solo marcarlo. Regla: **estáticas siempre; dinámicas solo en setup/cierre de frame
  nunca en el camino de emisión por MOVE**.

## 5. Mínimo ancho por número de planos (DMA de Agnus)

- Documentar con fuente fiable el **ancho mínimo de fetch según el nº de planos** (el DMA de
  Agnus necesita más ancho cuantos más planos: la última palabra de cada plano debe terminar a
  tiempo). No hay tabla en el AHRM 3.ª del repo; obtenerla (AHRM Apéndice C, guías de Agnus o
  pruebas en hardware/WinUAE) antes de añadirla a `validate` (código nuevo). No inventar valores.

## 6. ECS/AGA: afinar perfiles

- `ecs` y `aga_a1200` existen con valores conservadores. Afinar con el **Apéndice C** del AHRM
  (ECS: mayor ancho/fetch, blitter 16368×16384) y documentación AGA (FMODE, HAM8, 24-bit,
  DPF 4+4) cuando se incorpore al workspace.

