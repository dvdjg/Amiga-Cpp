# 116 - FLATSHADE CONVEX: intención detallada

Documento de **intención** de la demo (gate `tools/check/demo-docs.mjs`): qué hace, qué pretende demostrar, qué hay en pantalla y cómo evoluciona en el tiempo. La evaluación del modelo de visión vive en `VISION.md` (prompt canónico, sin pistas).

## Qué hace

Objeto **convexo** `pilka` (malla `obj2c`, 60 vértices / 90 aristas / 32 caras) girando con **sombreado plano**, importado de `demoscene-repo-orig/effects/flatshade-convex`: por frame se calcula la luz de cada cara (producto escalar normal·(cámara−p0) con la tabla `kInvSqrt`, sin `sqrt`), la visibilidad de aristas del sólido convexo (**XOR** de la luz de las caras adyacentes, que cancela las aristas internas y deja silueta + aristas de contraste), la transformación y proyección de vértices (4.12 empaquetado, `muls.w`/`divs.w`) y el dibujo: **contorno EOR `ONEDOT`** por Blitter (`flat_shade_xor`: preludio común una vez + Bresenham por arista, replicado en cada plano del color) y **un area fill XOR** (paridad de scanline, barrido descendente) que rellena el interior según la técnica del original. El display es una ventana 256×256×4 con los registros del original sobre un **triple buffer** (`scene::compose`): se dibuja en el buffer trasero, se publica en el swap del frame siguiente y se pre-limpia el siguiente, de modo que el fill queda solapado con el transform del frame próximo.

## Qué pretende demostrar

1. La técnica **contorno + area fill XOR** como vía barata de polígono relleno en Amiga (1 fill por frame, no por cara) — frágil por paridad, con gate visual (`freeze-diff`), no de cobertura.
2. La **pipeline multi-buffer con Copper** del engine (`scene::compose` sobre `Scene`): triple buffer, parcheo de `BPLxPT` en `commit()` y solape fill/CPU.
3. El **sombreado plano calculado en CPU** con matemáticas 4.12 sin libcalls (`eng::lib3d`: visibilidad con luz, visibilidad convexa por XOR, transform del proyecto empaquetado; HOST-014/047) — la demo es el tutorial del importe fiel del effect original.
4. El **HUD de depuración de coste cero**: zona aparte de 1 bpp activada por Copper y actualizada solo al cambio (texto estático en init; contador ~1 vez/segundo).

## Elementos en pantalla

- **Balón (dodecaedro/icosaedro truncado) con caras sólidas** de tonos azules: cada cara visible con su tono de luz (0..15, paleta `flatshade_colors`), la cara más clara girada hacia el observador. El contorno EOR delimita silueta y aristas de contraste.
- **Franja de depuración** en la parte baja de la ventana (filas de ventana ≈216..224): texto blanco `FLATSHADE v2.0.<build>  F <frames>` sobre fondo oscuro (plano de 1 bpp, paleta de banda 16..31).
- Fondo oscuro uniforme (`#001122` del original).

## Evolución temporal

- La rotación avanza **8 unidades de vuelta (0..4095) por frame** (`m_angle + 8`): giro continuo; las caras cambian de tono y la cara más iluminada va recorriendo el sólido.
- Con la pipeline de 3 buffers el frame mostrado va **un paso por detrás** del calculado (latencia de 1 frame, como el original).
- Con `-DFLATSHADE_FREEZE_ANGLE=N` el ángulo se congela (capturas deterministas; dos builds con el mismo ángulo dan el mismo PNG byte a byte, gate `freeze-diff`).
- Cadencia medida (`A500_debug`, 2026-10): **19,7 fps** (2,54 campos/frame, ~360k ciclos/update). Sigue por debajo del suelo de 25 fps del efecto de relleno de polígonos: la deuda y el plan (referee async) están en DT-026 de `ROADMAP_DEUDA_TECNICA.md`.
