# HOST-207: expression templates lite

Test host de `engine/include/eng/core/expr.hpp` (`eng::math::et`): el árbol de expresión se
construye en compilación y se evalúa **una sola vez** al convertir al tipo final, sin
temporales por operador.

## Qué comprueba

1. **Escalar nativo `constexpr`**: `a + b*c - d` se pliega en tiempo de compilación
   (`static_assert`) y respeta la precedencia; la mezcla `expr + crudo` funciona por los dos
   lados.
2. **`MiniFloat16`**: la expresión produce el mismo resultado que la evaluación directa.
3. **`Fixed`**: la suma en el mismo exponente es directa; el producto sube a 4.24 y
   `eval<Fixed<s16,12>>` lo **reescala y moldea** vía `converter<Fixed>`; `a + b*c` compila
   porque `et_add` **promueve** el término a 4.24 y normaliza una sola vez (acumulador ancho);
   al destino con otro exponente (8.8) reescala, no reinterpreta.
4. **`Vec`/`Mat` fusionados**: `eval_into(dst, a + b*2 - c)` recorre un solo bucle y escribe
   por componente; difundir un escalar llena el contenedor; `assign_to` equivale a
   `eval_into`.
5. **`Vec<Fixed>` fusionado**: el producto por componente sube a 4.24, se puede sumar un
   término (promoción exacta) y `et_set` reescala al escalar del vector destino dentro del
   mismo bucle.

## Límite documentado

El acumulador ancho cubre `a + b*c`, pero no productos anidados (`(a*b)*c`), que pedirían
64 bits. La comparativa de `.s` (`tools/analyze/expr-asm-compare.mjs`) muestra que el árbol
gana en escalares `MiniFloat16` (menos pila) y no compensa en `Vec<3>`/`Mat<2>`; ver
`docs/engine/architecture/EXPRESSION_TEMPLATES.md` §4–5.

## Salida de referencia

```
eng::math::et expression templates:
OK: expression templates lite (escalar constexpr, Fixed, MF16, Vec/Mat fusionados)
```

## Ejecutar

```bash
CXX="/c/.../g++.exe" bash tools/run-host-tests.sh tests/host/207_math_expr
```
