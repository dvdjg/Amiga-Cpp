# HOST-449 — texto por palabras (`Surface::draw_text_words`)

Test host de la impresión de texto por **palabras de 16 px** de las franjas de HUD: dos glifos de `Font8` por palabra, ocho escrituras por par (patrón y fondo de golpe), sin bucle por píxel ni por bit.

- `Playfield::store_plane_word(wx, wy, bits, color)`: escribe la palabra de patrón **en todos los planos** — los planos seleccionados por `color` reciben `bits`, el resto fondo 0 — con una escritura por plano. Genérico en planos y color; con 1 plano y `color=1` es la franja de depuración más sencilla (una única escritura).
- `Playfield::store_plane_word_rows(wx, wy, bits_by_row, rows, color)`: la racha de filas por palabra (texto por palabras sin pagar la vía virtual por fila): dirección calculada una vez, ruta rápida con el paso entre filas derivado de `planeline_for` y **verificado** (contiguo/interleaved) y fallback por fila para layouts de anillo; el test comprueba equivalencia byte a byte con 8 llamadas por fila, el contrato de fallo (filas previas escritas) y el camino de anillo.
- `Playfield::store_plane_words_row(wx, wy, words, count, rows, color)`: la racha **horizontal** (un trozo de línea) — mapeo del playfield resuelto una vez, equivalencia byte a byte con las llamadas por palabra y con las rachas por fila, y guarda que impide rebasar la fila.
- `Surface::draw_text_words(x, y, text, color)`: compone la línea (celda impar final rellenada con espacio, truncado al clip, requisito `x` múltiplo de 16) y la escribe por palabras con el color pedido.
- `eng::playfield::DiffText<MaxChars>` (`eng/field/diff_text.hpp`): fila de HUD **por diferencias** sobre `draw_text_words` — guarda la última cadena pintada y repinta solo las celdas cuyo par de glifos cambió (típicamente 1-2 palabras de un contador frente a la línea completa; es la palanca del presupuesto de la demo `001_adv_walk`). Cuidado de composición: su primera pintura escribe **todas** sus celdas, así que lo que comparta fila a su derecha debe pintarse después (aviso en la cabecera).

El test compara la línea impresa contra un **oráculo independiente píxel a píxel** (bit a bit de `Font8::row`, `bit 0 = columna izquierda`) en **todos los planos**: franja de 1 plano con color 1, lienzo de 4 planos con color 5 (el patrón llega solo a los planos del color), color 0 (nada), cadena vacía, truncado por clip, rechazos de geometría y escrituras directas de palabra. `DiffText` se verifica por **diferencia de buffers**: primera pintura == línea completa, texto idéntico == cero escrituras, cambio de una palabra == solo los bytes de esa celda (y == repintado completo del texto nuevo), acortar == relleno con espacio, fallo de geometría sin tocar `prev` (reintento) y `reset`.

```bash
CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/graphics/449_word_text
```
