# L1-004: coste por vértice del pipeline 3D de CPU (`actors`)

Sonda de **coste** (estática, una sola pasada en `init`) del trabajo de CPU que la demo 088 agrupa en la sección `actors`, para decidir **antes de tocar el pipeline** dónde se va el tiempo:

1. `mesh_transform` (rotación + traslación de los 8 vértices del cubo).
2. `camera_contains_vertices` (test de frustum: 5 planos × 8 vértices).
3. `project_camera_vertex` (8 proyecciones).

Cada fase se repite 64 veces con rotaciones deterministas; el coste medio **por vértice y repetición** se publica en `g_eng_run_status.detail`:

| Bits | Valor |
|---|---|
| 0–9 | transform (ciclos/vértice / 4) |
| 10–19 | frustum (ciclos/vértice / 4) |
| 20–29 | proyección (ciclos/vértice / 4) |

Un checksum del trabajo realizado se escribe en `g_actors_checksum` (`volatile`) para que el compilador no elimine las fases medidas.

Es el **A/B** de referencia para cualquier cambio del pipeline (si se optimiza una fase, el mismo test debe bajar su campo sin cambiar el resultado). La demo 088 solo se usa como veredicto final.

## Ejecutar

```bash
bash tests/amiga/l1_backend/004_actors_cost/verify.sh
```

Salida esperada (los valores son orientativos; varían con el emulador):

```
[L1-004] OK: coste/vértice transform=... frustum=... proyeccion=... ciclos (detalle 0x...)
```
