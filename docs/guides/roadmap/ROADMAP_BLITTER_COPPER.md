# Roadmap — Blitter ↔ Copper

Evaluación de las técnicas «Copper lanza blits» y «Blitter escribe la copperlist» para
llevarlas al engine, partiendo de lo que ya hay.

## Qué ya tenemos

- **`FramePlan`** (jobs de Blitter: `CopyRect`/`LogicBlit`/`Line`/`C2P`/…), ejecutados por
  `AmigaBackend::execute_frame_plan` por **CPU** (`wait_blitter` antes de cada job).
- **`copper::Plan`/`Scheduler`/`StaticPlan`**: la copperlist se construye por **CPU** (moves) y
  se publica por **doble buffer** (`DoubleBuffer`, swap `COP1LC`); parcheo por `PatchHandle`/
  `Patch32`/`Template`.
- **`blitter_memcpy`** (copia lineal, `wait` síncrona/asíncrona), **`blitter_busy()`** (BBUSY) y
  **servicio de fondo** (`set_blitter_service`, drenado durante la espera).
- `SpriteLayer`, C2P, `pattern_fill`, `area_fill` (blits).

Conclusión: tenemos las **piezas** (cola de blits por CPU, CL por CPU + doble buffer, espera de
Blitter). Faltan los **puentes** Copper↔Blitter.

## Técnica A — Copper lanza blits (Copper → Blitter)

El Copper escribe `BLTCON*`/punteros/módulos/`BLTSIZE` en una línea, disparando un blit
**sincronizado al haz** (reparación de bordes de scroll, copia de la columna nueva, HUD dirty,
cola de blits).

- **Encaje**: un `CopperIntentKind::BlitterJob` (o `LaunchBlit`) con los registros a programar y
  la línea; el `Scheduler` ya emite moves a registros y el `Plan` ordena por scanline.
- **Beneficio**: momento **exacto** del haz sin CPU (menos jitter); la CPU solo prepara la lista.
- **Riesgo**: el Blitter es **único** → serializar con los blits de CPU (huecos seguros:
  post-`DIWSTOP`, bordes, fuera del fetch); ver `blitter-memcpy.md` §Concurrencia.
- **Fases**: (1) intent + emisión; (2) política de «ventana segura»; (3) demo (borde de scroll).
- **Estado**: hecho. `CopperIntentKind::BlitterJob` + `BlitterJob` (`raster_intent.hpp`) +
  `Scheduler::emit_blitter_job`/`set_blitter_window` (ventana segura). `takeover_display`
  activa **`COPCON`/`CDANG`**: sin él el Copper **no puede** escribir los registros del Blitter
  (<0x80) y su primera escritura lo detiene (`WinUAE custom.cpp:2835-2840`). Validado en
  `tests/host/graphics/260_copper_blitter` y en `demos/techniques/amiga/blitter/210_copper_blitter`.
  **Serialización obligatoria**: si el blit del Copper cae **mientras corre** un blit de CPU
  largo, su `BLTSIZE` lo **aborta** y el display de bitplanes se rompe; la ventana segura debe
  evitar *ese tramo* (en la 210, el borde inferior tras el blit de CPU del scroll). Ver
  `blitter-memcpy.md` §Concurrencia.

## Técnica B — Blitter escribe/parchea la copperlist (Blitter → Copper)

El Blitter trata la copperlist como **destino**: genera o parchea en bloque waits/moves
(gradientes densos por línea, punteros, colores), con doble buffer.

- **Encaje**: `copper::Template`/`PatchHandle` ya parchean desde CPU; el salto es que el
  **Blitter** rellene un tramo de la lista (p. ej. N `COLORxx` por línea) en vez de la CPU.
- **Beneficio**: offload de CPU cuando hay **muchos** moves por frame.
- **Riesgo**: presupuesto de Chip y de tiempo de blit; solo compensa con muchos moves.
- **Fases**: (1) medir (moves/frame reales); (2) prototipo de parcheo por Blitter de un tramo de
  la CL; (3) evaluar.
- **Estado**: prototipo hecho (fase 2). Un `graphics::BlitJob` (`CopyRect`, 1 word de ancho,
  `destination_modulo_bytes = 2`) enviado con `AmigaBackend::blitter_submit` escribe los **data
  words** de `count` MOVEs consecutivos (stride 4 B) sin tocar los registros. Validado en
  `demos/techniques/amiga/blitter/210_copper_blitter` (`copperlist patch (Tecnica B): OK`). El mismo `BlitJob` cubre
  el **borde de scroll** (`CopyRect 20×256`, `mods = 2`). Pendiente: (1) medir moves/frame reales
  y (3) evaluar si compensa.

## Técnica C — Blitter IRQ → completar / rearmar

El fin de blit genera IRQ (nivel 3); el handler marca una bandera / encola en
`task::BackgroundQueue` o `eng::os`, o rearma la siguiente oleada.

- **Estado**: la **IRQ de Blitter ya existe** — `level3_dispatch` (atiende el bit `BLIT`) llama a
  la tarea de `install_blit_service`/`set_blit_service`. Esa tarea **es** la notificación de fin
  (opcionalmente programable).
- **Falta**: (1) el **puerto de mensajes del mini-SO** (`eng::os`, documentado, sin implementar)
  para que el aviso sea un `Msg` de la cola; (2) una API cómoda de «fin de copia» sobre
  `blitter_memcpy(wait=false)`.
- **Beneficio**: uso asíncrono **seguro** sin polling.

## Prioridad

1. **C** — pequeño y desbloquea el asíncrono seguro (base para A/B).
2. **A** — la técnica más útil para juegos/demos (blits sincronizados al haz).
3. **B** — solo si el perfil muestra **muchos** moves/frame (si no, no compensa).

## Línea de trabajo: primitivas por lotes

Para escenas 3D con muchas líneas, la optimización prioritaria debe atacar primero la preparación
en CPU y después la programación de registros. Una lista compacta de segmentos reduce el tamaño de
la descripción y evita duplicar la misma geometría para cada bitplane, pero no elimina el lanzamiento
hardware: el Blitter sigue dibujando cada línea como una operación secuencial. La ganancia debe
demostrarse con perfil, no suponerse por el mero cambio de contenedor.

1. **Medición base**: registrar ciclos de construcción del `FramePlan`, trabajos por frame,
   arranques del Blitter, escrituras de registros, esperas y tiempo total para wireframes de varias
   densidades y máscaras de planos.
2. **Batch compacto**: añadir una descripción de segmentos con destino, máscara de planos,
   operación y estilo; traducirla inicialmente al camino existente para validar la API sin cambiar
   el resultado.
3. **Ejecución directa**: calcular una vez por segmento el octante, error, módulos y tamaño, y
   reutilizar esos datos al recorrer los planos activos. Mantener el orden original como política
   predeterminada.
4. **Caché de estado**: centralizar las escrituras de registros y omitir solo los valores comunes
   que no hayan cambiado. Invalidar la caché cuando otro camino pueda tocar el Blitter o cuando se
   cambie de propietario.
5. **Agrupación segura**: agrupar por destino, operación, módulo y estilo solo cuando el contrato
   declare que la reordenación es válida. `Clear`, `OR`, `EOR`, máscaras y rellenos conservan su
   orden salvo prueba explícita de equivalencia.
6. **Patrones de línea**: sustituir el `BLTBDAT = $FFFF` fijo por un estilo que transporte textura,
   fase y `ONEDOT`, y agrupar segmentos con el mismo estilo.
7. **Patrones de polígonos**: extender el seam para patrones de una y varias filas, manteniendo
   separadas la cobertura del área y la operación lógica. Validar contorno, paridad y relleno con
   `FILL_XOR`/`FILL_OR`.
8. **Comparación final**: contrastar jobs actuales, batch compacto, batch con geometría compartida
   y batch con caché mediante bitmap equivalente, conteo de registros, ciclos y memoria.

La API pública debe presentar segmentos, estilos y patrones reutilizables; el backend conserva la
responsabilidad de convertirlos a registros y de aplicar las restricciones de orden, Chip RAM y
serialización del único Blitter.

## Línea de trabajo: copias, cookie-cut y fast blobs

Las optimizaciones de BOB deben priorizar la reducción de lanzamientos y de escrituras de registros
antes que las microoptimizaciones del minterm. El plan recomendado es:

1. **Medición por operación**: separar ciclos de preparación, escrituras de registros, esperas,
   lanzamientos y tráfico estimado para `CopyRect`, cookie-cut, `OrBlob` y `LogicBlit`.
2. **Fast path por formato**: usar un solo blit para BOBs intercalados cuando el asset, la máscara
   y el destino cumplen el contrato; comparar contra el camino planar por bitplane.
3. **Ejecutor lógico parametrizado**: generalizar el patrón de `OrBlobBatch` para cookie-cut y
   operaciones `OR`/`AND`/`XOR`, fijando el estado común una vez y cambiando solo punteros y tamaño.
4. **Caché de registros**: omitir escrituras de estado común sin cambios, con invalidación al
   cambiar de propietario o al entrar desde una ruta que programe registros directamente.
5. **Copias desplazadas**: separar rutas alineada y desplazada, y agrupar por `source_shift`,
   módulos y máscaras compatibles.
6. **Política de minterm**: seleccionar copia, OR blob, cookie-cut o clear según el contrato del
   asset, evitando pagar canales y máscaras que no se usan.
7. **Save/restore**: medir `save-under`/`restore` frente a framebuffer doble, fusionar dirty
   rectangles compatibles y omitir trabajo de objetos estáticos o invisibles.
8. **Assets preparados**: cachear máscaras alineadas y layouts intercalados; generar variantes de
   desplazamiento solo cuando el perfil demuestre que el coste de memoria se recupera.
9. **Equivalencia**: comparar cada fast path con la ruta de referencia píxel a píxel, incluyendo
   bordes no alineados, desplazamientos, transparencia, solapamientos y varios bitplanes.

El contrato público debe describir intención (`opaque`, `masked`, `additive`, `logic`, `restore`),
formato y política de composición. La traducción a canales A/B/C/D, módulos, minterms y layout
intercalado sigue siendo responsabilidad del backend Amiga.

## Línea de trabajo: Copper en el hotpath

`copper::Plan` ya ordena las intenciones por posición raster relativa mediante un índice (`m_perm`)
y resuelve la prioridad dentro de cada línea. El orden existente facilita la fusión, pero no la
realiza completamente: `materialize()` sigue llamando al emisor con una intención cada vez. El
objetivo es agrupar las rachas compatibles después de ordenar, sin cambiar la semántica de prioridad.

### Paso activo: saltar resolución de prioridad sin colisiones

`sort_by_top()` cuenta las intenciones por línea. Si el máximo por línea es uno, `sort_priority_within_lines()` no recorre las 256 líneas: no hay prioridades que arbitrar y el orden raster ya es el resultado. La ruta con varias intenciones conserva el insertion sort estable previo, incluidos sus desempates FIFO. El indicador se calcula en la pasada de conteo existente, sin otra pasada ni almacenamiento por intención.

Verificación funcional: HOST-070 compara listas, orden raster, prioridad, cruce PAL, overflow y doble buffer. Medición pendiente: comparar ciclos de `sort_priority_within_lines()` con `tools/debug/profile.mjs` en CopperPlanScene y en una escena con varias intenciones por línea; no asignar ganancia numérica antes de esa captura. Después, decidir con el perfil si medir emisión por grupos compatibles.

1. **Medir por fases**: separar `sort_by_top`, `sort_priority_within_lines`, `emit`, cielo y parcheo;
   contar intenciones, grupos, WAITs, MOVEs, palabras y ciclos.
2. **Preconstruir estructura estable**: mover cielo, raster bars, gradientes y listas fijas a
   `StaticPlan`/plantillas de doble buffer; por frame parchear solo palabras de datos.
3. **Separar cambios**: distinguir `structure_dirty` de `values_dirty`; si solo cambian colores o
   punteros, no ordenar ni materializar de nuevo.
4. **Fusionar por línea y tipo**: formar grupos contiguos tras el orden raster/prioridad. Un grupo
   `PaletteLine` emite un WAIT y varios MOVEs compatibles.
5. **Batch especializado de paleta**: añadir una ruta compacta para cambios `{línea, registro,
   valor}` que evite el despacho general y el bucle de un único elemento.
6. **Eliminar redundancias**: eliminar WAITs repetidos y MOVEs consecutivos redundantes solo después
   de resolver prioridades y sin mezclar operaciones con semántica diferente.
7. **Saltar trabajo innecesario**: omitir la ordenación de prioridad si no hay conflictos, limpiar
   solo líneas tocadas en listas dispersas y guardar `raster_key` al añadir la intención.
8. **Reducir indirección**: conservar `m_perm` para evitar copiar estructuras, pero emitir desde
   grupos `{line, first, count, kind}`.
9. **Separar debug y producción**: usar `SchedulerT<false>` sin contadores/Timeline en release y
   `SchedulerT<true>` en debug/profiling, verificando que las rutas rápidas cumplen el contrato.
10. **Validar equivalencia**: comparar bitmap/copperlist, prioridad de última escritura, ventanas de
    Blitter, `PaletteSpan`, registros distintos, WAITs, MOVEs y ciclos antes de activar cada ruta.

La fusión es segura cuando conserva el orden de prioridad dentro de la misma línea y agrupa solo
operaciones compatibles. `PaletteLine` puede fusionarse con otros `PaletteLine` de la misma línea;
no debe fusionarse automáticamente con `PaletteSpan`, `ShiftLines`, `BitplaneSplit`, `Priority` o
`BlitterJob`.

## Cuándo **no** compensa

- Un blit trivial por frame: la CPU lo programa en VBlank.
- Direcciones que dependen de un cálculo solo disponible **en el instante** del wait (salvo CL
  precalculada).
- Lógica/IA/audio: siguen en CPU.
- Listas Copper enormes generadas por Blitter cada frame en A500: se comen el presupuesto.

## Referencias

- `docs/reference/amiga/techniques/blitter-memcpy.md`, `copper-timing-and-budget.md`
- `docs/engine/architecture/{DISPLAY_COMPOSITION,SCENE_COMPOSITION}.md`
- Fuente del emulador (mecanismo): `AGENTS.md` §1.11
