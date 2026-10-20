# 086 — Intención de Palette Checkerboard

## Qué hace

Construye un tablero indexado estático y lo muestra como cuatro bandas de resolución de color creciente. En cada banda, un panel angosto recorre los colores ordenados por luminancia y una matriz N×N muestra todas las parejas ordenadas de índices como checkerboard de 2×2 píxeles. La franja EHB usa la paleta de 32 bases más los 32 half-bright derivados por el sexto bitplane. El Copper conmuta la profundidad y los punteros/paletas de cada segmento; el engine compila la lista y mantiene el bucle de juego.

## Qué pretende demostrar

1. Que una única pantalla puede mezclar 3, 4, 5 y 6 bitplanes mediante cambios de raster del Copper, y que cada tramo conserva su geometría, paleta e imagen propias.
2. Que cada índice de color y cada pareja ordenada A/B se puede inspeccionar en el propio display; el checker es A B / B A y la diagonal contiene las celdas sólidas A/A.
3. Que ordenar ambos ejes por luminancia hace visible la diferencia de brillo: parejas próximas a la diagonal tienden a formar tonos intermedios estables; las parejas alejadas conservan un contraste alternante fuerte. Esta guía es de luminancia, no garantiza que cualquier pareja de cromas se perciba como mezcla natural.
4. Que EHB muestra 64 índices efectivos con 32 registros físicos, porque el sexto bitplane duplica cada base a media intensidad.
5. Que una paleta y los bitmaps de cada franja se preparan fuera del bucle de frame; solo se actualiza el contador hexadecimal del HUD, por palabras y sobre el bitplane 0.

## Elementos en pantalla

- Cuatro franjas horizontales: 3 BPL / 8 colores / matriz 8×8; 4 BPL / 16 / 16×16; 5 BPL / 32 / 32×32; y EHB 6 BPL / 64 / 64×64.
- En cada franja, una columna de muestras RGB444 y una matriz de checkerboards de 2×2. Cada celda alterna los índices de la fila y columna; la matriz completa representa los N² pares ordenados y los triángulos simétricos intercambian la fase del patrón.
- Un panel de texto al lado de cada matriz identifica profundidad y dimensión. `DIAG: SIM LIGHT` señala parejas de brillo próximo; `FAR: BRIGHT GAP` señala contrastes grandes. En EHB se muestran el nombre `COLOR PAIRS`, la versión, el tamaño de la matriz y el contador hexadecimal de frame.
- Negro y blanco están presentes en todas las paletas; COLOR01 es blanco para el texto y el índice cero es negro.

## Evolución temporal

- Durante cada barrido, el Copper conmuta de 3 a 4, luego 5 y finalmente 6 bitplanes en los límites verticales de las franjas. Las matrices y las paletas no cambian entre frames; esa estabilidad permite comparar las cuatro configuraciones a la vez.
- El contador `F=XXXXXXXX` avanza en hexadecimal cada ocho frames dentro del panel EHB y se escribe tras la sincronización del frame, solo en COLOR01/bitplane 0. No se anima ni se repinta la matriz.
