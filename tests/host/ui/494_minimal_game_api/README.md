# HOST-494: juego minimo por la fachada

Contrato de compilacion del juego de alto nivel: `Screen`, `input` y `present` con `eng::App`, sin `FramePlan`, planos, memoria ni backend. El backend del test solo instancia el tipo; no se construye.

```bash
bash tools/run-host-tests.sh tests/host/ui/494_minimal_game_api
```
