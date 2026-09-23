# HOST-170: lenguaje y gestos (`eng::sim`)

Test host de `engine/include/eng/sim/communication.hpp`: comunicación explícita entre
criaturas, atada a **conducta, emoción y jerarquía**.

## Qué comprueba

1. **`signal_for_behavior`/`signal_intensity`**: la conducta y la emoción eligen el gesto
   (huir → alarma, cortejar → cortejo, someterse → sumisión) y su intensidad.
2. **`make_signal`**: el alcance depende del oído y de la **ecolocalización**.
3. **`receive_signals`**: quien está en alcance registra el tracker correspondiente (una
   alarma es una amenaza); fuera de alcance o en otra región, no.
4. **`apply_signal_effect`**: la alarma da miedo; el saludo acerca; recibir una **sumisión**
   eleva al receptor y lo sosiega (jerarquía).
5. **`SimWorld::broadcast_signals`**: entrega entre criaturas de la misma región.

## Salida de referencia

```
Sim communication:
OK: Sim communication (gestos, recepcion, emocion, difusion)
```

## Ejecutar

```bash
CXX="/c/.../mingw64/bin/g++.exe" bash tools/run-host-tests.sh tests/host/170_sim_communication
```
