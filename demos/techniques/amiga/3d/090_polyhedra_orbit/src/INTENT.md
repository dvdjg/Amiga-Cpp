# INTENT — 090_polyhedra_orbit (icosidodecaedro y cubo en órbita)

## Qué hace

Un **icosidodecaedro** de radio 90 (30 vértices, 60 aristas, 20 triángulos y 12 pentágonos) gira en el centro de la pantalla con tres componentes de Euler que derivan lentamente; sus ejes de rotación cambian despacio en el tiempo. Un **cubo** de medio lado 24 lo orbita en una elipse de radios 210×150 con un vaivén de profundidad de ±30, girando sobre sí mismo con otra deriva de ejes. La cámara es un pinhole de focal 256 centrado en (160,104) con near 64: los dos sólidos quedan siempre dentro del frustum y el cubo nunca cruza el plano cercano.

Las 32 caras del poliedro usan 7 tonos de una paleta de 16 (4 planos); los triángulos (20 caras) llevan un **damero 2×2** que mezcla su tono base con un compañero de paleta, y los 12 pentágonos quedan sólidos. El cubo usa seis tonos sólidos en caras adyacentes distintas. El HUD ocupa las filas 208–255 con el nombre, la versión, el contador de frames y los fps.

Flags de diagnóstico: `K090_CPU_FILL` (0 silueta XOR, 1 spans CPU, 2 cookie por cara, 3 máscara por plano), `K090_DITHER`, `K090_CULL2D`, `K090_FREEZE` y `K090_START_PHASE` para congelar la pose.

## Qué pretende demostrar

1. **Relleno de sólidos convexos con color por cara sobre OCS usando solo el Blitter**: por cada plano se dibuja la frontera de la unión de caras con ese bit (contorno `EOR`+`ONEDOT`), las aristas internas se cancelan por paridad y un **area fill `XOR`** reconstruye el índice de cada cara. El parche de color de cada arista es la XOR de las dos caras adyacentes visibles, o el color de la cara única en la silueta. La tabla de adyacencia se construye una vez; por frame no se recorta ni se deduplica por plano.
2. **Presupuesto real de la técnica**: medir cuánto cuesta cada parte (transform, culling, contorno, fill, patrón, clear) y dejar constancia de que **20 fps no son alcanzables** con dos sólidos y 7 tonos en un 68000 a 320×208. El A/B de 2 colores sin damero (16,3 fps) demuestra que el cuello son los ~80 blits de línea y el transform, no la variedad de color.
3. **Damero por plano con paridad**: el plano 3 selecciona solo los triángulos y se ditheriza después del fill (`D = A & B`); los planos 0–2 mantienen el color base, de modo que el borde de la silueta alterna entre dos entradas de la cara y nunca deja huecos de fondo.
4. **Aislamiento por sólido**: clear y area fill con una caja por sólido; las siluetas están garantizadas disjuntas por `orbit_separation_ok()`, así que no hace falta z-buffer ni cleave entre ellos.

## Elementos en pantalla

| Elemento | Color | Papel |
|---|---|---|
| Fondo | azul marino (0x013) | contraste con todas las caras |
| Pentágonos del poliedro | naranja, cian, amarillo, rojo (1/2/4/7) | caras sólidas; cada arista con un triángulo es visible |
| Triángulos del poliedro | pares 11↔3, 13↔5, 14↔6 | caras con damero 2×2; el patrón distingue triángulos vecinos en pantalla |
| Cubo | seis caras sólidas (1/2/4/7/3/5) | órbita elíptica; sus caras adyacentes nunca repiten tono |
| HUD (filas 208–255) | blanco (15) sobre el fondo | nombre + versión, `FRAME`, `FPS` |

## Evolución temporal

- El poliedro gira con velocidades base 2×/3×/1× de la fase y una oscilación lenta de cada eje (±11/7/5 en la tabla seno), de modo que nunca repite exactamente la misma orientación en el tramo visible.
- El cubo completa una vuelta de órbita cada ~340 frames y gira sobre sí mismo a 3×/5×/7×; su profundidad oscila ±30 unidades, así que su tamaño aparente cambia.
- La fase avanza 12 unidades por frame (tabla de 4096); `K090_FREEZE=1` la congela y `K090_START_PHASE` elige la pose inicial para comparar capturas.
- El HUD se repinta cada 32 frames (el campo de fps solo cambia una vez por segundo); el contador y los fps son la telemetría de cadencia.

## Criterio de éxito

1. Las 32 caras se ven con el color/patrón correcto, sin huecos de fondo ni bandas de paridad, y las caras adyacentes nunca comparten color.
2. Los contornos no dejan restos de posiciones anteriores ni puntos sueltos en la fila 0.
3. La cadencia se mantiene estable en 3 campos con las dos siluetas separadas; las medidas y la deuda quedan en `VALIDATION.md`.
