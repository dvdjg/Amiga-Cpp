#pragma once

/// \file intent_queue.hpp
/// **Cola de intención no bloqueante** con **completación**: el frente común de lo asíncrono del
/// engine (blit, sonido, …). Declarar una intención **encola** (no espera); `flush()` avanza sin
/// esperar; `wait(ticket)` es el **único** bloqueo, y solo si se pide. Al ejecutarse una petición
/// se **avisa** por la política `Done` (en el engine real: un `Msg IntentDone` al puerto del juego).
///
/// Es **genérica** sobre el valor de la intención (`Item`), la vía (`Executor`) y el aviso
/// (`Done`), así que el mismo mecanismo sirve para blit (`Item = BlitOp`) y para audio
/// (`Item = SoundIntent`). El vocabulario y el planner completo:
/// `docs/engine/architecture/INTENT_PLANNER.md`.

#include <eng/core/types/ptr.hpp>
#include <eng/core/types/types.hpp>

namespace eng::graphics {

/// Asset de sprite (definido en `sprite_asset.hpp`): la intención lo referencia sin depender de él.
class Sprite;

/// Identificador de una petición encolada: correlaciona la **completación**. Es un contador
/// creciente que el `Done` devuelve tal cual; el juego lo resuelve (p. ej. en su tabla de eventos).
using Ticket = eng::u32;

/// Contrato de la **vía de ejecución**: dice si admite trabajo (`ready`) y ejecuta una petición
/// (`run`). Es **el mismo** para blit y para lo no-blit — una sola forma de encolar en el engine.
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

// --- El vocabulario de dibujo (prototipo mínimo del planner) ----------------

/// Tipo de dibujo pedido. **El juego no elige minterm ni canal**: pide qué quiere.
enum class DrawKind : eng::u8 {
	Rect,   ///< relleno `(x,y,w,h,color)`
	Line,   ///< trazo `(x,y)->(x2,y2,color)`
	Sprite, ///< `(x,y,frame)`
};

/// Una intención de dibujo (valor pequeño y copiable). El planner la compila a `BlitOp`.
struct DrawIntent {
	DrawKind kind = DrawKind::Rect;
	eng::s16 x = 0;
	eng::s16 y = 0;
	eng::u16 w = 0;    ///< Rect
	eng::u16 h = 0;    ///< Rect
	eng::s16 x2 = 0;   ///< Line
	eng::s16 y2 = 0;   ///< Line
	eng::u8 color = 0; ///< Rect/Line
	eng::u8 frame = 0; ///< Sprite
	eng::Ref<const Sprite> sheet {}; ///< `Sprite`: el asset (no propietario; solo `Sprite`)
};

/// La cola de dibujo del juego: `Item = DrawIntent`.
template <eng::u16 N, class Executor, class Done>
using DrawQueue = IntentQueue<N, DrawIntent, Executor, Done>;

/// **Receta de dibujo precompilada** (setup): una lista FIJA de intenciones que el juego describe
/// **una vez** (en el setup de la escena) y el bucle **reproduce** por frame con trabajo mínimo.
/// Es la aplicación de la regla de coste: lo invariante se resuelve fuera del bucle; el frame solo
/// recorre la receta (sin asignación, sin dispatch). Ver `CODING_STYLE.md` y `INTENT_PLANNER.md` §4.1.
template <eng::u16 N>
class DrawRecipe {
public:
	constexpr void clear() noexcept { m_count = 0u; }
	[[nodiscard]] constexpr eng::u16 count() const noexcept { return m_count; }
	[[nodiscard]] constexpr const DrawIntent& operator[](eng::u16 i) const noexcept {
		return m_items[i];
	}

	/// Añade una intención fija (setup). `false` si la receta está llena.
	constexpr bool add(const DrawIntent& item) noexcept {
		if (m_count >= N) {
			return false;
		}
		m_items[m_count++] = item;
		return true;
	}

	/// **Reproduce** la receta en la cola (una vez por frame): recorrido mínimo, sin bloquear.
	template <class Queue>
	void emit(Queue& queue) const noexcept {
		for (eng::u16 i = 0u; i < m_count; ++i) {
			queue.enqueue(m_items[i]);
		}
	}

private:
	DrawIntent m_items[N] {};
	eng::u16 m_count = 0u;
};

} // namespace eng::graphics
