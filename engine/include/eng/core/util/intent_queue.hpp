#pragma once

/// \file intent_queue.hpp
/// **Cola de intención no bloqueante** con **completación**: el **mecanismo** genérico del planner,
/// compartido por todos los subsistemas. Declarar una intención **encola** (no espera); `flush()`
/// avanza sin esperar; `wait(ticket)` es el **único** bloqueo, y solo si se pide. Al ejecutarse una
/// petición se **avisa** por la política `Done` (en el engine real: un `Msg IntentDone`).
///
/// Vive en `eng/core/util` porque es **puro** (tipos + `Ref`, sin vocabulario): el **vocabulario** de
/// dibujo (`DrawIntent`/`DrawQueue`) está en `eng/graphics/intent_queue.hpp` y el de audio
/// (`SoundIntent`/`SoundQueue`) en `eng/audio/sound_queue.hpp`. Así la cola la comparten blit y
/// audio **sin** que `eng/audio` dependa de `eng/graphics`. El planner completo:
/// `docs/engine/architecture/INTENT_PLANNER.md`.

#include <eng/core/types/ptr.hpp>
#include <eng/core/types/types.hpp>

namespace eng {

/// Identificador de una petición encolada: correlaciona la **completación**. Es un contador
/// creciente que el `Done` devuelve tal cual; el juego lo resuelve (p. ej. en su tabla de eventos).
using Ticket = eng::u32;

/// Contrato de la **vía de ejecución**: dice si admite trabajo (`ready`) y ejecuta una petición
/// (`run`). Es **el mismo** para blit, dibujo y audio — una sola forma de encolar en el engine.
template <class E, class Item>
concept QueueExecutor = requires(E& e, const Item& item) {
	{ e.ready() }; // convertible a bool
	e.run(item);
};

/// Política de completación **nula** (para colas que no avisan, p. ej. la de blit).
struct NoDone {
	constexpr void operator()(Ticket) const noexcept {}
};

/// Cola FIFO de capacidad fija `N`, **no bloqueante**, con aviso de completación.
///
/// - `Item`: el valor de la intención (pequeño y copiable; sin punteros propietarios).
/// - `Executor`: `{ bool ready(); void run(const Item&); }` — la vía de ejecución (CPU/Blitter/…).
/// - `Done`: `void operator()(Ticket)` — el **aviso** cuando una petición se ejecuta (evento).
template <eng::u16 N, class Item, class Executor, class Done>
	requires QueueExecutor<Executor, Item>
class IntentQueue {
	static_assert((N & (N - 1u)) == 0u, "IntentQueue: N potencia de dos");

public:
	/// Liga la vía de ejecución (el backend, o un doble de test). No es propietario.
	constexpr void bind(Executor& executor) noexcept { m_exec = executor; }
	/// Liga el aviso de completación (en el engine real, un posteador que apunta al puerto). Se
	/// copia: debe ser un **handle** (referencia/puntero al destino), **no** un valor con estado
	/// propio — si no, el aviso incrementaría una copia.
	constexpr void bind_done(Done done) noexcept { m_done = done; }

	/// **Declara** una intención: la encola y devuelve su `Ticket`. **NO** espera.
	Ticket enqueue(const Item& item) noexcept {
		if (full()) {
			pump(); // avanza lo que la vía admita (sin bloquear si va al día)
		}
		if (full()) {
			drain(); // último recurso: la capacidad es fija
		}
		m_items[m_head] = item;
		const Ticket t = ++m_seq;
		m_tickets[m_head] = t;
		m_head = (m_head + 1u) & (N - 1u);
		return t;
	}

	/// Avanza la cola **sin esperar**: ejecuta mientras la vía esté lista.
	void flush() noexcept { pump(); }

	/// **Único** punto de bloqueo: espera a que `t` se haya ejecutado (como `glFinish`).
	void wait(Ticket t) noexcept {
		while (m_done_seq < t && !empty()) {
			drain();
		}
	}

	/// **Punto de dependencia** total: vacía la cola (espera a que la vía admita cada petición).
	void wait_all() noexcept {
		while (!empty()) {
			drain();
		}
	}

	[[nodiscard]] bool empty() const noexcept { return m_head == m_tail; }
	[[nodiscard]] bool full() const noexcept { return ((m_head + 1u) & (N - 1u)) == m_tail; }

private:
	/// Avanza la cola **sin bloquear**: ejecuta mientras la vía esté lista.
	void pump() noexcept {
		while (!empty() && m_exec->ready()) {
			run_one();
		}
	}
	/// Vacía la cola sondeando: último recurso cuando la capacidad fija se llena.
	void drain() noexcept {
		while (!empty()) {
			if (!m_exec->ready()) {
				continue; // la vía está ocupada: se reintenta (poll)
			}
			run_one();
		}
	}
	/// Ejecuta la petición del frente, marca su ticket como hecho y **avisa** (`Done`).
	void run_one() noexcept {
		m_exec->run(m_items[m_tail]);
		m_done_seq = m_tickets[m_tail];
		m_done(m_tickets[m_tail]); // aviso (evento): la petición llegó a su punto
		m_tail = (m_tail + 1u) & (N - 1u);
	}

	Item m_items[N] {};
	Ticket m_tickets[N] {};
	eng::u16 m_head = 0u;
	eng::u16 m_tail = 0u;
	Ticket m_seq = 0u;      ///< último ticket repartido
	Ticket m_done_seq = 0u; ///< último ticket **ejecutado** (para `wait`)
	eng::Ref<Executor> m_exec {}; ///< la vía (no propietaria)
	Done m_done {};               ///< el aviso
};

} // namespace eng
