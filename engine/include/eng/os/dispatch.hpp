#pragma once

/// \file dispatch.hpp
/// **Despacho por tabla** del mini-SO (`eng::os`): una tabla `constexpr` de handlers indexada por
/// `MsgType`. Es la forma predecible de resolver el tipo como índice (bounds check + carga + `jsr`),
/// sin depender de que el `switch` baje a tabla de saltos. Ver
/// `docs/engine/architecture/MINI_OS_MESSAGE_LOOP.md` §14.

#include <eng/os/message.hpp>
#include <eng/os/port.hpp>

namespace eng::os {

/// Firma de un handler: `void(Ctx&, const Msg&)`.
template <class Ctx>
using MsgHandler = void (*)(Ctx&, const Msg&);

/// **Tabla de despacho** indexada por `MsgType`. `fill` pone un handler por defecto; `set` uno por
/// tipo; `dispatch` resuelve `handlers[(u8)m.type]` con comprobación de rango.
template <class Ctx>
struct HandlerTable {
	static constexpr eng::u8 kCount = static_cast<eng::u8>(MsgType::COUNT);
	using Fn = MsgHandler<Ctx>;

	Fn handlers[kCount] {};

	/// Pone `f` como handler de todos los tipos (base antes de especializar).
	constexpr void fill(Fn f) noexcept {
		for (eng::u8 i = 0u; i < kCount; ++i) {
			handlers[i] = f;
		}
	}

	constexpr void set(MsgType t, Fn f) noexcept { handlers[static_cast<eng::u8>(t)] = f; }

	/// Handler registrado para `t` (puede ser el default de `fill`).
	[[nodiscard]] constexpr Fn get(MsgType t) const noexcept {
		return handlers[static_cast<eng::u8>(t)];
	}

	/// Despacha un mensaje (ignora `nullptr` y tipos fuera de rango).
	void dispatch(Ctx& ctx, const Msg& m) const noexcept {
		const eng::u8 i = static_cast<eng::u8>(m.type);
		if (i < kCount && handlers[i] != nullptr) {
			handlers[i](ctx, m);
		}
	}
};

/// Drena `port` y despacha cada mensaje con la tabla.
template <class Ctx, eng::u16 N>
void dispatch_all(const HandlerTable<Ctx>& table, Ctx& ctx, MsgPort<N>& port) noexcept {
	Msg m;
	while (port.pop(m)) {
		table.dispatch(ctx, m);
	}
}

} // namespace eng::os
