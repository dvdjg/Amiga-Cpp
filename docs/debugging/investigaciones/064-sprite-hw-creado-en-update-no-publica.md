# 064 — Sprite HW creado durante `update` no se publica en target (abierto)

**Síntoma.** En la demo `064_shmup_wave`, los actores de bala creados con `SpriteScene::add` **dentro de
`update`** (alta dinámica desde un evento de timeline) producen `compose_sprites` correcto —los contadores
por frame dan `sprites=6`, `placements=6`, `ok=true` y los placements tienen `width_words=1`, `height=8`,
`data` no vacío y canal válido— pero **no se ven píxeles** en pantalla. Los mismos actores, creados **en
`init`** (antes de `takeover`), sí se ven.

## Evidencia

- Marcadores de diagnóstico escritos por la demo en los bitplanes (filas 4..80) y leídos con análisis de
  píxeles del PNG de `run-demo`: `count`, `sprites`, `placements`, `width_words`, `height`, `channel`,
  `hpos`, `vstart` y `data.empty()` correctos en el frame capturado; **0 píxeles amarillos** en la imagen.
- Descartado por A/B en target:
  - El camino de BOB enemigo no interviene (emisor de BOBs desactivado → sigue sin verse).
  - El `clear_box` de los enemigos no interviene (borrado desactivado → sigue sin verse).
  - El pool/formación en sí no interviene (formación inactiva sin alta inmediata → sigue sin verse).
  - La copperlist se reconstruye y publica cada frame (`build_copper` devuelve `ok`; `DoubleBuffer::flip` +
    `install`), y el armado va **antes** de los WAIT de las bandas (sin `WAIT` hacia atrás).
  - Con una alta inmediata en `init` de la misma ráfaga, los mismos actores **sí** se ven.
- En **host** el patrón dinámico (alta/baja por frame con presupuesto, `SpriteScene<8>`) se reprodujo en un
  programa aparte y da `sprites=6`, `0 degraded` — el camino puro de `compose_sprites` es correcto
  (`HOST-391`, `HOST-432` lo cubren).

## Hipótesis vigentes

1. **Orden/bus del armado tardío**: la lista que arma los canales nuevos se publica (`install`) cuando el
   frame ya ha consumido el VBlank y el Copper rearma los canales al principio del frame siguiente con los
   registros **antiguos** (POS/CTL a 0 del reset), quedando el armado nuevo para el frame siguiente y así
   indefinidamente. Verificar con GDB/`COP1LC` y el contenido de la lista activa en el frame de la
   primera publicación.
2. **Carrera con el reset de canales**: el reset (`SPRxPT` a un bloque válido + POS/CTL=0) precede al
   armado dentro de la misma lista; si el armado se materializa en un bloque que el Copper no ejecuta ese
   frame, el reset del frame siguiente apaga otra vez el canal. Un contador de "listas publicadas tras la
   aparición del actor" discriminaría.
3. **Diferencia de camino `init` vs `update`** (copperlist de `takeover` ya contiene el armado vs
   `install` posterior): comparar el comportamiento con el armado hecho en `init` pero **republicando**
   la lista por frame.

## Workaround adoptado (demo 064)

Pool **fijo** de 8 balas creadas en `setup_content` (actores persistentes) y recicladas **por posición**;
la timeline reasigna la X y la Y de una ráfaga de 3. Elimina la alta dinámica del camino caliente sin
perder el objetivo didáctico (F2/F8) y deja este hallazgo abierto para atacarlo con la lista de Copper
bajo GDB.

## Referencias

- `demos/features/engine/amiga/064_shmup_wave/src/main.cpp` (workaround y comentario).
- `engine/include/eng/api/sprites.hpp`, `engine/include/eng/scene/actor_sprite.hpp`,
  `engine/include/eng/graphics/sprite_manager.hpp` (`emit_placements_into`).
- `docs/reference/emulators/winuae/sprite-dma.md` (estructura DMA y rearme).
