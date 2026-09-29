#pragma once

/// \file intent_done.hpp
/// **Posteador de completación** del planner: la política `Done` **real** de una `IntentQueue`.
/// Cuando una petición llega a su punto, postea un `Msg` de tipo `MsgType::IntentDone` con el
/// **`ticket`** en `payload.user.a` al puerto del juego. Es un **handle** (apunta al puerto), apto
/// para copiarse en la cola; no es un valor con estado propio. Ver
/// `docs/engine/architecture/INTENT_PLANNER.md` §5.

#include <eng/core/types/ptr.hpp>
#include <eng/core/types/types.hpp>
#include <eng/os/message.hpp>
#include <eng/os/port.hpp>

namespace eng::os {

/// `Done` de una cola de intención: postea el evento `IntentDone` con el `ticket`.
///
/// El `ticket` va como `eng::u32` (el tipo `eng::Ticket` es un `u32`): así **`os` no depende** del
/// vocabulario que lo usa (gráficos, audio). Si la cola del puerto está llena, el aviso **se
/// pierde** (documentado: la completación es un aviso, no una garantía de entrega; el `wait()`
/// sigue siendo el bloqueo).
template <eng::u16 N = 32u>
struct IntentDonePoster {
	eng::Ref<MsgPort<N>> port {};

	/// Avisa al puerto de que el `ticket` llegó a su punto (postea `MsgType::IntentDone`).
	void operator()(eng::u32 ticket) const noexcept {
		if (!port.valid()) {
			return;
		}
		Msg m {};
		m.type = MsgType::IntentDone;
		m.payload.user.a = ticket;
		(void)port->post(m);
	}
};

} // namespace eng::os
