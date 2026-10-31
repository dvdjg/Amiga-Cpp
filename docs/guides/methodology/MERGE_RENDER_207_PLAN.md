# Plan de fusión: `fix/render-207` → `master`

Estado comprobado el 2026-10-31. La rama ya contiene `master` en un merge commit (`6d4672c5`), pero **master no se ha movido todavía**: el worktree principal (`Amiga-Cpp`) tiene cambios locales sin commit y no se puede hacer un fast-forward seguro.

## Qué se ha integrado

- Se conserva la rama: `PlaneBobLayer`, `bob_erase_plane_box`, el scheduler de grupos beam-aware, 210 con 6 BOBs, 211 con `clear + draw`, y la política de 207 con una capa protagonista por parada.
- De master se conserva:
  - `bob_beam_window` no bloqueante y `wait_bob_beam` como **API opt-in**;
  - las cajas de pantalla de las animaciones de tiles (`set_display_offset`);
  - la poda de capas desactivadas en `rebuild_anim_visible`;
  - `AGENTS.md` y `DOC-MAP-PRINCIPAL.md` actualizados.
- No se activa la guarda global bloqueante en 207: master medía 44,09 → 42,46 fps con ella. La ficha de master se sustituye por la de la rama, con una nota de fusión.

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

Secuencia de 207: sin bandas inferiores ni parpadeo; la fogata cambia en la zona central.

## Bloqueo actual

`git -C C:/Users/dvdjg/Documents/programa/AI/Amiga/Amiga-Cpp status --short` muestra cambios locales en 207/208, docs de tiles/tools y engine. Entre ellos hay trabajo no commiteado de 208 y de utilidades que **no forma parte de este merge**. No se toca ese worktree para no mezclar trabajo ajeno.

## Pasos para cerrar la fusión

1. En `Amiga-Cpp` (master): revisar y commitear o guardar (`git stash push -u`) **todo** el trabajo local. No dejar cambios sin commit.
2. Comprobar que master es ancestro de la rama:
   `git merge-base --is-ancestor master fix/render-207`
3. Fast-forward seguro desde master:
   `git merge --ff-only fix/render-207`
4. Recompilar y medir 207, 208, 210, 211, 126 y 209 en master.
5. Ejecutar los gates de la tabla anterior y hacer push de `master`.

Si el trabajo pendiente de 208 pertenece a otra línea, llevarlo a una rama propia antes del paso 3; no mezclarlo con este merge.

## Riesgos

- El worktree master contiene cambios de 208 que pueden solaparse con `engine/field`; por eso no se hace fast-forward mientras esté sucio.
- No usar `--no-ff` aquí: la rama ya contiene master y el fast-forward conserva el merge commit existente.
- Si se quiere publicar antes de cerrar, subir `fix/render-207`; no reescribir master hasta validar el árbol fusionado.
