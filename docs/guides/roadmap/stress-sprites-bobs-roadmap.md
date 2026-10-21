# Roadmap de estrés: sprites HW y BOBs a 50 fps

Este roadmap define una batería determinista para revelar fallos del engine al mover objetos transparentes con carga de juego realista. El objetivo es validar el reparto y reuso de Sprite HW, attached, cadenas, degradación a BOB, composición, Blitter, colisiones y presentación sin flicker, y usar los casos como presión práctica para que la API de juego resulte sencilla y predecible. No declara validado ningún camino por el mero hecho de que exista una demo o un test host.

El soporte técnico ya disponible se describe en [ROADMAP_JUEGO_SPRITES_BOBS.md](ROADMAP_JUEGO_SPRITES_BOBS.md) y [ROADMAP_SPRITES_COMPLETO.md](ROADMAP_SPRITES_COMPLETO.md); el estado real de implementación permanece en [ROADMAP_UNIFICADO.md](ROADMAP_UNIFICADO.md), que es la única lista viva de estado. Este documento añade el diseño de la campaña de estrés y sus criterios de salida.

## Objetivos y alcance

- Verificar movimiento transparente a un frame de juego por VBlank PAL (50 Hz nominal), con cadencia uniforme y cero candidatos de flicker, tearing o corrupción.
- Reproducir condiciones de videojuego: cruces, solapamientos, colisiones, saturación, cambios de representación, movimiento rápido, wrapping y entradas/salidas por los cuatro bordes.
- Cubrir Sprite HW, BOB y combinaciones, usando la fachada de juego existente (`SpriteScene`, actores, compuestos, `BobLayer` y `FastBobLayer`) o la API de nivel alto que los sustituya. Las demos no programan registros ni administran canales manualmente para conseguir la carga.
- Hacer cada fase observable por herramientas deterministas y por un modelo local de visión; conservar secuencias, telemetría y veredictos reproducibles en el `VALIDATION.md` correspondiente.
- Exponer fricciones de API en vez de ocultarlas con código de test de bajo nivel; cualquier decisión manual de canal, attached, rearme o fallback se anota como una brecha de interfaz.

## Matriz común de ejecución

Cada test se ejecuta con los cuatro perfiles siempre que su contenido sea aplicable. Una variante no aplicable debe declarar la razón técnica y queda fuera de la afirmación de cobertura; no se computa como PASS.

| Perfil | Bitplanes de playfield | Colores de playfield | Condición de prueba |
|---|---:|---:|---|
| A | 3 | 8 | Presión de objetos con mayor margen de DMA de bitplanes. |
| B | 4 | 16 | Perfil equilibrado y referencia de compatibilidad. |
| C | 5 | 32 | Carga de bitplanes alta y conflicto potencial de registros de color con Sprite HW. |
| D | 6, EHB | 64 (32 + 32 half-brite) | Máxima profundidad de esta matriz; probar tonos base/half-brite y sus interacciones. |

La cifra de profundidad describe el playfield, no el número de colores disponibles para cada objeto. Los sprites normales usan tres colores opacos más transparencia; un par attached tiene capacidad de hasta quince colores opacos más transparencia. El perfil de estrés attached de esta batería utiliza intencionadamente siete colores opacos más transparencia para que el resultado sea identificable y comparable. La paleta de Sprite HW es un recurso compartido: los colores dominantes se asignan de forma única dentro de cada conjunto simultáneo que la paleta pueda representar; cuando el número de actores exceda ese espacio, la identidad visual se completa con formas/símbolos únicos y la telemetría declara el cambio de banco o la política aplicada. No se exige una cantidad físicamente imposible de colores simultáneos.

La resolución objetivo es 320×256 en PAL, o la ventana PAL estándar de la demo si su `INTENT.md` fija otra. El área jugable, la franja de depuración y las regiones recortadas se declaran explícitamente. Cada test incluye duración fija de 400–600 frames como base, con fases de calentamiento y puntos de captura en arranque, máxima carga, colisiones, rebotes, envolturas y final. Las secuencias de frames consecutivos cubren también transiciones de un único frame cuando el mecanismo de captura lo permita.

## Contrato visual y de instrumentación

Cada entidad lleva un marcador de identidad reconocible (color dominante y forma/símbolo); el fondo es contrastado y estable, con rejilla de baja frecuencia o bandas no periódicas que permitan localizar huecos y restos sin introducir alias visual. El identificador no depende únicamente del color, porque la paleta es finita y compartida. Para comparaciones automáticas se fijan los colores RGB esperados, tolerancia, silueta, tamaño, región y fase en el `INTENT.md` de la demo.

Las trayectorias son deterministas y parametrizadas por el frame. El guion de cada fase declara posiciones esperadas o cajas/regiones temporales verificables por herramientas, no por estimaciones de píxeles del VLM. El estado de ejecución publica como mínimo frame/fase, número de actores solicitados y representados, Sprite HW, pares attached, reusos por canal, BOBs por estrategia, degradaciones, colisiones y errores de composición. Los contadores de peor caso incluyen jobs/words de Blitter, coste/ciclos por frame y tamaño de emisión Copper cuando estén disponibles. Si un campo no existe todavía en la telemetría, el test registra la carencia como tarea del engine en lugar de inventar un valor.

La franja inferior reserva nombre de demo, versión y datos mínimos de depuración en un único bitplane, con texto por palabras, conforme a `AGENTS.md` §0. Las aserciones de contenido y movimiento excluyen esa región para no tratar el HUD como glitch del escenario.

## Catálogo de pruebas

### Grupo 1 — Sprite HW, reuso vertical y attached

**S1.1 — Carrusel de reuso vertical extremo (primera sonda).** Mantiene ocupados los ocho canales durante toda la altura útil. Cada canal representa una secuencia ordenada de al menos cinco objetos distintos, con el gap de reuso exigido por el allocator; los objetos consecutivos de una misma cadena tienen arte, color o símbolo identificablemente distintos. Se mezclan pares attached con siete colores opacos más transparencia y sprites libres de tres colores más transparencia. Las entidades se mueven en Y a velocidades y fases distintas, cruzan otras trayectorias y se envuelven por los bordes superior e inferior. Propósito: máximo estrés de allocator, composición y rearme/cadena; detectar pérdida, duplicación, fantasma y estado viejo al cambiar de dueño el canal.

**S1.2 — Cruces, solapamientos y bordes con attached y libres.** Ocho canales activos en combinaciones simultáneas de pares attached y sprites libres, con figuras de 32–48 píxeles de alto y reuso vertical cuando el planificador lo permita. Dos oleadas se cruzan en el centro; otras entidades entran y salen parcialmente por los cuatro bordes. Se verifican transparencia, prioridad de capas y respuesta visual al solaparse. El caso no presupone cuatro pares attached más canales libres a la vez: el guion fija una ocupación físicamente posible y repite una subfase libre para aislar el modo de tres colores.

**S1.3 — Cadenas verticales de proyectiles.** Dos o tres canales se reutilizan para ristras de ocho a doce proyectiles por cadena, con formas reconocibles y fases desfasadas; los canales restantes transportan actores grandes, preferentemente attached. La secuencia fuerza wrap, cambio de ocupante, reordenación vertical y colisiones. Debe ejercitar tanto la estrategia DMA encadenada como el rearme por Copper cuando el selector elija cada camino; si una estrategia no resulta seleccionable por el compositor de alto nivel, esa combinación se marca SIN VERIFICAR.

### Especificación de arranque para S1.1

La primera demo propuesta es `sprite_reuse_carousel`, dentro de `demos/techniques/amiga/sprites/`. El número 230 que aparecía en el borrador inicial es solo una propuesta antigua: al consultar `node tools/check/next-number.mjs demos/techniques/amiga/sprites`, el siguiente número libre es **224**. La ruta candidata es, por tanto, `demos/techniques/amiga/sprites/224_sprite_reuse_carousel/`; se debe volver a ejecutar el checker inmediatamente antes de crearla porque el número puede cambiar si se incorporan demos concurrentes.

```text
demos/techniques/amiga/sprites/224_sprite_reuse_carousel/
├── README.md
├── VALIDATION.md
└── src/
    ├── INTENT.md
    ├── VISION.md
    └── main.cpp
```

El `README.md` presenta el objetivo, la técnica, los cuatro perfiles, la búsqueda previa de abstracciones y los comandos de build/run/analyze. `src/INTENT.md` especifica elementos, firma visual, trayectorias y eventos temporales esperados; `src/VISION.md` debe respetar el gate vigente de `demo-docs` y conservar la evaluación canónica de `vision-final.mjs`, sin reemplazarla por prompts de depuración propios. `VALIDATION.md` empieza como NO VERIFICADA y guarda la matriz perfil×fase, fps/campos, telemetría, flicker, respuestas de visión y alcance realmente cubierto. Las preguntas específicas sobre una entidad se usan durante depuración, no como sustituto del informe visual canónico.

El `main.cpp` se desarrolla a partir del patrón vivo de `demos/techniques/amiga/sprites/216_attached_actors/src/main.cpp`: `SpriteScene` registra actores; `scene.emit` compone el frame y devuelve representaciones/placements; el pool de cocinado attached y los buffers DMA se reservan en Chip con tipos que preservan el banco; el scheduler publica planes, paleta y placements; `DoubleBuffer` evita reescribir la copperlist activa; el frame sigue el latido de VBlank del engine. El ejemplo no debe copiar código obsoleto ni bajar a registros: `SpriteManager::emit_placements_into` solo se usa si la fachada aún no ofrece la publicación de la escena que necesita la demo, y esa fricción se reporta como brecha de API.

**Parámetros visuales iniciales:** 320×256 PAL, ocho canales HW, arte de 16×16 para el carrusel principal y altura/gap tomados de `sprite_limits.hpp`. La secuencia busca al menos cinco objetos distintos por cada canal durante cada ciclo vertical; el mismo canal debe cambiar claramente de objeto entre placements, sin reutilizar consecutivamente el mismo arte/color/firma. Se preparan actores attached de cuatro planos y actores libres de dos planos, con formas distintivas y siete tonos opacos en el perfil attached. Los perfiles 3/4/5/EHB cambian el playfield y su presupuesto, no la codificación de color intrínseca del objeto. El fondo usa bandas/rejilla de contraste moderado y no periódica, con la banda HUD de 1 bpp reservada.

**Descomposición de implementación:** (1) generar arte determinista en compilación (diamantes/gemas de cuatro planos y figuras libres de dos planos, con máscara por color índice cero); (2) reservar bitplanes, DATA y pool attached en memoria compatible con DMA; (3) dar de alta actores y mantener sus `ActorId`; (4) cocinar trayectorias/reusos con posiciones iniciales que cubran toda Y y respeten el gap; (5) componer con `SpriteScene` y registrar Sprite HW, degradados, placements por canal y errores; (6) construir/publicar la lista de Copper en doble buffer; (7) actualizar movimiento y animación cada tick y medir la fase de peor carga; (8) cerrar primero el caso mínimo y después añadir cruces, wrap y el máximo enjambre.

El esqueleto de `main.cpp` se organiza alrededor de `init`, `update` y `render`, con helpers `build_background`, `build_art`, `add_actors`, `compose` y `build_copper`. `init` configura memoria y display, cocina los recursos y marca `READY` solo después de una composición inicial válida; `update` avanza trayectorias, llama la emisión de la escena, instala la lista preparada y actualiza contadores; `render` usa el probe de ejecución. El estado privado contiene una escena de capacidad fija (propuesta inicial `SpriteScene<64>`), `ActorId` para los actores, `FramePlan`, `DoubleBuffer`, buffers tipados de bitplanes/sprites en Chip y ventanas de FPS/contadores. Los nombres/capacidades se ajustan a las firmas actuales del engine durante el aterrizaje; la intención del esqueleto no es fijar una API incompatible.

**Movimiento y coste:** las trayectorias se determinan en setup mediante tablas o pasos incrementales, con fases fijas por actor. No se implementa el ejemplo algebraico `y = base + (frame * speed + phase) % range` directamente en el bucle caliente: las reglas de coste 68000 exigen evitar `%` y multiplicación de 32 bits por frame sin medida. Se usan acumuladores, una tabla precomputada o una secuencia cocinada; el wrap se cubre como evento visible y verificable. El contador `max_reuse` se deriva del resultado real del allocator/emisión, no de la cantidad solicitada por el generador.

**Telemetría mínima en `g_eng_run_status.detail` y contadores del periférico:** estado/fase o código de fallo; actores solicitados y representados; placements Sprite HW; canales ocupados; reusos máximo y total por canal; pares attached; BOBs por estrategia; degradaciones; composiciones correctas/incorrectas; colisiones detectadas; `fps10`; y carga máxima observada de Blitter/Copper si el engine puede medirla. Antes de ampliar `detail`, se asigna un layout de bits/campos documentado y una forma de leer contadores completos; un valor empaquetado sin interpretación reproducible no satisface el gate.

**Secuencia inicial de cierre:** arrancar en A (3 planos) y C (5 planos), por ser casos con presupuestos contrastantes; después B y D. Capturar inicio, cada cruce/reuso representativo, máximo solapamiento, ambos wraps verticales, aparición tras wrap y final de fase. Aplicar FPS y flicker como gates independientes, conteo por color+forma con `check-elements`, frame-diff/motion-check para continuidad y la evaluación visual de inventario, seguimiento y contraste con INTENT. Cualquier desaparición de una sola fase detiene la aceptación y se reproduce primero en una sonda mínima del allocator/emisor implicado.

**Hallazgo R1 (medido, 2026-10):** la recomposición + reemisión por frame de `SpriteScene` con 30 actores costo ~670k ciclos/frame (≈5,0 campos), de los cuales ~580k eran composición. La misma escena de 40 placements con copperlist **estática** y parcheo de POS/CTL (PT al rotar el arte) mide **49,92 fps / 142.102 ciclos / 1,002 campos** en A y C. Conclusión: el planner debe ofrecer un camino incremental estable (asignación por actor que solo cambie cuando se rompan reusos) y la emisión debe parchear la estructura existente en vez de reconstruirla. La demo 224 es la referencia medida; la fachada aún no expone ese camino y el contrato está en `SPRITE_MATERIALIZATION_CONTRACT.md` §4.

**Lanzamiento una vez implementada la demo:** desde el entorno Git Bash configurado para el toolchain del proyecto, construir con cada perfil de planos, ejecutar y analizar en ese orden. La forma de seleccionar `K224_PLANES` debe integrarse al mecanismo de configuración que `build-demo.sh` ya admita; la línea `-DK224_PLANES=3|4|5|6` del borrador es un contrato de configuración pendiente de confirmar, no se asume que pasar un define arbitrario esté habilitado. Los comandos de validación de fps y flicker se registran en el `README.md` y `VALIDATION.md`, y deben usar la misma configuración que la secuencia visual.

**Salida del esqueleto:** el trabajo no se considera completado por crear el directorio o arrancar en `READY`. S1.1 queda verificada por perfil solo tras demostrar reuso real de los ocho canales, cambio de objetos por franja Y, attached/libres, presencia y continuidad, 50 Hz estables y cero candidatos de flicker. La integración con la API pública se evalúa registrando cuántas decisiones de bajo nivel tuvo que tomar la demo y por qué.

### Grupo 2 — BOB y carga de Blitter

**B2.1 — Enjambre transparente y solapado.** De doce a veinte BOBs de 16×16 o 32×16 con máscara, bajo órbitas, rebotes y entradas/salidas deterministas. Tres o cuatro cuerpos ocupan intencionadamente la misma región en fases concretas. Se recorren las políticas de fondo disponibles (cookie-cut, máscara interleaved y save-under/restore según proceda) sin cambiar el guion de posiciones. El oracle detecta basura de máscara, restos del fondo anterior, orden incorrecto y picos de carga.

**B2.2 — BOBs grandes y FastBob en Dual Playfield.** Seis a ocho objetos de 32×32 o 48×32 en el plano frontal, con solapamiento controlado que fuerza la degradación de FastBob a cookie-cut/clear cuando corresponda. La comparación de fases mantiene las mismas posiciones antes, durante y después de la degradación y verifica que la transición no desplaza ni parpadea.

### Grupo 3 — Escena mixta y degradación

**M3.1 — Jugador/actor attached, enemigos BOB y proyectiles Sprite HW.** Uno o dos actores grandes attached, ocho a doce enemigos BOB y una o dos cadenas de proyectiles Sprite HW. La ruta ejercita colisión hardware CLX donde la configuración lo soporta y colisión software/hitboxes en todos los perfiles; cada resultado de colisión tiene una marca visible temporal o un contador verificable. Se incluyen solapes entre las tres representaciones y orden de capas declarado.

**M3.2 — Saturación y degradación determinista Sprite HW → BOB.** El número de actores excede deliberadamente la capacidad de canales disponible en algunas franjas. El planner debe seleccionar de forma reproducible qué entidades conserva como Sprite HW y cuáles degrada, siguiendo prioridades declaradas. Se capturan frames inmediatamente antes, durante y después del cambio de representación; no se admite salto de posición, pérdida de transparencia, duplicación ni flicker. El resultado de la política se compara con telemetría para distinguir una decisión válida de un objeto sencillamente perdido.

### Grupo 4 — Movimiento y límites de pantalla

**E4.1 — Entrada/salida por los cuatro bordes a velocidad alta.** Actores con velocidades de 4–8 píxeles por frame atraviesan cada borde, parcialmente visibles durante la entrada y salida, y reaparecen según la regla de wrap declarada. Las formas incluyen máscaras asimétricas para hacer evidente un clipping incorrecto. Se inspeccionan restos en la pantalla, líneas de límites y continuidad temporal.

**E4.2 — Convergencia central y rebote sincronizado.** Todos los grupos convergen a una región central en el mismo frame y rebotan con un patrón determinista. Ese instante combina máximo solapamiento, composición, trabajo del Blitter y rearme de canales. El perfil de rendimiento debe conservar los datos del frame pico, no solo el promedio.

### Grupo 5 — EHB

**H5.1 — EHB, half-brite, transparencia y mezcla de representaciones.** Fondo y objetos de playfield emplean colores base y half-brite seleccionados para contrastar; Sprite HW y BOB mantienen sus propias claves transparentes. Incluye objetos que cruzan regiones de paleta y se solapan con ambos tipos de tono. Se verifica que el sexto bitplane y el modo de color no alteran los índices esperados, y que la gestión de la paleta compartida en cinco/seis planos no corrompe fondo ni sprites.

## Veredicto por test y perfil

El resultado se registra como `PASS`, `FAIL` o `SIN VERIFICAR` para cada combinación test×perfil. `PASS` requiere superar todos los gates; ningún promedio, captura aislada o pase de Ollama compensa un gate determinista fallido. La matriz de ejecución y las exclusiones se almacenan en el `VALIDATION.md` de cada demo.

| Gate | Evidencia | Umbral de PASS |
|---|---|---|
| Cadencia PAL | `measure-fps.mjs`, duración/campos por frame y perfil de la fase pico | ≥49,5 fps promedio, un field por frame y sin frames de juego que consuman dos VBlanks ni picos que rompan la cadencia. |
| Flicker, tearing y corrupción temporal | `temporal-detect.py`, `flicker-check.mjs`, baseline de regresión y revisión de la secuencia | Cero candidatos no resueltos; `max_candidates: 0` y `max_blocks_low: 0`. Un candidato confirmado o no explicado implica FAIL. |
| Presencia y forma | `check-elements.mjs`, contrato de píxeles/regiones y Ollama en los frames clave | Todas las entidades declaradas presentes, con forma, tamaño y color/firma correctos en cada fase aplicable. |
| Movimiento | diff/correlación/bbox deterministas y prompts B/C de visión | Trayectoria continua conforme al guion; ninguna aparición/desaparición inexplicada, teletransporte o cambio de forma. |
| Transparencia y restauración | máscaras/ROI, histograma y secuencia | Clave transparente no cubre el fondo; cero píxeles residuales o daño de máscara en todos los pasos. |
| Bordes y wrapping | ROIs de borde y frames antes/durante/después de cada cruce | Clipping/wrap según contrato, sin restos ni filas/columnas fuera de la silueta. |
| Solapamientos, capas y colisión | secuencia de estados esperados, asserts de colisión y visión C | Orden de capas estable; eventos y contadores de colisión concuerdan; sin z-fighting ni pérdida de entidades. |
| Degradación y telemetría | `g_eng_run_status.detail`/contadores y comparación de representación | Política determinista, conteos coherentes y transición sin cambio visual de posición ni flicker. |

El benchmark de FPS no sustituye el perfil del instante de carga máxima: se guardan ciclos y costes por fase/ruta, incluidos spawn, wrap, reordenación y degradación. Si el instrumento no puede observar una métrica aún, se registra SIN VERIFICAR y se propone su implementación como trabajo del engine.

## Protocolo de visión local y artefactos

La evaluación sigue el flujo de `DEMO_VISUAL_DEBUG.md` §6.4 y mantiene separados el interrogatorio abierto y la comprobación dirigida. Se analiza la secuencia en orden temporal, preferiblemente en ventanas deslizantes de 12–24 frames consecutivos, más imágenes individuales a resolución completa para detalles de máscara y bordes. Se revisa también la hoja de contacto manualmente; el VLM es un filtro de sospecha y no reemplaza conteos ni diffs objetivos.

**Prompt A — inventario sin contexto:** se usa literalmente el prompt A canónico de `DEMO_VISUAL_DEBUG.md` §6.4. No se anticipa al modelo el contenido esperado.

**Prompt B — seguimiento:** para cada entidad representativa (incluyendo al menos un actor attached, uno libre, un BOB y una entidad degradada cuando aplique), se utiliza el prompt B canónico de `DEMO_VISUAL_DEBUG.md` §6.4, indicando la trayectoria esperada y el comportamiento de wrap/colisión que se está evaluando.

**Prompt C — contraste con intención:** se utiliza el prompt C canónico de `DEMO_VISUAL_DEBUG.md` §6.4, sustituyendo intención y lista por las entidades/colores/formas exactas del `INTENT.md`. Se pregunta por cada transición, solapamiento y transparencia. Cualquier entidad ausente, a medias o incoherente mantiene el test abierto; si A contradice C, prevalece A hasta resolver la discrepancia con evidencia objetiva.

Cada demo conserva `src/INTENT.md`, `src/VISION.md`, `VALIDATION.md`, contrato de elementos y puntos de secuencia según las convenciones vigentes. `VISION.md` contiene los prompts/veredictos según `demo-docs`; la respuesta cruda, capturas y perfiles de una pasada van en las rutas de evidencia canónicas descritas por `DEMO_VISUAL_DEBUG.md` §6.5, no se sustituyen por un resumen inventado. El `VALIDATION.md` resume comandos, configuración completa, frames/fases, respuestas crudas relevantes, métricas y alcance probado.

## Integración, interfaz y orden de ejecución

La primera implementación es S1.1, como sonda de la ruta de escena y del allocator; se comienza con los perfiles A y C y se amplía a B y D después de pasar los gates parciales. Antes de reservar número o directorio, ejecutar `node tools/check/next-number.mjs demos/techniques/amiga/sprites` y revisar [NUMBERING.md](../../ai-dev-environment/NUMBERING.md). El test se coloca como demo de técnica solo si realmente examina una técnica específica; las pruebas puras de planner/allocator pertenecen a `tests/host/` y los casos de hardware sin demostración, a la taxonomía on-target vigente.

Orden de campaña:

1. **R0 — Contrato de observabilidad:** fijar identificadores visuales, fases, telemetría y aserciones por región. Confirmar que las herramientas existentes pueden extraer secuencias, contar elementos y analizar flicker; anotar huecos de tooling antes de usarlos como gates.
2. **R1 — S1.1:** probar reuso vertical y attached/libres en 3 y 5 planos; cerrar fallos en sonda mínima antes de modificar el caso combinado. Extender a cuatro perfiles con evidencia de secuencia.
3. **R2 — Casos Sprite HW:** S1.2 y S1.3 cubren solapamiento, bordes, cadenas/rearme y variación de objetos por cada canal.
4. **R3 — Carga BOB:** B2.1 y B2.2 separan errores de máscara/restauración de los conflictos FastBob/DPF.
5. **R4 — Integración de juego:** M3.1 y M3.2 ejercitan colisiones, composición mixta y degradación observable desde la API pública.
6. **R5 — Extremos y perfil de color:** E4.1/E4.2 y H5.1 cierran bordes, picos de carga y EHB. La batería completa solo se marca cerrada cuando todas las celdas aplicables son PASS.

Los tests host complementan las demos con estados sintéticos para allocator, ocupación por línea, pares attached, cadenas, selección de representación y degradación. No reemplazan el gate visual/on-target. Los ejemplos deben pedir entidades, materialización preferida y trayectorias desde la fachada; cualquier acceso manual a canales, estructura DMA o registros para forzar el resultado se considera una señal de API incompleta, salvo que sea precisamente el objeto de una prueba técnica de nivel bajo.

## Referencias

- [ROADMAP_JUEGO_SPRITES_BOBS.md](ROADMAP_JUEGO_SPRITES_BOBS.md): arquitectura de objetos y fases de juego F1–F8.
- [ROADMAP_SPRITES_COMPLETO.md](ROADMAP_SPRITES_COMPLETO.md): contratos y detalle técnico de cadenas, degradación, paleta, Blitter y pendientes.
- [ROADMAP_UNIFICADO.md](ROADMAP_UNIFICADO.md): estado y prioridades de sprites (fuente única de estado).
- [OBJECT_SYSTEM.md](../../engine/architecture/OBJECT_SYSTEM.md), [SPRITE_CHANNEL_WINDOWS.md](../../engine/architecture/SPRITE_CHANNEL_WINDOWS.md) y `docs/reference/amiga/techniques/`: contratos de objetos, canales y técnicas.
- [DEMO_VISUAL_DEBUG.md](../methodology/DEMO_VISUAL_DEBUG.md): gates de secuencia, Ollama, flicker y prompts canónicos.
- [docs/testing/README.md](../../testing/README.md) y [PROCEDIMIENTO_DEMOS_Y_JUEGOS.md](../methodology/PROCEDIMIENTO_DEMOS_Y_JUEGOS.md): pirámide de testing y gates F0–F5.
- [BUS_BUDGET.md](../../engine/architecture/BUS_BUDGET.md): presupuesto del bus para medir los límites de cada perfil.
