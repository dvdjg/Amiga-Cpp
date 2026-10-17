# 079 - WIREFRAME: intención detallada

Documento de **intención** de la demo (gate `tools/check/demo-docs.mjs`): qué hace, qué pretende demostrar, qué hay en pantalla y cómo evoluciona en el tiempo. La evaluación del modelo de visión vive en `VISION.md` (prompt canónico, sin pistas).

## Qué hace

Dibuja el objeto `pilka` (malla `obj2c` de 60 vértices / 90 aristas / 32 caras, un sólido tipo balón de fútbol) en **alambre**, con las líneas trazadas por el **Blitter en modo `LINE`** (`BC0F_LINE_OR`), como el original del demoscene (`wireframe.c`): por frame se calculan visibilidad de caras (producto escalar normal·(cámara−p0) sin luz), se marcan las aristas de las caras visibles, se transforman y proyectan los vértices marcados (matriz 4.12, empaquetada `(m0+y)·(m1+x)+m2·z−x·y` con `muls.w`/`divs.w` nativos) y se trazan las aristas visibles con el preludio del modo línea fijo una vez y el Bresenham preparado por arista (`eng::retro::wire_lines` → `blitter_line_prepare`/`blitter_line_draw`). El display es una ventana 256×256×4 en los registros del original (`SetupPlayfield(MODE_LORES, 4, X(32), Y(0), …)` + `SetupBitplaneFetch`) sobre un **anillo de 5 planos**: cada frame se dibuja solo en el plano activo (pre-limpiado en el frame anterior, oculto entonces) y la copperlist rota los punteros para que el bit 3 sea el recién dibujado y los bits 2..0 el rastro de los 3 frames anteriores (colores 8/4/2/1).

## Qué pretende demostrar

1. El **trazado de alambre por Blitter** como técnica de vector graphics del engine (`eng::retro::wire_lines`, hermana sin relleno de `flat_shade_xor`): preludio estático + una espera por línea, sin `FramePlan` por arista (el camino de plan pagaba O(N²) en el informe de presupuesto y `memset`/`memcpy` por línea).
2. La **pipeline de anillo con Copper**: N buffers de plano sin tearing, pre-clear solapado del plano oculto (el clear deja de estar en el camino crítico) y rastro por persistencia de planos.
3. El **bucle de frames por IRQ** (`run_frames` + `Wait()`): sin sondeo de VPOSR y sin servicio de Blitter para demos que no usan la cola de fondo.
4. El **HUD de depuración de coste cero** (norma del repo): nombre + versión + contador en una **zona aparte** con un 5º plano de 1 bpp activado por Copper, actualizado solo cuando cambia texto (estático en init; contador ~1 vez/segundo).

## Elementos en pantalla

- **Balón de fútbol en alambre**, líneas azules (paleta `wireframe_colors`, 16 tonos de azul), centrado y ocupando ~2/3 de la ventana; los 4 planos visibles forman el rastro cromático (color 8 = frame actual, 7/3/1 = anteriores).
- **Franja de depuración** en la banda superior de la ventana (filas ≈16..23, libre del balón): texto blanco `WIREFRAME v2.0.<build>  F <frames>` sobre fondo oscuro, pintada en un **5º plano de 1 bpp activo todo el frame** (sin zonas de Copper; el fetch extra ~0,11 campos) con paleta espejo 16..31 para no alterar ningún color de la escena.
- Fondo azul oscuro uniforme (COLOR00 del original, `0x012`).

## Evolución temporal

- La rotación avanza **8 unidades de vuelta (0..4095) por frame** en los tres ejes (como el original, `frameCount * 8`): giro continuo perceptible en la secuencia (la visibilidad de caras y aristas hace que el alambre "ruede").
- El contador del HUD se incrementa 1 por frame y se **repinta 1 vez/segundo** (campo de ancho fijo, 24 escrituras de palabra); no hay otros cambios de contenido.
- Con `-DWIREFRAME_FREEZE_ANGLE=N` la rotación se congela en `N` vueltas (capturas deterministas para A/B pixel a pixel).
- Cadencia medida (`A500_debug`, 2026-10): **33,1 fps** (1,51 campos/frame, ~214k ciclos/update); se ejecuta un update por frame de juego sin tirones de dos campos.
