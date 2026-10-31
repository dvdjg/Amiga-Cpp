# Plan de fusión: `fix/render-207` → `master`

## Estado

**Fusión completada el 2026-10-31.** `master` avanzó por fast-forward a `1ec72ec4`, que incluye el merge `6d4672c5` y toda la rama `fix/render-207`.

El worktree master tenía cambios locales sin commit de 207/208, docs de tiles y herramientas. Se preservaron en la rama `wip/master-render-208-20261031` (`e3dc1f82`) antes del fast-forward; **no se fusionaron automáticamente**.

## Qué se integró

- Se conserva la rama: `PlaneBobLayer`, `bob_erase_plane_box`, scheduler de grupos beam-aware, 210 con 6 BOBs, 211 con `clear + draw` y la política de 207 con una capa protagonista por parada.
- De master se conserva:
  - `bob_beam_window` no bloqueante y `wait_bob_beam` como API opt-in;
  - cajas de pantalla de animaciones de tiles (`set_display_offset`);
  - poda de capas desactivadas en `rebuild_anim_visible`;
  - `AGENTS.md` y `DOC-MAP-PRINCIPAL.md`.
- No se activa la guarda global bloqueante en 207: master medía 44,09 → 42,46 fps con ella. La ficha de master quedó sustituida por la de la rama, con nota de fusión.

## Validación del árbol fusionado

| Demo | Resultado |
|---|---|
| 207 visita completa | **49,67 fps**, 142.808 ciclos, 1,007 campos |
| 207 parada de fogata | **49,92 fps**, 142.102 ciclos, 1,002 campos |
| 210 PlaneBobLayer | 49,87 fps |
| 211 teleport | 49,91 fps |
| 126 Fast BOBs | 49,97 fps |
| 209 Beginning Fields 3bpp | 49,91 fps |
| Gates | HOST-504, HOST-505, L1-007, demo-docs, encoding, md-wrap, links, doc-index, doc-coverage |

La secuencia de 207 no muestra bandas ni parpadeo; la fogata cambia en la zona central.

## Trabajo pendiente

`wip/master-render-208-20261031` contiene cambios de 208, 207 y herramientas que estaban sin commit en el worktree master. No es una rama de entrega. Revisar y llevar a master solo lo que esté validado, en commits atómicos.

## Pasos siguientes

1. Revisar `wip/master-render-208-20261031`; decidir qué partes de 208/207 se integran.
2. Recompilar y medir master en 207, 208, 210, 211, 126 y 209.
3. Ejecutar los gates y hacer push de `master`.
4. Si 208 no está listo, mantenerlo en su rama y no bloquear master.

## Riesgos

- El trabajo de 208 puede solaparse con `engine/field`; revisarlo antes de fusionarlo.
- No usar `--no-ff` para esta integración: master ya contiene el merge commit y el fast-forward conservó la historia.
- `wip/master-render-208-20261031` es una copia de seguridad, no una entrega.
