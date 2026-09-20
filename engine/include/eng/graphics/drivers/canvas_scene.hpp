#pragma once

/// \file canvas_scene.hpp
/// Driver **planar con `Surface`**: cierra el hueco «efecto → dibujo con `Surface`» sin
/// renunciar a la composition del display.
///
/// Un `CanvasScene` envuelve un `field::CanvasPlayfield` (bitmap planar *interleaved*,
/// un lienzo sin tiles ni scroll) y le añade la copperlist de display (DMACON, BPLCON0,
/// modulos interleaved, DIW/DDF y punteros BPLx), exponiendo:
///
/// - `surface()`: el contexto de dibujo con clip (`field::Surface`) para rellenar caras,
///   lineas, rectangulos y texto.
/// - `takeover()`/`install()`: el contrato de driver (swap de copperlist).
/// - `bitplanes()`/`bitmap()`: por si el efecto prefiere escribir el bitmap a mano.
///
/// Es el puente entre las dos lineas del engine: los **drivers** de efecto daban
/// `bitplanes()` sin `Surface` y los **playfields** dan `Surface`
/// sin composition multi-buffer. `CanvasScene` da `Surface` con display propio.
///
/// Para doble/triple buffer se usa `MultiBuffered<CanvasScene, N>`: `CanvasScene` ofrece
/// `bind()` (planos + copperlist ya reservados) y `bitplane_bytes_for()` sin poseer la
/// memoria, y `CanvasPlayfield::bind()` acepta bitplanes externos. El llamador dibuja en
/// `back().surface()` y publica con `commit()`.

#include <eng/core/domains.hpp>
#include <eng/core/types.hpp>
#include <eng/field/playfield.hpp>
#include <eng/field/surface.hpp>
#include <eng/graphics/copper/copper.hpp>
#include <eng/graphics/copper/scheduler.hpp>
#include <eng/graphics/driver.hpp>
#include <eng/memory/arena.hpp>

namespace eng::graphics::drivers {

/// Configuracion de una escena-planar con `Surface`.
struct CanvasSceneConfig {
	u16 width = 320;      ///< ancho visible en pixeles (multiplo de 16)
	u16 height = 256;     ///< filas del bitmap
	u8 planes = 4;        ///< planos de bitplane (4 = 16 colores, 5/6 = mas)

	u16 diwstrt = 0x2c81;
	u16 diwstop = 0x2cc1;
	u16 ddfstrt = 0x0038;
	u16 ddfstop = 0x00d0;

	/// `BPLCON0` (BPU + bits de modo). 4 planos color = `0x4200`; EHB = `0x6200`.
	u16 bplcon0 = 0x4200;

	/// Paleta opcional cargada al principio de la lista.
	eng::PaletteWords palette {};
	u8 palette_first = 0;
	u8 palette_count = 0;

	/// Tamano del bloque Chip de la copperlist.
	u32 copper_bytes = 4096;
};

/// Lienzo planar con display (copperlist) y `Surface` de dibujo.
class CanvasScene {
public:
	static constexpr GraphicsDriverId id = GraphicsDriverId::CanvasScene;

	/// Bytes por fila del bitmap (interleaved, `(width/8) & ~1`).
	static constexpr u32 row_bytes_for(const CanvasSceneConfig& config) {
		return static_cast<u32>(config.width / 8u) & ~1u;
	}
	/// Bytes de un plano completo (todas las filas del bitmap interleaved).
	static constexpr u32 plane_bytes_for(const CanvasSceneConfig& config) {
		return row_bytes_for(config) * static_cast<u32>(config.height);
	}
	/// Bytes de TODOS los planos (interleaved: cada fila lleva `planes*row` bytes).
	static constexpr u32 bitplane_bytes_for(const CanvasSceneConfig& config) {
		return row_bytes_for(config) * static_cast<u32>(config.planes) * static_cast<u32>(config.height);
	}

	/// Reserva el bitmap (interleaved) + la copperlist en Chip RAM y construye la lista.
	bool init(MemorySystem& memory, const CanvasSceneConfig& config) {
		return bind(memory.chip.allocate_block<eng::PlaneTag>(bitplane_bytes_for(config) + 16u, 16),
			    memory.chip.allocate_block<eng::CopperTag>(config.copper_bytes, 16), config);
	}

	/// Construye sobre bloques **ya reservados** (mismo contrato que `init`, sin reservar
	/// memoria). Es lo que usa `MultiBuffered<CanvasScene, N>` para repartir N buffers.
	bool bind(eng::Block<eng::PlaneTag> bitplanes, eng::Block<eng::CopperTag> copper,
		  const CanvasSceneConfig& config) {
		m_config = config;
		m_copper_block = copper;
		if (!m_copper_block.valid()) {
			m_ok = false;
			return false;
		}
		if (!m_playfield.bind(bitplanes, field::CanvasPlayfield::Config {config.width, config.height, config.planes})) {
			m_ok = false;
			return false;
		}
		return rebuild_copper(config);
	}

	/// Reconstruye la copperlist (misma geometria; util si cambia la paleta).
	bool rebuild_copper(const CanvasSceneConfig& config) {
		if (!m_playfield.initialized() || !m_copper_block.valid()) {
			m_ok = false;
			return false;
		}
		const field::PlayfieldHardwareView hv = m_playfield.hardware_view();
		copper::Scheduler sched { m_copper_block };
		sched.move(copper::Register::DMACON,
			   static_cast<u16>(copper::DmaSetClear | copper::DmaMaster |
					     copper::DmaCopper | copper::DmaBitplane));
		sched.move(copper::Register::BPLCON0, config.bplcon0);
		sched.move(copper::Register::BPLCON1, 0x0000);
		sched.move(copper::Register::BPLCON2, 0x0000);
		// Bitmap interleaved: cada fila lleva los `planes` planos; el modulo retrocede
		// una fila menos un plano (`planes*row - row`). Lo calcula el playfield.
		sched.move(copper::Register::BPL1MOD, hv.bpl1mod);
		sched.move(copper::Register::BPL2MOD, hv.bpl2mod);
		sched.move(copper::Register::DIWSTRT, config.diwstrt);
		sched.move(copper::Register::DIWSTOP, config.diwstop);
		sched.move(copper::Register::DDFSTRT, config.ddfstrt);
		sched.move(copper::Register::DDFSTOP, config.ddfstop);
		const eng::u8* base = hv.bitplanes;
		const u32 row = hv.bitmap_bytes_per_row;
		for (u8 plane = 0; plane < config.planes; ++plane) {
			const eng::uintptr addr =
				reinterpret_cast<eng::uintptr>(base + static_cast<u32>(plane) * row);
			sched.move_bitplane_pointer(plane, eng::ChipAddress {addr});
		}
		if (!config.palette.empty() && config.palette_count != 0u) {
			sched.emit_palette(config.palette, config.palette_first, config.palette_count);
		}
		sched.end();

		m_copper_words = sched.words_used();
		m_copper_ptr = sched.data();
		m_report = sched.report();
		m_ok = sched.ok();
		return m_ok;
	}

	/// Toma el control del display mostrando la copperlist del lienzo (una vez).
	template <typename Backend>
	void takeover(Backend& backend) const {
		if (m_ok && m_copper_ptr != nullptr) backend.takeover_display(m_copper_ptr);
	}
	/// Instala la copperlist del lienzo (swap de puntero COP1LC; no toma el control).
	template <typename Backend>
	void install(Backend& backend) const {
		if (m_ok && m_copper_ptr != nullptr) backend.install_copper_list(m_copper_ptr);
	}

	/// Hooks de driver para encajar con el contrato `GraphicsDriver`.
	void begin_frame(RenderContext&) {}
	void end_frame(RenderContext&) {}

	/// Contexto de dibujo (clip = toda la pantalla). El llamador dibuja con `Surface`:
	/// `set_pixel`/`fill_rect`/`draw_line`/`fill_polygon` sin tocar planos ni registros.
	field::Surface surface() {
		return field::Surface {m_playfield,
				       field::SurfaceRect {0, 0, m_config.width, m_config.height}};
	}

	const field::CanvasPlayfield& playfield() const { return m_playfield; }
	field::CanvasPlayfield& playfield() { return m_playfield; }
	/// Planos del lienzo (interleaved de la escena).
	[[nodiscard]] constexpr eng::PlaneBytes bitplanes() const { return m_playfield.bitplanes(); }
	constexpr bool ok() const { return m_ok; }
	constexpr u16 copper_words() const { return m_copper_words; }
	constexpr const u16* copper_words_ptr() const { return m_copper_ptr; }
	constexpr const copper::ScheduleReport& copper_report() const { return m_report; }

private:
	CanvasSceneConfig m_config {};
	field::CanvasPlayfield m_playfield {};
	eng::Block<eng::CopperTag> m_copper_block {};
	const u16* m_copper_ptr = nullptr;
	copper::ScheduleReport m_report {};
	u16 m_copper_words = 0;
	bool m_ok = false;
};

} // namespace eng::graphics::drivers
