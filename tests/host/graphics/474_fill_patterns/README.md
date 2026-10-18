# HOST-474: patrones de relleno de polígonos (`eng::graphics::FillPattern`)

Test host del catálogo de **patrones de relleno** de `engine/include/eng/graphics/fill_pattern.hpp` (tablero 2×2 y 4×4, punteado, líneas verticales/horizontales y oblicuas 45°/135°) y de su aplicación en el relleno por bitplane (`fill_polygons_by_plane_cpu`, que con `Solid` es la referencia de siempre).

## Qué comprueba

1. **Palabras del catálogo** (`fill_pattern_word`): goldens por tipo (tablero con filas alternas `0xAAAA`/`0x5555` y `0xCCCC`/`0x3333`, punteado `0x8888`/0, verticales constantes, horizontales 1 de 4 filas) y **periodo/continuidad** de las oblicuas (rotación de 1 bit por fila, periodo 16).
2. **Plano de patrón** (`fill_pattern_plane_cpu`): cada fila es la palabra repetida a lo ancho (comparado por palabras `u16`, endian-agnóstico) y la altura se recorta a la capacidad del buffer.
3. **Composición por plano con patrón**: un rectángulo de color 3 (bits 0-1) se rellena en dos planos y el resultado se verifica **bit a bit** contra una referencia independiente por píxel (`dentro(x,y) && bit_del_patrón(x,y)`) para Checker2/Checker4/VLines4/HLines4/Oblique45/Dots2.
4. **Regresión de la firma sin patrón**: con `Solid` (defecto) el relleno reproduce exactamente el rectángulo, como la referencia anterior al parámetro.

## Salida de referencia

```
OK: patrones de relleno (checker, punteado, líneas v/h y oblicuas)
```

## Ejecutar

```bash
bash tools/run-host-tests.sh tests/host/graphics/474_fill_patterns
```

## Notas

- El catálogo genera palabras de 16 px (MSB = píxel izquierdo) con periodo horizontal de 16 px; es el formato que encaja con el Blitter del backend, que aplica el patrón con un blit `D = A & B` (`B = D`) por plano tras el area fill (mismo minterm/coste en la demo 080, que texturiza la sala con `Checker4`).
- El plano de patrón se construye una vez (Chip RAM) y se reutiliza cada frame; coste del blit de aplicación: 6 ciclos/palabra sobre la caja del fill (medido en la 080: ~50-80k ciclos por pasada completa con la caja de la sala).
