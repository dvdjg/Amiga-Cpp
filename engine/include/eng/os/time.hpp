#pragma once

/// \file time.hpp
/// **Tiempo del mini-SO** (`eng::os`): conversiones ticks↔µs y `ScopedTimer`. El acceso a los
/// registros del CIA (reloj de ticks) vive en el **backend**; aquí solo está la parte pura y una
/// `TickSource` que el backend rellena. Ver `docs/engine/architecture/MINI_OS_TIME.md`.
///
/// El reloj E del CIA es ≈ 709379 Hz (PAL) / 715909 Hz (NTSC) → ~1,4 µs por tick. Las conversiones
/// usan kHz enteros (`709`/`715`) para **no arrastrar `__mulsi3`/`__udivdi3`** en el 68000.

#include <eng/core/types.hpp>

namespace eng::os {

/// Reloj E del CIA en kHz (PAL/NTSC).
inline constexpr eng::u32 kCiaKHzPal = 709u;
inline constexpr eng::u32 kCiaKHzNtsc = 715u;

/// Ticks de CIA → microsegundos. `t` debe cumplir `t * 1000` sin desbordar `u32` (t < ~4,29 M,
/// ≈ 6 s de ticks); para tramos mayores, suma por partes.
[[nodiscard]] constexpr eng::u32 ticks_to_us(eng::u32 t,
					     eng::u32 khz = kCiaKHzPal) noexcept {
	return (t * 1000u) / khz;
}

/// Microsegundos → ticks de CIA. `us * khz` debe caber en `u32` (us < ~6 s).
[[nodiscard]] constexpr eng::u32 us_to_ticks(eng::u32 us,
					     eng::u32 khz = kCiaKHzPal) noexcept {
	return (us * khz) / 1000u;
}

/// **Fuente de ticks** (la aporta el backend: CIA-B en modo continuo, `MINI_OS_TIME.md`). En host
/// puede ser una fuente falsa para tests.
struct TickSource {
	eng::u32 (*now)(void* user) = nullptr;
	void* user = nullptr;

	[[nodiscard]] eng::u32 ticks() const noexcept { return (now != nullptr) ? now(user) : 0u; }
};

/// Mide un tramo con una `TickSource`.
struct ScopedTimer {
	const TickSource* src = nullptr;
	eng::u32 t0 = 0u;

	explicit ScopedTimer(const TickSource& s) noexcept : src(&s), t0(s.ticks()) {}

	[[nodiscard]] eng::u32 elapsed_ticks() const noexcept { return src->ticks() - t0; }
	[[nodiscard]] eng::u32 elapsed_us() const noexcept { return ticks_to_us(elapsed_ticks()); }
};

} // namespace eng::os
