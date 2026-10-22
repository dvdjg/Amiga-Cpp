#pragma once

namespace eng::amiga {

/// **Modo cooperativo de VBlank** (KS 1.3, **experimental / no validado**): el SO conserva
/// el display (Copper, DMA, Intuition) y la propiedad del autovector de nivel 3; el engine
/// solo añade su servidor con `AddIntServer(INTB_VERTB/BLIT)`, levanta la señal de frame y
/// el bucle duerme en `Wait()`. No se hace `Forbid`, no se llama a `LoadView` y no se toca
/// `COP1LC`/`DMACON`. Si `INTENAR` muestra VERTB apagada, solo se **setea** ese bit
/// (`SETCLR|VERTB`), porque un proceso CLI puede dejarla apagada; no se sustituye el vector.
///
/// **No usar todavía como modo de juego**: en la demo 212 (CLI, sin Copper) el servidor se
/// llamó **una vez** y dejó de despacharse (frame 0, `g_irq_diag_vbl_calls=0x1001`), incluso
/// sin teclado/CD32. El modo está a la espera de la respuesta sobre la habilitación de VERTB
/// en un proceso CLI y el libro de `INTENA` de exec; ver el anexo de
/// `docs/debugging/investigaciones/consultas/consulta-ks13-cooperative-display-wait-en.md`.
///
/// Con `on=false` el backend vuelve al camino exclusivo/bare-metal. Debe fijarse **antes**
/// de instalar el servicio de VBlank (p. ej. antes de `Engine::run_frames`).
void set_vblank_cooperative(bool on) noexcept;

/// `true` si el backend está en modo cooperativo (display del SO + `Wait`).
[[nodiscard]] bool vblank_cooperative() noexcept;

} // namespace eng::amiga
