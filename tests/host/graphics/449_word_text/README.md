# HOST-449 — texto por palabras (`Surface::draw_text_words`)

Test host de la impresión de texto por **palabras de 16 px** de las franjas de HUD: dos
glifos de `Font8` por palabra, ocho escrituras por par (patrón y fondo de golpe), sin
bucle por píxel ni por bit.

- `Playfield::store_plane_word(wx, wy, bits, color)`: escribe la palabra de patrón **en
  todos los planos** — los planos seleccionados por `color` reciben `bits`, el resto fondo
  0 — con una escritura por plano. Genérico en planos y color; con 1 plano y `color=1` es
  la franja de depuración más sencilla (una única escritura).
- `Playfield::store_plane_word_rows(wx, wy, bits_by_row, rows, color)`: la racha de filas por palabra (texto por palabras sin pagar la vía virtual por fila): dirección calculada una vez, ruta rápida con el paso entre filas derivado de `planeline_for` y **verificado** (contiguo/interleaved) y fallback por fila para layouts con anillo que envuelvan dentro de la racha; el test comprueba equivalencia byte a byte con 8 llamadas por fila, el contrato de fallo (filas previas escritas) y el camino de anillo.
- `Surface::draw_text_words(x, y, text, color)`: compone la línea (celda impar final
  rellenada con espacio, truncado al clip, requisito `x` múltiplo de 16) y la escribe por
  palabras con el color pedido.

El test compara la línea impresa contra un **oráculo independiente píxel a píxel** (bit a
bit de `Font8::row`, `bit 0 = columna izquierda`) en **todos los planos**: franja de 1
plano con color 1, lienzo de 4 planos con color 5 (el patrón llega solo a los planos del
color), color 0 (nada), cadena vacía, truncado por clip, rechazos de geometría y
escrituras directas de palabra.

```bash
CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/graphics/449_word_text
```
