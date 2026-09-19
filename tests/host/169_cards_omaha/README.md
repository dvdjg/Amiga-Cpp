# HOST-169: Omaha de extremo a extremo

Test host de Omaha sobre el motor completo: equity de 4 cartas privadas y sesión CPU vs CPU.

## Qué comprueba

1. `equity_vs_random_omaha` (2 de 4 privadas + 3 de 5 comunitarias): AAxx domina a una mano
   basura y el resultado es determinista por semilla.
2. `run_session` con `SessionConfig::variant = PokerVariant::Omaha`: se juegan las manos, se
   conservan las fichas (suma de net = 0), hay actividad y es determinista por semilla.
3. Omaha con `with_jokers = true`: la sesión conserva las fichas.

## Salida de referencia

```
eng::cards omaha:
OK: eng::cards omaha (equity 4 cartas, sesion y comodines)
```

## Ejecutar

```bash
CXX="/c/.../g++.exe" bash tools/run-host-tests.sh tests/host/169_cards_omaha
```
