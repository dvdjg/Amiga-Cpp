# HOST-376: evaluación por ventanas

Valida el contrato host-only de entrenamiento por streaming: una fuente entrega ventanas reutilizables, el encoder se evalúa sin cargar el audio completo y el presupuesto predeterminado es de 6 GiB.

```bash
bash tools/run-host-tests.sh tests/host/audio/376_stream_window
```
