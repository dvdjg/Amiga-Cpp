#pragma once

/// \file blit_queue.hpp
/// **API de Blitter por intención** (estilo OpenGL): el desarrollador declara peticiones
/// (`fill`/`stamp`) y **no espera** a que se dibujen. Se **encolan** (una a una o en array) y el
/// **feeder** las ejecuta cuando el Blitter está libre, de a una (el Blitter tiene un solo juego de
/// registros). Solo `flush()`/`wait()` son puntos de bloqueo explícitos (como `glFinish`).
///
/// Diseño y alternativas: `docs/engine/architecture/BLITTER_INTENT_QUEUE.md`.
///
/// El **ejecutor** (quien sondea `BBUSY` y programa los registros) se inyecta por plantilla (un
/// `concept`, no `void*`+puntero a función — CODING_STYLE): en Amiga lo aporta el backend; en host,
/// un doble de prueba. Aquí **no se nombran registros**.

#include <eng/core/types/ptr.hpp>
#include <eng/core/types/span.hpp>
#include <eng/core/types/types.hpp>

namespace eng::graphics {

/// Una petición de blit **por intención** (sin registros). `Fill` = limpiar un rectángulo
/// intercalado (DEST|A_TO_D, `BLTADAT=0`); `Stamp` = OR de un asset sobre el destino (A_OR_B con
/// `ASH` fino). Todos los campos son de **dominio** (puntos/bytes), no del chipset.
struct BlitOp {
	enum class Kind : eng::u8 { Fill, Stamp };
	Kind kind = Kind::Fill;
	eng::u8* dst = nullptr;      ///< destino (base del bitmap intercalado)
	const eng::u8* src = nullptr; ///< `Stamp`: origen (atlas/asset)
	eng::s16 dst_mod = 0;        ///< `BLTDMOD` (bytes)
	eng::s16 src_mod = 0;        ///< `Stamp`: `BLTAMOD` (bytes)
	eng::u16 words = 0;          ///< palabras por fila (`BLTSIZE` bajo)
	eng::u16 height = 0;         ///< filas (`BLTSIZE` alto, ya × planos si intercalado)
	eng::u8 ashift = 0;          ///< `Stamp`: desplazamiento fino 0..15
};

/// Contrato del **ejecutor**: sabe si el Blitter está libre (BBUSY) y programa una petición.
template <class E>
concept BlitExecutor = requires(E& e, const BlitOp& op) {
	{ e.blitter_free() }; // devuelve algo convertible a bool
	e.submit(op);
};

/// **Cola FIFO de peticiones** de capacidad fija `N` (sin heap). El desarrollador encola y sigue;
/// `pump()` avanza mientras el Blitter admite trabajo; `wait()` vacía (dependencia explícita).
template <eng::u16 N, BlitExecutor Executor>
class BlitQueue {
	static_assert((N & (N - 1u)) == 0u, "BlitQueue: N potencia de dos");

public:
	/// Liga el ejecutor (el backend Amiga, o un doble de host). No es propietario.
	void bind(Executor& executor) noexcept { m_exec = executor; }

	/// Encola una petición ya formada.
	void enqueue(const BlitOp& op) noexcept {
		// Si está llena, drena un poco antes de encolar (no bloquea si el Blitter va al día).
		if (full()) {
			pump();
		}
		if (full()) {
			wait(); // último recurso: la cola es de capacidad fija
		}
		m_ops[m_head] = op;
		m_head = (m_head + 1u) & (N - 1u);
	}

	/// **Intención**: rellenar un bitmap intercalado (una petición).
	void fill(eng::u8* dst, eng::u16 words, eng::u16 height, eng::s16 dst_mod = 0) noexcept {
		enqueue(BlitOp {BlitOp::Kind::Fill, dst, nullptr, dst_mod, 0, words, height, 0});
	}

	/// **Intención**: OR de un asset (fino con `_ash`), una petición.
	void stamp(const eng::u8* src, eng::u8* dst, eng::u16 words, eng::u16 height, eng::s16 src_mod,
		   eng::s16 dst_mod, eng::u8 ashift) noexcept {
		enqueue(BlitOp {BlitOp::Kind::Stamp, dst, src, dst_mod, src_mod, words, height, ashift});
	}

	/// **Intención en array de golpe**: encola muchas de una vez.
	void all(eng::Span<const BlitOp> ops) noexcept {
		for (const BlitOp& op : ops) {
			enqueue(op);
		}
	}

	/// Avanza la cola **sin esperar**: programa mientras el Blitter esté libre.
	void pump() noexcept {
		while (!empty() && m_exec->blitter_free()) {
			m_exec->submit(m_ops[m_tail]);
			m_tail = (m_tail + 1u) & (N - 1u);
		}
	}

	/// Garantiza que se está drenando (un intento de avance; NO vacía).
	void flush() noexcept { pump(); }

	/// **Punto de dependencia**: vacía la cola (espera a que el Blitter admita cada petición).
	void wait() noexcept {
		while (!empty()) {
			if (!m_exec->blitter_free()) {
				continue; // el Blitter está ocupado: se reintenta (poll)
			}
			m_exec->submit(m_ops[m_tail]);
			m_tail = (m_tail + 1u) & (N - 1u);
		}
	}

	[[nodiscard]] bool empty() const noexcept { return m_head == m_tail; }
	[[nodiscard]] bool full() const noexcept {
		return ((m_head + 1u) & (N - 1u)) == m_tail;
	}

private:
	BlitOp m_ops[N] {};
	eng::u16 m_head = 0;
	eng::u16 m_tail = 0;
	eng::Ref<Executor> m_exec {}; ///< ejecutor (no propietario)
};

} // namespace eng::graphics
