# HOST-205: el humano como personaje

Test host de `eng/sim/expression.hpp::expression_from_input`: la entrada del humano
(gestos explícitos + *timing*) produce las mismas fugas que un NPC, y la mesa las lee con el
mismo `ReadModel`.

## Qué comprueba

1. **Gestos explícitos**: los gestos pactados (A/B) producen fugas; sin *timing* no hay
   tell de tiempo.
2. **Timing implícito**: responder rápido = `instant_call`, tardar = `tank`, entrada
   errática = `fidget`; sin nada, no hay fugas.
3. **Lectura simétrica**: la mesa aprende el tell de *timing* del humano en el showdown
   (`p_strong` alto) y lo clasifica como legible.

## Salida de referencia

```
eng::sim input_expression:
OK: eng::sim input_expression (gestos explicitos, timing y lectura simetrica)
```

## Ejecutar

```bash
CXX="/c/.../g++.exe" bash tools/run-host-tests.sh tests/host/sim/205_sim_input_expression
```
