# HOST-206: introspección simulada y ritmo del rival

Test host de `eng/sim/introspection.hpp` (afecto derivado de los hechos del propio motor)
y del puente `eng/board/persona.hpp` hacia la búsqueda de tablero. Cubre también
`gestures_for_pace` de `eng/sim/expression.hpp`: el canal por el que un motor que **espera**
al humano muestra impaciencia y hartazgo.

## Qué comprueba

1. **Confianza y duda**: un margen grande entre la mejor y la segunda jugada da confianza y
   poca duda; un margen mínimo invierte el resultado; una jugada forzada (una sola
   disponible) da seguridad total.
2. **Libro y presión**: consultar el libro sube la confianza y marca conocimiento; poco
   tiempo y desventaja producen presión.
3. **Sorpresa y alerta** (tras la jugada del rival): un error del rival (la posición mejora
   respecto a lo esperado) da alerta; empeorar da sorpresa.
4. **Satisfacción y afecto**: una jugada buena y clara da satisfacción y sube la alegría;
   la presión da miedo y tensión a la psique.
5. **Puente con `eng::board`**: `facts_from_search` fija mejor/segunda línea y
   `facts_after_opponent` convierte el salto de evaluación en alerta.
6. **Ritmo**: una espera corta no genera gestos; a partir de un umbral aparecen impaciencia
   (pie/dedo), resoplido, y con esperas largas bostezo y desplome.

## Salida de referencia

```
eng::sim introspection:
OK: eng::sim introspection (confianza, duda, presion, sorpresa, ritmo y puente con board)
```

## Ejecutar

```bash
CXX="/c/.../g++.exe" bash tools/run-host-tests.sh tests/host/206_sim_introspection
```
