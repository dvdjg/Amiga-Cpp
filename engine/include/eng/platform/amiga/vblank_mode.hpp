#pragma once

namespace eng::amiga {

/// **Modo cooperativo de VBlank** (KS 1.3, **validado en la 212**): el SO conserva el display
/// (Copper, DMA, Intuition) y la propiedad del autovector de nivel 3; el engine solo añade su
/// servidor con `AddIntServer(INTB_VERTB/BLIT)`, levanta la señal de frame y el bucle duerme
/// en `Wait()`. No se hace `Forbid`, no se llama a `LoadView`, no se toca `COP1LC`/`DMACON`
/// y **tampoco se escribe `INTENA`**: el SO es su dueño y la mantiene; el engine no sustituye
/// el vector ni re-arma el latido en este modo.
///
/// La demo 212 (CLI, display del SO, HUD del host) corre en este modo y recibe VBlank, input
/// y timers en la IRQ. El fallo anterior de "una llamada" era local de la 212 (el HUD crudo
/// desbordaba la pila; ahora usa `TextLine`/`operator<<`), no de la cadena de exec; detalle
/// en `docs/debugging/investigaciones/consultas/consulta-ks13-cooperative-display-wait-en.md`.
///
/// Con `on=false` el backend vuelve al camino exclusivo/bare-metal. Debe fijarse **antes**
/// de instalar el servicio de VBlank (p. ej. antes de `Engine::run_frames`).
void set_vblank_cooperative(bool on) noexcept;

/// `true` si el backend está en modo cooperativo (display del SO + `Wait`).
[[nodiscard]] bool vblank_cooperative() noexcept;

} // namespace eng::amiga
