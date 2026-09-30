# Rasterizado: CPU y Blitter tras una interfaz uniforme

`field::Surface` es el **contexto de dispositivo** del engine: expone una API de dibujo
uniforme (`set_pixel`, `draw_line`, `fill_rect`, `fill_polygon`, `blit`, `blit_masked`,
`draw_text`) sin que el consumidor sepa si detrás hay **CPU** o **Blitter**. Este documento
describe el *seam* `field::Rasterizer` (`engine/include/eng/field/raster.hpp`) que hace
transparente esa elección.

```
   Surface  (API estable)
      │  fill_rect / blit / blit_masked
      ▼
   Rasterizer  (seam)
   ├── CpuRaster      → Playfield::draw_span_op (RasterOp) / copy_rect_cpu
   └── BlitterRaster  → Playfield::fill_polygon (PolygonFillSink/Blitter) / FramePlan
```

## Tipos

| Tipo | Quién lo define | Qué describe |
|---|---|---|
| `RasterOp` (`Copy`/`Or`/`And`/`Xor`/`Clear`) | engine | Operación lógica de una escritura (CPU: lógica de palabras; Blitter: `minterm`). |
| `RasterCaps` | **backend** | Hay Blitter, ancho de bus (16 OCS / 32-64 AGA), fill/line/shift/minterms, `setup_cycles`. |
| `RasterPolicy` | **app/escena** | `AccelMode::Auto`/`Cpu`/`Blitter`, área mínima para el Blitter, `cpu_fast`. |
| `Rasterizer` | engine | Interfaz: `fill_rect` / `copy_rect` / `copy_masked`. |

## Implementaciones

- **`CpuRaster`**: relleno por scanline (`Playfield::draw_span_op`, con `RasterOp`), **línea**
  (`cpu_line`: Bresenham **agrupado en spans por fila**, recortada al `ClipRect`) y copias por CPU
  (`Playfield::copy_rect_cpu` / `copy_masked_cpu`). La copia usa stores de **32 bits** cuando
  `RasterPolicy::cpu_fast` está activo y origen/destino quedan alineados a 4 (ruta `move.l`; *CPU
  blit assist* del 68020, ver `../guides/optimization/OPTIMIZACION_GPP_68000.md`). Sin
  multiplicaciones de 32 bits en el bucle (`mulu16` + avance de punteros).

### Primitivas CPU optimizadas (`field/cpu_primitives.hpp`)

El dibujo CPU **no** va píxel a píxel: las primitivas pintan por **tramos horizontales**
(`Playfield::draw_span`, que agrupa 16/32 px por escritura). Son **portables** (operan sobre
`Playfield`, sin chipset) y son el *fallback* CPU canónico y la base para plataformas sin Blitter
(p. ej. el futuro port a **Atari ST**).

| Rutina | Qué hace |
|---|---|
| `cpu_fill_rect` | Rellena el rect por spans (una fila = un tramo). |
| `cpu_line` | Línea con Bresenham **agrupado por fila**: misma cobertura que Bresenham, `draw_span` por fila en vez de píxel a píxel (de `\|dx\|` a `\|dy\|+1` escrituras en líneas horizontales). |
| `cpu_fill_polygon` | Polígono convexo por **even-odd** con `draw_span` por fila (edge table). |

Equivalencia verificada en **HOST-267** (la línea cubre el Bresenham de referencia; el polígono
rellena sin agujeros).

- **`BlitterRaster`**: relleno por `Playfield::fill_polygon` (usa el `PolygonFillSink`/Blitter si
  está instalado; si no, CPU); copias por el `FramePlan` (`CopyRect`/`MaskedBobCookieCut`, con
  `source_shift` y `descending`); **línea** por Blitter si se pasa un `FramePlan` y la línea cae
  dentro del clip (encola `BlitJobKind::Line`, o `LineEor` si `op == RasterOp::Xor`, que el
  backend ejecuta con `blitter_line`/`blitter_line_eor` por plano); si no, CPU (Bresenham).

## Selección (backend → escena)

El backend declara sus capacidades y ofrece un atajo para instalar el rasterizador; la escena
no conoce al backend:

```cpp
// amiga_backend (OCS/AGA): Blitter de 16 bits, fill/line/shift/minterms
backend.install_raster(scene);   // elige kBlitterRaster/kCpuRaster segun raster_caps()
scene.surface().fill_rect(x, y, w, h, color, field::RasterOp::Xor); // misma llamada
```

`Scene::set_raster(rasterizer, policy)` fija la elección; `Surface` la lee del playfield. Un
backend host (sin Blitter) declara `RasterCaps{ .blitter = false }` y se usa `kCpuRaster`.

## Estado y extensión

- **Hecho**: `RasterOp` (CPU), relleno CPU/Blitter (con umbral `Auto`), **línea CPU, por
  Blitter y EOR/ONEDOT** (`BlitJobKind::Line`/`LineEor` + `Rasterizer::draw_line`, con recorte
  de segmento), copia CPU (32 bits) y Blitter con `source_shift`/`descending`, **blit lógico**
  (`BlitJobKind::LogicBlit`, `B = D` + minterm: sombras/glow/máscaras), copia enmascarada CPU y
  Blitter, **blit lógico** (`BlitJobKind::LogicBlit`, `B = D` + minterm: `Surface::blit_shadow`
  `$C0` / `blit_glow` `$FC`), copia enmascarada CPU y Blitter, y **colisión pixel-perfect**
  (`field::collide_cpu` + `AmigaBackend::blitter_collide`, verificada en hardware por el
  self-test de 077); `install_raster` con `RasterCaps` OCS/AGA por target; `row_bytes` a 4.
- **Operaciones Blitter que aún NO cubre el seam** (ver `frame_plan.hpp`/AHRM cap. 6):
  - **Relleno con patrón** (suelos/techos 3D): `fill_polygon` con una fuente de patrón en vez de
    color plano; requiere un `BlitJob` de fill con canal de patrón (más complejo que el FILL_OR
    actual).
  - **C2P** (chunky→planar): uso especializado multi-fase del Blitter (ver `C2P_BLITTER.md`).
  - **Línea EOR en lote**: el backend ya fija los comunes una vez por racha `LineEor` (077 la
    ejercita). La demo 116 **no** se migra: su ruta por defecto ya es una utilidad del engine
    (`retro::flat_shade_xor`, HOST-213) y las rutas `draw_edges`/asm son opt-in de diagnóstico.

## Dirección para primitivas por lotes

La ruta actual es correcta como seam inicial, pero una aplicación con muchas primitivas dinámicas
puede pagar demasiado trabajo de CPU al convertir cada segmento en un `BlitJob` completo. La
dirección recomendada es añadir batches compactos de dominio, sin exponer registros del Blitter a
la aplicación:

```text
Surface / Wireframe
        │ segmentos, color, operación, estilo
        ▼
PrimitiveBatch compacto
        │ agrupación segura por destino y estado
        ▼
Encoder / ejecutor del backend
        │ reutiliza estado común; cambia solo estado dependiente de la primitiva
        ▼
Blitter
```

El batch de líneas debe conservar la geometría y una máscara de planos, en vez de duplicar todos
los metadatos por plano. El backend calcula una vez por segmento el octante, los incrementos, el
error y el tamaño, y aplica esa geometría a cada plano activo. La expansión a planos sigue siendo
necesaria porque el hardware escribe bits independientes; el batch no convierte varias líneas en
un único blit.

La agrupación solo puede reordenar operaciones declaradas compatibles. Debe conservar el orden al
mezclar `Clear`, `OR`, `EOR`, rellenos, máscaras, destinos distintos o cualquier operación cuyo
resultado dependa de la secuencia. El camino conservador mantiene el orden original y usa el batch
solo para reducir representación y trabajo de preparación.

El ejecutor puede mantener una caché de estado común del Blitter. Debe distinguir estado común
(`BLTCON` estable, máscaras, módulos y estilo) de estado por primitiva (punteros, acumulador y
`BLTSIZE`). La caché se invalida si otro camino puede escribir registros custom, al cambiar de
backend o al perderse la propiedad del ejecutor. La optimización es válida únicamente con una
prueba de equivalencia píxel a píxel y una medición de escrituras de registros evitadas.

## Estilos y patrones

El estilo de línea debe formar parte de la intención de dominio, no del destino. Debe poder
describir una línea sólida o texturada, su desplazamiento inicial del patrón, `ONEDOT` y la
operación lógica (`OR`/`EOR`). El backend traducirá el patrón a `BLTBDAT` y su estado asociado;
los segmentos consecutivos con el mismo estilo podrán agruparse. El valor sólido actual es
`$FFFF`, pero no debe ser una limitación del API final.

El relleno de polígonos necesita dos capacidades separadas: patrón de cobertura del área y
operación de combinación con el destino. Para un patrón de una fila, el Blitter puede repetir la
fuente mediante su módulo; para un patrón de varias filas, el API debe aceptar una vista con
`row_bytes` y altura, y el backend decidir si reprograma la fuente por fila o emite varios blits.
La fase de contorno y la fase de relleno deben compartir destino, clip y convención de planos para
preservar la paridad del `FILL_XOR`/`FILL_OR`.

La API de alto nivel debe ofrecer estilos y patrones como objetos reutilizables, por ejemplo
`LineStyle` y `FillPattern`, mientras que la representación de ejecución puede internar patrones,
agrupar referencias y evitar copiar sus datos. El color sigue siendo un índice planar o una
operación lógica; la paleta visible continúa siendo responsabilidad de la composición de display.

## Criterio de validación

La evolución debe compararse en cuatro rutas: jobs actuales, batch compacto, batch con geometría
compartida y batch con caché de estado. Hay que medir construcción de la cola, escrituras de
registros, arranques y esperas del Blitter, tiempo total y memoria de la descripción. La prueba de
corrección debe comparar el bitmap resultante con la ruta de referencia para líneas sólidas,
`EOR`/`ONEDOT`, patrones de línea, rellenos planos y patrones de una y varias filas.

## Mejoras de copias y operaciones lógicas

Las operaciones `CopyRect`, cookie-cut, `OrBlob` y `LogicBlit` tienen una oportunidad de mejora
común: el coste dominante puede ser el número de lanzamientos y la reprogramación de registros,
no el minterm que ejecuta el Blitter. Estas mejoras son propuestas de evolución del engine y deben
validarse con perfiles y equivalencia de bitmap.

### Layout intercalado

Un BOB cookie-cut sobre planos separados necesita normalmente un lanzamiento por plano. Un asset
intercalado puede recorrer las planelíneas de todos los planos con un solo lanzamiento, al precio de
preparar la imagen y la máscara en ese layout. El mismo principio ya está materializado para
`OrBlobBatch` y debe reutilizarse para cookie-cut cuando el destino lo permita. La máscara debe
estar replicada según el contrato intercalado del asset; no se debe asumir que una máscara de un
solo plano sirve directamente para todas las filas intercaladas.

### Estado común y operaciones por lote

La ejecución debe separar el estado común (`BLTCON`, máscaras, módulos, desplazamiento y minterm)
del estado variable (punteros y `BLTSIZE`). Para una secuencia compatible, el engine puede fijar el
estado común una vez y cambiar solo punteros y tamaño, como hace `OrBlobBatch`. El patrón debe
generalizarse a cookie-cut, copias desplazadas y `LogicBlit` OR/AND/XOR, siempre con un único dueño
de los registros y una invalidación explícita cuando otro camino pueda escribir el Blitter.

La agrupación puede usar destino, layout, minterm, desplazamiento, tamaño, módulos y máscara como
claves. El orden original se conserva por defecto: no se deben mezclar automáticamente clear,
restore, OR, cookie-cut y operaciones lógicas si el resultado puede depender de la secuencia.

### Selección de operación

El asset debe declarar la operación mínima que necesita. Un objeto opaco puede usar copia directa;
un efecto aditivo puede usar `D = A | D` (`$FC`); un objeto transparente necesita cookie-cut (`$CA`);
y un borrado puede usar D-only (`$00`). No se debe pagar cookie-cut cuando no hay transparencia ni
introducir una máscara para un blob aditivo.

### Copias desplazadas

Las copias alineadas deben usar la ruta C→D y las desplazadas la ruta A→D con el barrel shifter.
Conviene agrupar copias por `source_shift`, reutilizar módulos y máscaras, precalcular los módulos
de fuente y destino, y limitar correctamente la primera y la última palabra. El desplazamiento
impide reutilizar todo `BLTCON0`, pero no impide compartir el resto del estado compatible.

### Save/restore y dirty rectangles

En BOBs, `save-under` y `restore` pueden costar más que el dibujo. El engine debe poder fusionar
rectángulos sucios próximos, omitir save/restore cuando el framebuffer doble reconstruye el fondo y
usar `MaskedBlobNoSave` o `OrBlob` cuando la política visual lo permita. También debe descartar
restauraciones de objetos invisibles o que no hayan cambiado de posición. Esta política pertenece a
la escena o al gestor de objetos, no al encoder de registros.

### Máscaras y variantes de assets

Las máscaras de cookie-cut deben poder estar alineadas a palabra, cacheadas entre frames y
preparadas en formato intercalado. Si los desplazamientos frecuentes son conocidos, el pipeline
puede generar variantes del asset para esos desplazamientos; solo compensa si el ahorro de
programación o de DMA supera el coste de memoria. Las máscaras y variantes no deben regenerarse en
el bucle caliente sin una medición que lo justifique.

## Prioridades de rendimiento (ROI, verificado)

Cruce del estado real (jul 2026) con las propuestas de arriba. Ordenado por **retorno sobre
esfuerzo**; cada punto exige **test de equivalencia píxel a píxel** + **medición** (escrituras de
registro evitadas y ciclos) antes de darse por bueno.

1. **Caché de estado común del Blitter** (mayor ROI). Hoy cada `BlitJob` reprograma **todos** los
   registros en `blitter_job_from`. Para una **racha** con el mismo estado común (`BLTCON` =
   minterm + uso de canales + `ASH`; máscara; módulos; layout; `bitplane_count`) bastaría fijarlo
   **una vez** y cambiar solo **punteros + `BLTSIZE`** por job — exactamente lo que ya hace
   `OrBlobBatch` para blobs, **generalizado** a `CopyRect`/cookie-cut/`LogicBlit`. Clave de racha:
   `(minterm, source_shift, source_modulo, dest_modulo, mask_present, interleaved)`. **Invalidación
   obligatoria** si otro camino escribe custom (Copper, C2P, línea) o cambia de dueño. Evita ~N×K
   escrituras a registro por racha (K = registros comunes).
2. **Agrupación de blits por clave** (destino/layout/minterm/shift/tamaño): reordenar **solo**
   operaciones **declaradas compatibles**; conservar el orden si hay `Clear`/`EOR`/distintos destinos
   o cualquier resultado dependiente de secuencia. Reduce lanzamientos y facilita (1).
3. **Batch compacto de líneas** (`LineBatch`): hoy cada segmento se expande a un `BlitJob` **por
   plano** con todos los metadatos duplicados. Un batch conserva la **geometría + máscara de planos**
   y el backend calcula **una vez** octante/incrementos/error/`BLTSIZE`, aplicándolos a cada plano
   activo. No convierte varias líneas en un blit (el hardware escribe planos independientes), pero
   elimina el coste de preparación × planos.
4. **Cookie-cut intercalado**: un cookie-cut planar cuesta 1 lanzamiento por plano; un asset
   intercalado con la **máscara replicada** por fila de plano lo hace en **1 lanzamiento** (mismo
   principio que `OrBlobBatch`). Solo cuando el destino lo permita (planos contiguos).
5. **Estado precalculado en el sumidero**: `blitter_job_from` recomputa todo por job; el invariante
   (módulos, `BLTCON`) puede venir del **setup** (regla de coste: lo invariante fuera del bucle).
   Encaja con (1).

**Ya cubierto / parked:** vistas con banco (`MemView`/`Block`) — hecho; C2P y `LineEor` en lote —
parked/documentado; `BLTPRI` on/off — falta medición por caso (con `BLTPRI` el Blitter no cede slots
y el feeder por IRQ no solapa).

**Medir siempre** en cuatro rutas: jobs actuales · batch compacto · batch con geometría compartida ·
batch con caché de estado. Métricas: construcción de la cola, **escrituras de registro**, arranques
y esperas del Blitter, tiempo total y memoria de la descripción.

**Estado (jul 2026):** (1) caché de estado común por racha — **hecha** (`AmigaBackend::submit_blit_job`),
con contador de aciertos `AmigaBackend::blitter_common_hits()` (reprogramaciones evitadas, diagnóstico);
(2) agrupación por estado — **`FramePlan::sort_by_state()`** hecho (opt-in, HOST-388) y **medido** en
hardware con la demo **211_blit_state_bench** (rejilla de 112 tiles disjuntos de dos estados
intercalados): agrupar no cambia los lanzamientos (`448 = 112 × 4 planos`) ni el bitmap (capturas
byte-idénticas) y sube los aciertos de caché de **6 a 110** (≈832 escrituras a custom evitadas por
frame). El control homogéneo ya está saturado (**112/112**), así que `sort_by_state` es **neutro** cuando
todos los blits comparten estado: conviene activarlo solo en escenas **heterogéneas con destinos
disjuntos**. Sigue siendo responsabilidad del llamador garantizar esa independencia: en 104 el `add_shift`
(copia del ring) **depende del orden** respecto a los tiles, así que activarlo allí requeriría separar el
shift del lote o marcar los grupos independientes. El camino `inline` (`OrBlobBatch`) sigue siendo el más
rápido para lotes homogéneos; (1)/(2) acercan el camino del `FramePlan` heterogéneo a él.

## Verificación

- **HOST-212**: `RasterOp` (`Xor` dos veces = 0, `Or`/`And`/`Clear`), `BlitterRaster` (fill y
  copia por `FramePlan`), copia CPU/enmascarada CPU (píxeles + `blit_job_count`), **línea por
  Blitter** (encolado por plano y recorte con `clip_segment`) y `AccelMode::Auto`.
- **Sonda de codegen** `docs/guides/optimization/_probe_raster_copy.cpp`: 68000/68020 sin
  libcalls (`__mulsi3`/`__udivsi3`) y con `move.l` en la copia.
- **Demo (hardware)**: `077_math3d_cube` instala el rasterizador del backend, dibuja el cubo por
  CPU y **triángulos OR y EOR por Blitter** (`draw_line(&plan, …)` + `execute_frame_plan`),
  visibles en la captura, y hace un **self-test de `blitter_collide`** (colisión y no-colisión)
  en `init` (READY solo si pasa). `079_wireframe` dibuja todo el alambre por el seam
  (`Surface::draw_line` sobre un `ContiguousPlayfield` con `kBlitterRaster`). Verificado
  `build -> run -> analyze` (077; 079 renderiza aunque su `analyze` genérico pide colores de
  overlay que su paleta no tiene, deuda previa).
- **`RasterCaps` por target**: `raster_caps()` declara bus 16 (OCS) o 64 (`K_AGA`); 077 compila
  con `EXTRA_DEFINES="-DK_AGA=1"`. Programar `FMODE` (para usar de verdad 32/64 bits) es el paso
  siguiente del backend.

Referencias: `SCENE_COMPOSITION.md` §6.2, `DISPLAY_COMPOSITION.md`, `playfield.hpp`
(`PolygonFillSink`), `frame_plan.hpp` (`BlitJob`/`minterm`).
