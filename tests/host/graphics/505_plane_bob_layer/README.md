# HOST-505: capa de BOBs de un plano

Valida `eng::scene::PlaneBobLayer`:

- movimiento dentro del padding: **un solo draw**;
- salto mayor que el padding: **clear de la huella previa + draw**;
- el clear/draw caen en el **plano** del actor y usan `BLTDMOD=196`;
- `group_base` marca ambos jobs como el **mismo grupo atómico**;
- `reset_history` no deja borrados ni draws fantasma.

```bash
bash tools/run-host-tests.sh tests/host/graphics/505_plane_bob_layer
```
