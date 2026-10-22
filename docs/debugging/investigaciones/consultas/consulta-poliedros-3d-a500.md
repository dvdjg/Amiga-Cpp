# Consulta: poliedros 3D con caras planas en un A500 a 20+ fps

Necesito consejo externo sobre el enfoque de render y su rendimiento. Abajo está todo el contexto autocontenido: hardware, escena, algoritmo exacto y costes medidos. **Preguntar a Grok en inglés** con `consulta-poliedros-3d-a500-en.md`; este documento es el registro local.

## 1. Objetivo y hardware

- Amiga 500, OCS (Agnus/Denise), PAL, 68000 a 7,09 MHz, 512 KiB de Chip RAM, sin Fast RAM.
- Pantalla: 320×256, lowres, **5 planos** (32 colores), doble buffer (parcheando `BPLxPT`; copperlist construida una vez). Filas 0..207 = viewport 3D; filas 208..255 = banda HUD de 1 plano.
- Un campo PAL son ~141.876 ciclos de CPU. El DMA de planos a 320 px gasta 5×20=100 words/línea de ~227 slots de bus.
- Objetivo: **20 fps o más** (el suelo del repo para demos de relleno poligonal es 25 fps, así que 20 ya es una relajación). Resultado actual: **~4 fps**.

## 2. Escena

- Sólido central: **icosidodecaedro** (30 vértices, 60 aristas, 32 caras: 20 triángulos + 12 pentágonos), radio 90, girando sobre su centro con tres ángulos de Euler que derivan lentamente (los ejes de rotación oscilan un poco con el tiempo).
- Un **cubo** (medio lado 24, 8 vértices, 6 caras) orbita al sólido central en una elipse (radios 210×150 en z=520±30) mientras gira sobre sí mismo, también con ejes que derivan.
- Cámara: pinhole, focal 256, centro (160,104), near 64; objetos en z≈430..610, siempre dentro del frustum y sin cruzar el plano cercano.
- La órbita está elegida para que las **siluetas proyectadas nunca se solapen** (verificado numéricamente al arrancar); eso permite painter por sólido sin z-buffer y sin limpiar siluetas.
- Caras: 30 colores distintos para las 32 caras del poliedro (dos triángulos no adyacentes comparten color), el cubo reutiliza seis de esos tonos en caras distintas. Todas las caras adyacentes tienen color distinto.

## 3. Algoritmo exacto por frame (actual)

1. `scene.commit()` publica el back buffer anterior parcheando los 5 pares `BPLxPT` al principio del campo; se dibuja en el otro buffer (sin tearing).
2. Transformar los 30 vértices del poliedro con una matriz afín entera (tabla de senos de 12 bits, `Rx·Ry·Rz`, traslación +z=520) y los 8 del cubo igual (+ centro de la órbita).
3. Proyectar cada vértice **una sola vez por sólido** con fixed-point entero: `sx = 160 + x*256/z`, `sy = 104 + y*256/z` (s16 con signo, `divs.w`).
4. Culling de caras traseras con la normal 3D: `(b-a)×(c-a) · (0-a) > 0`, productos en s32 (`muls.w`). Los pentágonos usan sus tres primeros vértices. Sin clipping por cara.
5. Comprobaciones de bbox/área en pantalla: se descartan caras degeneradas y slivers <2 px.
6. Orden painter: orden del array de caras (las caras visibles de un sólido convexo no se solapan, y los dos sólidos no se solapan en pantalla).
7. Relleno. Se implementaron y midieron tres variantes:
   - **A. Blitter, por plano, contorno + área** (por defecto): por cada uno de los 5 planos, limpiar un rectángulo sucio (unión de las bbox de los objetos en el frame actual y los dos anteriores), dibujar las aristas de toda cara visible cuyo color tenga ese bit con el Blitter en **modo línea ONEDOT con minterm EOR**, y luego un **area fill FILL_XOR** sobre la bbox de las caras de ese plano.
   - **B. Spans de CPU**: por cara, `convex_spans` (DDA por scanline) escribe spans planares con `draw_span` en los 5 planos (overwrite opaco).
   - **C/D. Máscara + cookie-cut del Blitter**: construir una máscara de 1 bit por cara (spans de CPU) o por plano (unión de caras con ese bit) y copiarla a los planos de color con blits cookie-cut `BLTCON` A→(set) / ¬A→(clear).

La parte de geometría (1–6) está verificada: en dos poses congeladas la imagen renderizada coincide con una referencia 2D independiente (misma tabla/proyección/culling/painter) en el 98,8 % de los píxeles, con diferencias solo de 1–2 px en los bordes; no se encontró ni un píxel de fondo interior en 12 frames animados. La lentitud es puro coste de relleno.

## 4. Costes medidos (contador de ciclos del emulador, 7,09 MHz; 1 campo = 141.876)

Secciones del perfil (ciclos/frame, 5 planos):

| Sección | Ciclos/frame | Notas |
|---|---|---|
| Frame (update completo) | 1,25–1,55 M | ≈ 8,8–10,9 campos |
| Proyección + culling | ~148–155 k | 38 proyecciones + 38 culls |
| Relleno (A: por plano) | ~1,02–1,05 M | de los cuales **trazado de líneas ~891 k** |
| — limpiar rect sucio | ~40 k | 5 planos |
| — líneas de contorno | ~891 k | ~160–190 ops de línea ONEDOT EOR/frame ⇒ **~5 k ciclos por línea** |
| — area fill | ~50 k | 5 planos, FILL_XOR |
| HUD | ~23 k | repintado cada 8 frames |

Otras variantes: B (spans de CPU) relleno ≈ 2,35 M; C (máscara+cookie por cara) ≈ 1,04 M; D (máscara por plano + cookie) ≈ 1,0 M. Todas dan 2,7–4,1 fps en el contador de fps (cadencia de 9–16 campos).

## 5. Hechos del camino de relleno que no cuadran

- El Blitter **sí** hace relleno de polígonos (modo línea + area fill) y la fusión de contornos por paridad funciona, pero el coste lo domina el **número de operaciones de línea**, no los píxeles: ~170 líneas/frame a ~5 k ciclos cada una.
- Cada op de línea reprograma `BLTCON0/1`, `BLTBMOD/AMOD`, `BLTAPT`, `BLTCPT`, `BLTDPT`, `BLTSIZE` (varias escrituras a registros custom a ~57 ciclos cada una con `cpu_cycle_exact`) y espera a que acabe la línea anterior (el modo línea muta BLTDPT, así que hay que reescribirlo por línea).
- Con 30 colores distintos y 5 planos, una arista interna entre dos caras visibles se dibuja en cada plano donde difieren los bits de los dos colores (≈2–3 planos por arista), lo que multiplica las ~70 aristas visibles en ~170 ops de línea.
- El area fill en sí es barato (~50 k para todos los planos). El muro es el trazado.

## 6. Preguntas

1. **¿Son plausibles ~5 k ciclos por op de línea ONEDOT/EOR en OCS con DMA de 5 planos, o es señal de una ineficiencia corregible?** ¿Cuál es el coste teórico de una línea de 40–90 px en modo línea reprogramando registros por línea, y qué se puede legítimamente hoistear o batchear sin corromper el truco de paridad?
2. **¿Cuál es la arquitectura correcta en un A500 para muchas caras convexas planas de colores distintos (30–40 visibles, 320×208, 5 o 4 planos) a 20–25 fps?** Comparar opciones concretas con coste esperado: contorno por plano + area fill XOR (actual); máscara de 1 bit por cara + cookie-cut (variante C actual); relleno de CPU por scanlines en un buffer chunky + C2P (el engine tiene C2P 4bpp); tablas de spans/aristas precalculadas por orientación de cara o por frame; 4 planos con 16 colores sólidos más combinaciones de dither 2×2 / 4×4 para colores aparentes extra; cualquier otra cosa apropiada a OCS (trucos del modo línea del Blitter, sellos de arista pre-shifteados, Copper para cambiar colores por scanline u objeto, etc.).
3. **¿Cómo combinar dithering con colores por cara de forma eficiente?** Quiero mantener 4 planos (16 entradas) y usar patrones de tablero/2×2 entre dos entradas para obtener más colores de cara distinguibles. Con el area fill del Blitter esto parece exigir un relleno con patrón por cara (dos máscaras o un patrón aplicado por plano). ¿Cuál es la forma más barata y correcta de rellenar un polígono convexo con un dither de 2 colores en 4 planos, y cuánto cuesta frente a un color sólido?
4. **¿Deberíamos reducir el número de colores de cara?** Para detectar defectos queríamos un color por cara; ¿qué coloreado/presupuesto recomendáis para 32 caras en 4–5 planos si queremos 20 fps, y es un 3-coloreado más dithering un compromiso razonable?
5. **¿Hay referencias de la demoscene A500 para poliedros planos rápidos** (p. ej. flatshade-convex, lib3d, estilo 3D Construction Kit) cuya estrategia de relleno debiéramos copiar? Nombrar la técnica concreta (registros, orden, batching), no consejos genéricos.

## 7. Restricciones

- Solo 68000 (sin instrucciones 68020, sin FPU), no hace falta código automodificable, sin RAM extra fuera de Chip.
- El engine ya tiene: parcheo de copperlist, una rutina de línea/area fill del Blitter (`fill_polygons_by_plane_patterns`), un `blitter_fill_polygon` por cara (spans de máscara + cookie-cut por plano), un relleno de CPU `convex_spans` y una primitiva C2P 4bpp.
- Preferimos cambios reutilizables de engine antes que asm específico de demo, pero una primitiva asm de engine es aceptable si es la respuesta correcta.
