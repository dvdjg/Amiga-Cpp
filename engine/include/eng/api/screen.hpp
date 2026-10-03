#pragma once

/// \file screen.hpp
/// **Contexto de dibujo de alto nivel** (`eng::Screen`) y la racha de blits en streaming
/// (`eng::BlitStream`) que el `App` le inyecta. Es el "RastPort" de la fachada: el juego dibuja
/// sin ver planos, `FramePlan` ni `Rasterizer`, envolviendo un `playfield::DrawTarget`.
///
/// Se separa de `api/game.hpp` (que la reexporta) para que la fachada `App` no acumule el
/// repertorio de primitivas de dibujo en el mismo fichero.

#include <eng/core/types/box.hpp>
#include <eng/core/types/domains.hpp>
#include <eng/core/types/span.hpp>
#include <eng/core/types/typed.hpp>
#include <eng/core/types/types.hpp>
#include <eng/field/draw_target.hpp>
#include <eng/field/playfield.hpp> // `eng::playfield`: nombre público del motor de playfields
#include <eng/graphics/bitmap_view.hpp>
#include <eng/graphics/blitter_state.hpp>
#include <eng/graphics/sprite_asset.hpp>
#include <eng/scene/bobs.hpp>

namespace eng {

/// **Racha de blits en streaming**, inyectada por el `App` en el `Screen` de forma *type-erased*
/// (punteros a función + contexto): el `Screen` puede emitir blits inmediatos **sin conocer el
/// backend**. Ver `Screen::stamp`/`clear_now` y `ZERO_COST_FRAME_PATH.md` §Streaming.
struct BlitStream {
	void* ctx = nullptr;
	bool (*begin)(void*, graphics::BlobOp, u16, u16, s16, s16, s16, s16) = nullptr;
	void (*one)(void*, const void*, const void*, void*, u8) = nullptr;
	bool (*end)(void*) = nullptr;
	[[nodiscard]] bool valid() const noexcept { return begin != nullptr; }
};

/// **Contexto de dibujo de alto nivel** (análogo al `RastPort`): la app dibuja sin ver planos,
/// `FramePlan` ni `Rasterizer`. Envuelve un `playfield::DrawTarget` (Surface + rasterizador + plan +
/// clip) y ofrece primitivas del dominio.
class Screen {
public:
	Screen() = default;
	explicit constexpr Screen(playfield::DrawTarget target, BlitStream stream = {}) noexcept
		: m_target(target), m_stream(stream) {}


	/// \return `true` si la fachada tiene un destino de dibujo válido.
	[[nodiscard]] bool valid() const noexcept { return m_target.valid(); }
	/// \return el rectángulo visible (clip) del contexto de dibujo.
	[[nodiscard]] Box bounds() const noexcept { return m_target.box(); }
	/// Borra todo el área de dibujo con `color`.
	/// \param color  índice de paleta (0..2^planos−1).
	void clear(u8 color) {
		const Box b = bounds();
		(void)m_target.fill(b, color);
	}
	/// Rellena `box` con `color` (inmediato, CPU; puede pisar lo dibujado después).
	/// \param box    rectángulo a rellenar.
	/// \param color  índice de paleta.
	/// \return `false` si el rectángulo queda fuera del clip.
	bool fill(Box box, u8 color) { return m_target.fill(box, color); }
	/// Dibuja el **marco** de `box` con `color`.
	/// \param box    rectángulo.
	/// \param color  índice de paleta.
	/// \return `false` si queda fuera del clip.
	bool frame(Box box, u8 color) { return m_target.frame(box, color); }
	/// Dibuja una **línea** de `(x0,y0)` a `(x1,y1)`.
	/// \param x0,y0  extremo inicial (px).
	/// \param x1,y1  extremo final (px).
	/// \param color  índice de paleta.
	/// \param op     operación de rasterizado (`Copy`/`Or`/…).
	/// \return `false` si queda fuera del clip.
	bool line(s16 x0, s16 y0, s16 x1, s16 y1, u8 color,
		  playfield::RasterOp op = playfield::RasterOp::Copy) {
		return m_target.line(x0, y0, x1, y1, color, op);
	}
	/// Dibuja el **texto** `s` en `(x, y)`.
	/// \param x,y    posición (px).
	/// \param s      cadena (fuente del engine).
	/// \param color  índice de paleta.
	/// \return `false` si no cabe.
	bool text(s16 x, s16 y, const char* s, u8 color) { return m_target.text(x, y, s, color); }

	/// Borra `b` (`D = 0`) **encolado en el plan del frame**, en orden con los sprites (a
	/// diferencia de `fill`, que se vuelca con el rasterizador y puede pisar lo dibujado después).
	/// Es la forma de limpiar una banda/región antes de pintar objetos. `false` si no hay plan.
	bool clear_box(Box b) {
		if (!m_target.plan().valid()) {
			return false;
		}
		return eng::scene::clear_box(*m_target.plan(), m_target.bob_target(), b.x, b.y, b.w, b.h);
	}

	/// **Rellena un rectángulo con un color** (`D = color`) **encolado en el plan** (Blitter), en
	/// orden con los sprites. Es el relleno de color a **coste cero** (a diferencia de `fill`, que
	/// es inmediato y usa el rasterizador/CPU): `D = A` sin fetch (A deshabilitada, `BLTADAT`
	/// preload), un `FillRect` por plano con `AFWM`/`ALWM` recortando la primera/última palabra.
	///
	/// Aviso: al no leer D, los bits de las **palabras de borde** que quedan fuera del rectángulo
	/// de un `b` **no alineado a 16 px** se ponen a 0 (no se preserva lo de debajo). Alinea `b.x` y
	/// `b.w` a múltiplos de 16 para evitar ese recorte. `false` si no hay plan.
	/// \param b      rectángulo a rellenar (alinea `x`/`w` a 16 px).
	/// \param color  índice de paleta.
	/// \return `false` si no hay plan o el destino no vale.
	bool fill_box(Box b, u8 color) {
		if (!m_target.plan().valid() || b.empty()) {
			return false;
		}
		const graphics::BobTarget t = m_target.bob_target();
		if (t.planes.empty() || t.plane_count == 0u) {
			return false;
		}
		const Box clip = bounds();
		s32 x = b.x;
		s32 y = b.y;
		u16 w = b.w;
		u16 h = b.h;
		if (x < clip.x) {
			const s32 d = clip.x - x;
			if (d >= static_cast<s32>(w)) return true;
			w = static_cast<u16>(w - static_cast<u16>(d));
			x = clip.x;
		}
		if (y < clip.y) {
			const s32 d = clip.y - y;
			if (d >= static_cast<s32>(h)) return true;
			h = static_cast<u16>(h - static_cast<u16>(d));
			y = clip.y;
		}
		const s32 cx1 = static_cast<s32>(clip.x) + clip.w - 1;
		const s32 cy1 = static_cast<s32>(clip.y) + clip.h - 1;
		if (x > cx1 || y > cy1) return true;
		if (x + static_cast<s32>(w) - 1 > cx1) w = static_cast<u16>(cx1 - x + 1);
		if (y + static_cast<s32>(h) - 1 > cy1) h = static_cast<u16>(cy1 - y + 1);
		const u16 wx0 = static_cast<u16>(x & ~15);
		const u16 wx1 = static_cast<u16>((x + static_cast<s32>(w) - 1) & ~15);
		const u16 words = static_cast<u16>(((wx1 - wx0) >> 4) + 1u);
		const u16 afwm = static_cast<u16>(0xffffu >> (x & 15));
		const u16 alwm = static_cast<u16>(0xffffu << (15 - ((x + static_cast<s32>(w) - 1) & 15)));
		const bool inter = (t.layout == graphics::BobLayout::Interleaved);
		const u32 row = t.row_bytes;
		const u32 row_stride = inter ? row * t.plane_count : row;
		const u32 plane_step = t.plane_pointer_step();
		const s16 dmod = eng::graphics::mod16(static_cast<s32>(row_stride) -
						      static_cast<s32>(words) * 2);
		u8* base = t.data() + static_cast<u32>(y) * row_stride + (static_cast<u32>(wx0) >> 3u);
		for (u8 p = 0u; p < t.plane_count; ++p) {
			graphics::BlitJob job {};
			job.destination = graphics::BlitPtr::from_storage(
				reinterpret_cast<u16*>(base + static_cast<u32>(p) * plane_step));
			job.words_per_row = words;
			job.height = h;
			job.bitplane_count = 1u;
			job.destination_modulo_bytes = dmod;
			job.interleaved = true;
			job.minterm = (color & (1u << p)) != 0u ? 0xffu : 0x00u;
			job.fill.afwm = afwm;
			job.fill.alwm = alwm;
			(void)m_target.plan()->add_fill_rect(job);
		}
		return true;
	}

	/// **Dibuja un sprite** (BOB cocinado) en `(x, y)`. La geometría del destino la trae el
	/// contexto de dibujo (`DrawTarget::bob_target`, preparado por la escena), así que el
	/// juego no ve planos, strides ni minterns. `false` si no hay plan de frame o el
	/// sprite/frame no es válido (ver `graphics::Sprite::draw`).
	/// \param spr    sprite cocinado (geometría + frames + máscara).
	/// \param x,y    posición de pantalla del ancla.
	/// \param frame  índice de frame (0 = primero).
	/// \return `false` si no hay plan o el sprite cae fuera del clip.
	bool sprite(const graphics::Sprite& spr, s16 x, s16 y, u8 frame = 0u) {
		if (!m_target.plan().valid()) {
			return false;
		}
		const Box clip = m_target.box();
		if (x < clip.x || y < clip.y || static_cast<s32>(x) + spr.width() >
			static_cast<s32>(clip.x) + clip.w || static_cast<s32>(y) + spr.height() >
			static_cast<s32>(clip.y) + clip.h) return false;
		return spr.draw(*m_target.plan(), m_target.bob_target(), frame, x, y);
	}
	/// **Borra la caja de un sprite** en `(x, y)` (si su política es `ClearRect`).
	bool erase_sprite(const graphics::Sprite& spr, s16 x, s16 y) {
		if (!m_target.plan().valid()) {
			return false;
		}
		return spr.erase(*m_target.plan(), m_target.bob_target(), x, y);
	}

	/// **Dibuja una capa de BOBs** (`scene::BobLayer`) ordenada por su `z`, sin exponer al juego
	/// el `FramePlan` ni el `BobTarget`. Devuelve cuántos actores se dibujaron. `fine_scroll` (px)
	/// compensa el fino del campo (ver `BobLayer::emit`).
	[[nodiscard]] u16 bobs(const scene::BobLayer& layer, u8 fine_scroll = 0u) {
		if (!m_target.plan().valid()) {
			return 0u;
		}
		return layer.emit(*m_target.plan(), m_target.bob_target(), fine_scroll);
	}

	/// **Encola un aviso** (`ticket`) en el plan del frame: en modo asíncrono
	/// (`App::set_async_present(true)`) la IRQ de fin de blit lo publica como
	/// `MsgType::IntentDone` (con el `ticket` en `payload.user.a`) cuando hayan terminado los
	/// trabajos encolados **hasta ahora**. Para avisar al final de la ristra, llamar tras el
	/// último `sprite`/`clear_box`/… No consume Blitter: es una marca en la cadena.
	bool notify(u16 ticket) {
		if (!m_target.plan().valid()) {
			return false;
		}
		return m_target.plan()->add_notify(ticket);
	}

	/// **Blit planar** al plan del frame (copia desde una hoja planar). Para primitivas que
	/// `Screen` no cubre (chunky→planar usa `c2p`). El plan lo pone el contexto.
	bool blit(eng::Span<const u16> src, s32 x, s32 y, u16 w, u16 h, u16 src_row_bytes,
		  u32 src_plane_stride, u8 planes, u8 source_shift = 0u, bool descending = false,
		  playfield::RasterOp op = playfield::RasterOp::Copy) {
		if (!m_target.plan().valid()) {
			return false;
		}
		return m_target.blit(*m_target.plan(), src, x, y, w, h, src_row_bytes, src_plane_stride,
				    planes, source_shift, descending, op);
	}
	/// Copia un asset planar completo (procedencia Chip + geometría + layout) a la pantalla en el
	/// plan del frame. El juego no pasa punteros,
	/// strides, plano ni cantidad de planos: los trae el asset tipado y el contexto Screen.
	[[nodiscard]] bool bitmap(graphics::ChipBitmapView<PlaneTag> source, Box destination) {
		const Box screen_bounds = bounds();
		const auto& target = m_target.bob_target();
		if (!m_target.plan().valid() || !source.valid() || destination.empty() ||
		    source.plane_count > graphics::kBlitterMaxPlanes ||
		    source.layout != (target.interleaved() ? graphics::PlaneLayout::Interleaved
						    : graphics::PlaneLayout::Contiguous) ||
		    source.row_bytes < source.width / graphics::kPixelsPerByte ||
		    (source.row_bytes & (graphics::kBytesPerBlitterWord - 1u)) != 0u ||
		    source.planes.size() < source.byte_count() ||
		    destination.x < screen_bounds.x || destination.y < screen_bounds.y ||
		    static_cast<u32>(destination.w) > source.width || static_cast<u32>(destination.h) > source.height ||
		    static_cast<s32>(destination.x) + destination.w > static_cast<s32>(screen_bounds.x) + screen_bounds.w ||
		    static_cast<s32>(destination.y) + destination.h > static_cast<s32>(screen_bounds.y) + screen_bounds.h ||
		    (destination.x & (graphics::kPixelsPerBlitterWord - 1u)) != 0 ||
		    (destination.w & (graphics::kPixelsPerBlitterWord - 1u)) != 0u ||
		    source.width / graphics::kPixelsPerBlitterWord > graphics::kBlitterMaxWordsPerRow ||
		    source.height > graphics::kBlitterMaxRows ||
		    destination.w != source.width || destination.h != source.height) return false;
		const u16 words = static_cast<u16>(source.width / graphics::kPixelsPerBlitterWord);
		const u32 copied_row_bytes = static_cast<u32>(words) * graphics::kBytesPerBlitterWord;
		const u32 source_row_stride = source.interleaved()
				       ? static_cast<u32>(source.row_bytes) * source.plane_count
				       : source.row_bytes;
		const bool source_interleaved = source.interleaved();
		const bool destination_interleaved = target.interleaved();
		if (source.plane_count != target.plane_count || source_interleaved != destination_interleaved)
			return false;
		const u32 destination_row_stride = target.interleaved()
					   ? static_cast<u32>(target.row_bytes) * target.plane_count
					   : target.row_bytes;
		if (source.interleaved()) {
			// Each physical plane row is `row_bytes` apart; the modulo skips the remaining
			// planes to reach the same plane on the next logical scanline.
			graphics::BlitJob job {};
			job.kind = graphics::BlitJobKind::CopyRect;
			job.source = graphics::BlitPtr {source.planes};
			job.destination = graphics::BlitPtr {target.planes,
				static_cast<s32>(static_cast<u32>(destination.y) * destination_row_stride +
						 destination.x / graphics::kPixelsPerByte)};
			job.words_per_row = words;
			job.height = source.height;
			job.source_modulo_bytes = static_cast<s16>(source_row_stride - copied_row_bytes);
			job.destination_modulo_bytes = static_cast<s16>(destination_row_stride - copied_row_bytes);
			job.bitplane_count = source.plane_count;
			job.interleaved = true;
			job.source_plane_stride_bytes = source.plane_pointer_step();
			job.destination_plane_stride_bytes = target.plane_pointer_step();
			return m_target.plan()->add_copy_rect(job);
		}
		graphics::BlitJob job {};
		job.kind = graphics::BlitJobKind::CopyRect;
		job.source = graphics::BlitPtr {source.planes};
		job.destination = graphics::BlitPtr {target.planes,
			static_cast<s32>(static_cast<u32>(destination.y) * destination_row_stride +
					 destination.x / graphics::kPixelsPerByte)};
		job.words_per_row = words;
		job.height = source.height;
		job.source_modulo_bytes = 0;
		job.destination_modulo_bytes = 0;
		job.bitplane_count = source.plane_count;
		job.interleaved = false;
		job.source_plane_stride_bytes = source.plane_pointer_step();
		job.destination_plane_stride_bytes = target.plane_pointer_step();
		return m_target.plan()->add_copy_rect(job);
	}

	/// **Chunky→planar** por el seam (Blitter si hay plan, si no CPU del playfield).
	bool c2p(const playfield::C2pRequest& req) { return m_target.c2p(req); }

	/// El objetivo de dibujo subyacente (para efectos avanzados; el juego normal no lo necesita).
	[[nodiscard]] playfield::DrawTarget& target() noexcept { return m_target; }

	/// **Racha de estampado (streaming)**: emite copias de `sheet` al Blitter **en el momento** de
	/// cada `at()` (espera al blit anterior y escribe solo los registros que cambian), **sin
	/// `FramePlan` ni pasada de ejecución**. El juego describe objetos; el motor emite. Ver
	/// `ZERO_COST_FRAME_PATH.md` §Streaming. Requiere dibujar en orden con `clear_now` (no mezclar
	/// con el plan, que se ejecuta en `present`).
	struct StampRun {
		BlitStream stream {};
		const graphics::Sprite* sheet = nullptr;
		graphics::BobTarget target {};
		graphics::BlobOp op = graphics::BlobOp::CookieCut;
		u16 words = 0u;
		s32 start_row = 0;
		bool active = false;

		/// Emite una copia del `frame` del sprite en `(x, y)`.
		bool at(s16 x, s16 y, u8 frame) {
			if (!active || sheet == nullptr || frame >= sheet->bob().frame_count) {
				return false;
			}
			const graphics::Bob& bob = sheet->bob();
			const s16 wx = static_cast<s16>(x & ~15);
			const u16* base = reinterpret_cast<const u16*>(
				bob.sheet.address(static_cast<u32>(frame) * bob.frame_stride).cptr());
			u8* dst = target.data() + static_cast<u32>(start_row) * static_cast<u32>(y) +
				  (static_cast<u32>(wx < 0 ? 0 : wx) >> 3u);
			if (op == graphics::BlobOp::CookieCut) {
				stream.one(stream.ctx, base + words, base, dst, static_cast<u8>(x & 15));
			} else {
				stream.one(stream.ctx, base, base, dst, static_cast<u8>(x & 15));
			}
			return true;
		}
		/// Espera al último blit de la racha.
		bool done() {
			if (!active) {
				return false;
			}
			active = false;
			return stream.end(stream.ctx);
		}
	};

	/// Abre una racha de estampado; el `BlobOp` sale de la política del sprite (`Or`, `Opaque` o
	/// `CookieCut`). `false`/inactivo si el backend no la soporta o el sprite no vale.
	[[nodiscard]] StampRun stamp(const graphics::Sprite& sheet) {
		StampRun run {};
		if (!m_stream.valid() || !sheet.valid()) {
			return run;
		}
		const graphics::BobTarget bt = m_target.bob_target();
		const graphics::Bob& bob = sheet.bob();
		if (bob.sheet.empty() || bt.planes.empty() || bob.width < 16u) {
			return run;
		}
		const u16 words = static_cast<u16>(bob.width / 16u);
		const s16 amod = static_cast<s16>(words * 2u);
		const s16 dmod = static_cast<s16>(static_cast<s32>(bt.row_bytes) - static_cast<s32>(words) * 2);
		const u16 height = static_cast<u16>(bob.height * bob.planes);
		graphics::BlobOp op = graphics::BlobOp::CookieCut;
		if (bob.draw == graphics::BobDraw::Or) {
			op = graphics::BlobOp::Or;
		} else if (bob.draw == graphics::BobDraw::Opaque) {
			op = graphics::BlobOp::Opaque;
		}
		if (!m_stream.begin(m_stream.ctx, op, words, height, amod, amod, dmod, dmod)) {
			return run;
		}
		run.stream = m_stream;
		run.sheet = &sheet;
		run.target = bt;
		run.op = op;
		run.words = words;
		run.start_row = static_cast<s32>(bt.row_bytes) * bt.plane_count;
		run.active = true;
		return run;
	}

	/// **Borra ahora** (streaming, `D = 0`) la banda completa de `box` (filas completas de todos los
	/// planos, interleaved). Complementa a `stamp`; ambas emiten **en orden**, sin `FramePlan`.
	bool clear_now(Box box) {
		if (!m_stream.valid() || box.empty()) {
			return false;
		}
		const graphics::BobTarget bt = m_target.bob_target();
		if (bt.planes.empty()) {
			return false;
		}
		const u16 words = static_cast<u16>((box.w + 15u) / 16u);
		if (words == 0u) {
			return false;
		}
		const u16 height = static_cast<u16>(box.h * bt.plane_count);
		// Fila completa de un plano → filas interleaved contiguas: un solo blit D-only, `dmod = 0`.
		const bool full_row = (static_cast<u32>(words) * 2u == bt.row_bytes);
		const s16 dmod = full_row ? 0
					  : static_cast<s16>(static_cast<s32>(bt.row_bytes) *
									     bt.plane_count -
								     static_cast<s32>(words) * 2);
		u8* dst = bt.data() + static_cast<u32>(box.y) * static_cast<u32>(bt.row_bytes) *
					      static_cast<u32>(bt.plane_count);
		if (!m_stream.begin(m_stream.ctx, graphics::BlobOp::Clear, words, height, 0, 0, 0, dmod)) {
			return false;
		}
		m_stream.one(m_stream.ctx, nullptr, nullptr, dst, 0u);
		return m_stream.end(m_stream.ctx);
	}

	/// **Copia un rect ahora** (streaming, `D = C`): `words`×`height` palabras de `src` a `dst` con
	/// módulos `cmod`/`dmod` (bytes). Para desplazar/copiar una banda fuera del plan (scroll).
	bool copy_now(const void* src, void* dst, u16 words, u16 height, s16 cmod, s16 dmod) {
		if (!m_stream.valid() || src == nullptr || dst == nullptr || words == 0u || height == 0u) {
			return false;
		}
		if (!m_stream.begin(m_stream.ctx, graphics::BlobOp::Copy, words, height, 0, 0, cmod, dmod)) {
			return false;
		}
		m_stream.one(m_stream.ctx, src, nullptr, dst, 0u);
		return m_stream.end(m_stream.ctx);
	}

private:
	playfield::DrawTarget m_target;
	BlitStream m_stream {};
};

} // namespace eng
