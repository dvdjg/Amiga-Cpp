# HOST-495: contrato del planner

Dibujo y audio comparten `eng::IntentQueue` (`Ticket`, no bloqueo hasta `wait`, `Done` por peticion) y mantienen planes separados (`FramePlan`/`AudioPlan`).

```bash
bash tools/run-host-tests.sh tests/host/graphics/495_intent_plan_contract
```
