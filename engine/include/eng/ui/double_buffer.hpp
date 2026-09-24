#pragma once

/// \file double_buffer.hpp
/// **Pantalla de doble buffer** (`eng::ui`): dos buffers de pantalla y un *publish* (flip) que el
/// compositor usa para **no parpadear**. El compositor compone en el buffer **trasero**
/// (`back()`) mientras el display sigue leyendo el **delantero** (`front()`); `flip()` intercambia
/// y llama al *publisher*, que publica el nuevo delantero (en Amiga, parchear `BPLxPT`/`COP1LC`
/// en VBlank para que el Copper lea el buffer nuevo al inicio del frame).
///
/// Patrón de uso (una vez por frame, encadenado al VBlank):
/// ```
/// db.present(compositor, plan);   // compone en back() y hace flip() -> publica
/// ```
/// El *publisher* es un *seam* (no propietario): lo pone la app (p. ej. `scene.commit()` o
/// `copper::DoubleBuffer::flip`). Sin publisher, `flip()` solo intercambia los buffers (útil en
/// host y para el test). Ver `docs/engine/architecture/GUI_LIBRARY.md` §14.
///
/// No posee memoria: los dos buffers se enlazan con `bind(...)` (el pool/app los reserva).

#include <eng/core/types/types.hpp>
#include <eng/core/util/function_ref.hpp>
#include <eng/field/contiguous_playfield.hpp>
#include <eng/field/surface.hpp>
#include <eng/graphics/frame_plan.hpp>
#include <eng/ui/compositor.hpp>

namespace eng::ui {

class DoubleBufferScreen {
public:
	/// Publica el buffer delantero (parcheo de punteros de display). No propietario.
	using PublishFn = eng::util::FunctionRef<void()>;

	/// Un buffer de pantalla: lienzo planar contiguo + `Surface` de dibujo.
	struct Buffer {
		eng::field::ContiguousPlayfield playfield {};
		eng::field::Surface surface {};
		bool valid = false;
	};

	/// Enlaza los dos buffers (memoria del llamador, planos contiguos).
	bool bind(eng::u8* mem_a, eng::u8* mem_b, eng::u32 bytes, eng::u16 w, eng::u16 h,
		  eng::u8 depth) noexcept {
		m_width = w;
		m_height = h;
		const bool a = bind_buffer(m_buffers[0], mem_a, bytes, w, h, depth);
		const bool b = bind_buffer(m_buffers[1], mem_b, bytes, w, h, depth);
		m_back = 0u;
		return a && b;
	}

	[[nodiscard]] bool valid() const noexcept {
		return m_buffers[0].valid && m_buffers[1].valid;
	}
	[[nodiscard]] eng::u16 width() const noexcept { return m_width; }
	[[nodiscard]] eng::u16 height() const noexcept { return m_height; }

	/// Buffer donde se compone (el display NO lo muestra todavía).
	[[nodiscard]] eng::field::Surface& back() noexcept { return m_buffers[m_back].surface; }
	/// Buffer que el display está mostrando.
	[[nodiscard]] eng::field::Surface& front() noexcept {
		return m_buffers[static_cast<eng::u8>(1u - m_back)].surface;
	}

	void set_publisher(PublishFn fn) noexcept { m_publish = fn; }

	/// Intercambia delantero/trasero y publica el nuevo delantero (si hay publisher).
	void flip() noexcept {
		m_back = static_cast<eng::u8>(1u - m_back);
		if (m_publish.valid()) {
			m_publish();
		}
	}

	/// Compone en el buffer trasero y publica. Es el paso por frame de la UI.
	void present(Compositor& compositor, eng::graphics::FramePlan& plan) noexcept {
		compositor.set_screen(back());
		compositor.present_blit(plan);
		flip();
	}

	/// Igual, sin plan (composición CPU): para host y demos sin `FramePlan`.
	void present(Compositor& compositor) noexcept {
		compositor.set_screen(back());
		compositor.present();
		flip();
	}

private:
	static bool bind_buffer(Buffer& b, eng::u8* mem, eng::u32 bytes, eng::u16 w, eng::u16 h,
				eng::u8 depth) noexcept {
		if (mem == nullptr || !b.playfield.bind_raw(mem, bytes, w, h, depth)) {
			return false;
		}
		b.surface = eng::field::Surface {b.playfield, eng::field::SurfaceRect {0, 0, w, h}};
		b.valid = true;
		return true;
	}

	Buffer m_buffers[2] {};
	eng::u8 m_back = 0u;
	eng::u16 m_width = 0u;
	eng::u16 m_height = 0u;
	PublishFn m_publish {};
};

} // namespace eng::ui
