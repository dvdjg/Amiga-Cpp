#pragma once

/// \file scope_guard.hpp
/// `eng::util::ScopeGuard`: ejecuta una acción **al salir del ámbito** (RAII), sin
/// excepciones.
///
/// En el engine no hay excepciones, pero sí salidas tempranas (`return` de error,
/// `break`, `goto`) donde hay que **restaurar** algo: registros del Copper, la máscara
/// de DMA, el color de fondo, un banco de memoria. `ScopeGuard` garantiza esa
/// restauración en cualquier salida y evita olvidos.
///
/// Coste cero: guarda el callable por valor y un `bool`; no asigna. `release()` (o
/// `commit()`) desactiva la acción cuando el camino termina bien.
///
/// Uso:
///   auto restore = eng::util::make_scope_guard([&] { set_dmacON(old); });
///   ...                                // si algo falla y se sale, restaura
///   restore.release();                 // camino correcto: no restaurar

#include <eng/core/util/type_traits.hpp>
#include <eng/core/util/util.hpp>

namespace eng::util {

template <class F>
class ScopeGuard {
public:
	constexpr explicit ScopeGuard(F fn) noexcept : m_fn(fn), m_active(true) {}

	ScopeGuard(const ScopeGuard&) = delete;
	ScopeGuard& operator=(const ScopeGuard&) = delete;
	ScopeGuard& operator=(ScopeGuard&&) = delete;

	constexpr ScopeGuard(ScopeGuard&& other) noexcept
		: m_fn(move(other.m_fn)), m_active(other.m_active) {
		other.m_active = false;
	}

	~ScopeGuard() {
		if (m_active) {
			m_fn();
		}
	}

	/// Desactiva la acción (el camino terminó bien). También `commit()`.
	constexpr void release() noexcept { m_active = false; }
	constexpr void commit() noexcept { m_active = false; }
	[[nodiscard]] constexpr bool active() const noexcept { return m_active; }

private:
	F m_fn;
	bool m_active;
};

/// Crea un `ScopeGuard` que ejecuta `fn` al salir del ámbito (deduce el tipo).
template <class F>
[[nodiscard]] constexpr ScopeGuard<F> make_scope_guard(F fn) {
	return ScopeGuard<F>(fn);
}

} // namespace eng::util
