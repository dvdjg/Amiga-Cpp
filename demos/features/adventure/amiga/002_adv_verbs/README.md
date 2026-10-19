# Demo 002 — Verbos, hotspots, say e inventario (A1)

Demo de la capa de aventura (`eng::adv`) sobre la fachada, segunda de la serie: la sala base de la 001 (mundo de 352×208, tres walkboxes, cámara con paneo real y click-to-walk) más el hito **A1**: **barra de verbos clásica** con frase en construcción y resaltado, **réplicas** en cola con duración por VBlank, **hotspots** con prioridad GUI → hotspot → suelo (hit-test del HUD de `eng::hud` con dos rejillas), **inventario** como proyección de la `ItemBag` de `eng::rpg` a slots del HUD y una **puerta con cerradura** (`LockSet`) que se abre con la llave (etiqueta `key` + metadato). Qué pretende demostrar y evolución temporal: `src/INTENT.md`; evaluación de visión (prompt canónico): `src/VISION.md`; medidas, alcance y notas: `VALIDATION.md`.

```bash
bash ./tools/build/build-demo.sh demos/features/adventure/amiga/002_adv_verbs --debug
bash ./tools/run/run-demo.sh demos/features/adventure/amiga/002_adv_verbs
# Filmina del guion de atraccion (interaccion completa sin input):
bash ./tools/run/run-demo.sh demos/features/adventure/amiga/002_adv_verbs \
  --sequence-frames 10 --sequence-interval-ms 2000
```

## Técnica

- **Mundo, ventana y bobs**: idénticos a la 001 (bitmap 368 px en 4 planos, fetch 21 palabras con MOD 4, ventana por `BPLxPT`/`BPLCON1`, actor/puntero por BOBs `MaskedBlobNoSave` con save-under y clamp). Los props (llave, moneda y el hueco de la puerta abierta) son bobs estáticos del mismo sistema: la puerta abierta **no** se pinta sobre el fondo, se dibuja encima con save-under y cerrarla la destapa.
- **Franja HUD**: misma banda que la 001 (48 líneas desde la 252, 1 plano, `DDFSTOP=$d8`), con frase, réplica, barra de 9 verbos en 2 filas, inventario, nombre, versión y contador de frames. Todo el texto va por palabras con `eng::playfield::DiffText` (repintado por diferencias); los estados de verbo (seleccionado/hover) usan barra inferior porque el inverso de texto no es representable sobre 1 plano.
- **Verbos como contenido**: `VerbSet<10>` se registra en `build_verbs()` (etiqueta, `VerbSemantic`, `VerbArity`, `CursorMode`); `resolve_action` valida la aridad y el pegamento del juego enruta cada semántica (Take → `rpg::pick_up`, Use+llave → `LockSet::try_open`, …). El cursor cambia de arte por `CursorMode` (flecha/ojo/mano).
- **HUD de interacción**: un `eng::hud::Hud` con la región de la franja y dos rejillas (`push_grid_hit` 5×2 verbos y 1×2 inventario); `hud_hit_test` resuelve el clic sobre la franja y el inventario enlaza las vistas de slot (`bind_grid`) proyectadas desde la bolsa.
- **Guion de atracción**: si no hay input ~2 s, un guion determinista por frames recorre la interacción completa (ABRE → COGE×2 → USA llave → andar) usando **las mismas funciones** del click; cualquier click real lo cancela.

## Búsqueda previa

Patrón de bajo nivel ya abstraído y reutilizado en esta demo (candidatos del engine verificados):

- **Navegación y cámara**: `eng::adv::walkbox_route` y `eng::adv::Camera` (`engine/include/eng/adv/navigation.hpp`, `camera.hpp`), no reimplementación local.
- **Verbos, frase y réplicas**: `eng::adv::VerbSet`/`resolve_action`/`pick_pointer` (`engine/include/eng/adv/verb.hpp`) y `eng::adv::SayQueue` (`engine/include/eng/adv/say.hpp`), con HOST-480.
- **Hit-test de UI y rejillas**: `eng::hud::hud_hit_test` + `Hud`/`GridHandle` (`engine/include/eng/hud/input.hpp`, `model.hpp`), no un hit-test propio.
- **Objetos y cerraduras**: `eng::rpg::pick_up`, `ItemBag`, `LockSet::try_open` (`engine/include/eng/rpg/interaction.hpp`, `inventory.hpp`), no estado ad hoc en la demo.
- **Texto de HUD**: `eng::playfield::DiffText` sobre `Surface::draw_text_words` (`engine/include/eng/field/`), nunca píxel a píxel; **jobs de Blitter** con `eng::graphics::BlitJob` + `blitter_submit`.

## Estado

Funcional y verificado: HOST-480 (motor A1), secuencia del guion de atracción con la interacción completa (puerta abierta, inventario LLAVE+MONEDA), visión canónica OK (`src/VISION.md`, contornos limpios, cambio cerrada→abierta descrito) y cadencia de régimen **~48-49 fps** (`measure-fps`, ventanas 47,9-49,1, `1 campos/frame`; la media global la bajan las ventanas de arranque). Flicker **declarado** en `flicker-baseline.json` (zonas de la franja que cambian por diseño + save-under del actor; sin doble buffer). Gates: `demo-docs`, `prior-art`, `demo-ergonomics`, `demo-raw-writes`, `encoding`, `doc-index` en verde; la pasada completa de `test-regression.sh --flicker` de esta sesión quedó condicionada por la degradación del entorno de emulación (documentada en `VALIDATION.md`, afectó también a la 001 y a la 306 del hilo de HUD). Lo no cubierto: click real por GDB y «CERRAR» en secuencia (`VALIDATION.md` §Alcance).
