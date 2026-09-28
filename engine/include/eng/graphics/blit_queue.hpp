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

#include <eng/core/types/domains.hpp>
#include <eng/core/types/memory_kind.hpp>
#include <eng/core/types/ptr.hpp>
#include <eng/core/types/span.hpp>
#include <eng/core/types/types.hpp>
#include <eng/core/types/typed.hpp>
#include <eng/graphics/bitmap_view.hpp>

namespace eng::graphics {

/// Rectángulo de la intención, en píxeles respecto de la esquina de la zona destino.
struct BlitRect {
	eng::s16 x = 0;
	eng::s16 y = 0;
	eng::u16 w = 0;
	eng::u16 h = 0;
};

/// Una petición de blit **por intención** (sin registros). `Fill` = limpiar un rectángulo
/// intercalado (DEST|A_TO_D, `BLTADAT=0`); `Stamp` = OR de un asset sobre el destino (A_OR_B con
/// `ASH` fino). La intención es **una zona + un rectángulo**, no módulos ni `BLTSIZE`: eso lo deriva
/// `blit_regs()` de la geometría de la zona.
///
/// **`src`/`dst` son `BitmapView` tipados**: zonas (base + geometría + layout) con el **banco Chip
/// en el tipo** (el Blitter es DMA y solo ve Chip RAM). Además el **tag** distingue el papel: el
/// destino es una zona de **plano de playfield** (`PlaneTag`) y el origen un **asset de BOB**
/// (`BobTag`), de modo que **intercambiarlos no compila**. Ver
/// `docs/engine/architecture/BLITTER_INTENT_QUEUE.md` §7.
struct BlitOp {
	enum class Kind : eng::u8 { Fill, Stamp };
	Kind kind = Kind::Fill;
	BitmapView<PlaneTag, MemoryKind::Chip> dst {}; ///< zona destino (bitmap intercalado en Chip)
	BitmapView<BobTag, MemoryKind::Chip> src {};   ///< `Stamp`: zona origen (atlas/asset en Chip)
	BlitRect rect {};                              ///< rectángulo en la zona destino (píxeles)
	eng::u8 ashift = 0;                            ///< `Stamp`: desplazamiento fino 0..15
};

/// Registros del Blitter **derivados** de la intención: `BLTxPT`/`BLTxMOD`/`BLTSIZE` + banderas.
/// `src`/`dst` son punteros crudos — la **frontera explícita** hacia el chipset, que solo cruza el
/// ejecutor (en `submit`). El resto del motor no los ve.
struct BlitRegs {
	const eng::u16* src = nullptr; ///< `BLTAPT` (word)
	eng::u16* dst = nullptr;       ///< `BLTDPT` (word)
	eng::s16 src_mod = 0;          ///< `BLTAMOD` (bytes)
	eng::s16 dst_mod = 0;          ///< `BLTDMOD` (bytes)
	eng::u16 words = 0;            ///< `BLTSIZE` bajo (palabras por fila)
	eng::u16 height = 0;           ///< `BLTSIZE` alto (filas; × planos si intercalado)
	eng::u8 bitplane_count = 1;    ///< planos lógicos en una pasada
	eng::u32 src_plane_stride = 0; ///< `Planar`: separación entre planos del origen (bytes)
	eng::u32 dst_plane_stride = 0; ///< `Planar`: separación entre planos del destino (bytes)
	eng::u16 minterm = 0;          ///< minterm del canal D
	eng::u8 ashift = 0;            ///< `BLTCON0` bits 12-15 (desplazamiento fino; `Stamp`)
	bool interleaved = false;      ///< un solo recorrido intercalado
};

/// **Único sitio** que traduce la intención (zona + rectángulo) a los campos del chipset. El
/// `Stamp` lee el origen desde la esquina de su zona; el `Fill` no usa `src`.
///
/// Frontera declarada (baseline de casts justificada): los `static_cast` son **estrechamientos
/// deliberados** a los registros de 16 bits del Blitter (`BLTSIZE`/`BLTxMOD`) y los
/// `reinterpret_cast` la conversión a los punteros de registro; es el único punto del motor donde
/// el dominio (píxeles/bytes) se vuelve chipset, y el ejecutor lo consume en `submit`.
[[nodiscard]] inline BlitRegs blit_regs(const BlitOp& op) noexcept {
	BlitRegs r {};
	const bool inter = op.dst.interleaved();
	const eng::u8 planes = op.dst.plane_count;
	const eng::u32 row = inter ? static_cast<eng::u32>(op.dst.row_bytes) * planes
				   : op.dst.row_bytes;
	const eng::s16 wx = static_cast<eng::s16>(op.rect.x & ~15);
	const eng::s16 x0 = (wx < 0) ? 0 : wx;
	r.words = static_cast<eng::u16>((op.rect.w + 15u) / 16u +
					((op.rect.x & 15) != 0 ? 1u : 0u));
	r.height = inter ? static_cast<eng::u16>(op.rect.h * planes) : op.rect.h;
	r.dst = reinterpret_cast<eng::u16*>(op.dst.data() +
					    static_cast<eng::u32>(op.rect.y) * row +
					    (static_cast<eng::u32>(x0) >> 3u));
	r.dst_mod = static_cast<eng::s16>(op.dst.row_bytes - static_cast<eng::u32>(r.words) * 2u);
	r.dst_plane_stride = inter ? 0u : op.dst.plane_pointer_step();
	r.bitplane_count = inter ? 1u : planes;
	r.interleaved = inter;
	r.ashift = static_cast<eng::u8>(op.rect.x & 15);
	if (op.kind == BlitOp::Kind::Fill) {
		r.minterm = 0x00u; // D = 0
		return r;
	}
	// Stamp: el origen avanza una fila de hoja por fila de blit; ASH da el desplazamiento fino.
	r.src = reinterpret_cast<const eng::u16*>(op.src.data());
	r.src_mod = static_cast<eng::s16>(op.src.row_bytes - static_cast<eng::u32>(r.words) * 2u);
	r.src_plane_stride = inter ? 0u : static_cast<eng::u32>(op.rect.h) * op.src.row_bytes;
	r.minterm = 0x00fcu; // A OR B → D
	return r;
}

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

	/// **Intención**: rellenar (`D = 0`) un rectángulo del destino (una petición).
	void fill(BitmapView<PlaneTag, MemoryKind::Chip> dst, BlitRect rect) noexcept {
		enqueue(BlitOp {BlitOp::Kind::Fill, dst, {}, rect, 0});
	}

	/// **Intención**: OR de un asset sobre el destino (fino con `ashift`), una petición.
	void stamp(BitmapView<BobTag, MemoryKind::Chip> src, BitmapView<PlaneTag, MemoryKind::Chip> dst,
		   BlitRect rect, eng::u8 ashift = 0) noexcept {
		enqueue(BlitOp {BlitOp::Kind::Stamp, dst, src, rect, ashift});
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

	/// ¿Cola vacía (nada pendiente de ejecutar)?
	[[nodiscard]] bool empty() const noexcept { return m_head == m_tail; }
	/// ¿Cola llena (una entrada libre menos: el anillo reserva un hueco)?
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
