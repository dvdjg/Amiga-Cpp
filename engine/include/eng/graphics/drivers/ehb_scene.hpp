#pragma once

/// \file ehb_scene.hpp
/// Primer driver reutilizable para escenas Extra Half-Brite.
///
/// `StaticEhbScene` es deliberadamente pequeno, pero ya representa la direccion
/// arquitectonica del engine:
///
/// - El juego no escribe registros custom.
/// - El juego no calcula punteros BPLx ni palabras magicas de Copper.
/// - El juego pide una superficie EHB estatica, rellena sus bitplanes y describe
///   zonas de paleta de alto nivel.
/// - El driver traduce esa intencion a memoria Chip, bitplane DMA y copperlist.
///
/// Extra Half-Brite usa 6 bitplanes lowres. Los cinco primeros seleccionan los
/// 32 registros `COLOR00..COLOR31`; el sexto bitplane suma 32 al indice y hace que
/// Agnus/Denise muestren ese color a media intensidad. Es un modo muy atractivo
/// para aventuras graficas y escenas ricas porque duplica la gama percibida sin
/// duplicar los registros fisicos de paleta.

#include <eng/core/types.hpp>
#include <eng/graphics/copper/scheduler.hpp>
#include <eng/graphics/driver.hpp>
#include <eng/graphics/frame_plan.hpp>
#include <eng/memory/arena.hpp>

namespace eng::graphics::drivers {

/// Paleta fisica EHB.
///
/// Aunque visualmente hay 64 indices, solo existen 32 registros fisicos. El engine
/// habla en terminos de esta paleta base; los colores half-brite se obtienen por
/// hardware usando el sexto bitplane.
struct EhbPalette {
	u16 color[32] {};
};

/// Cambio de paleta en una linea concreta.
///
/// `line` usa el mismo espacio que `Copper::wait_line`: valores de raster visibles
/// aproximados en PAL lowres. `palette` debe apuntar a datos vivos durante la
/// construccion de la copperlist. Despues de construirla, el Copper ya contiene
/// copias de los 32 valores RGB444.
struct EhbPaletteZone {
	u8 line = 0;
	const EhbPalette* palette = nullptr;
};

/// Configuracion de display EHB 320x256.
///
/// Esta version fija el formato para mantener el primer driver auditable. Mas
/// adelante apareceran variantes con overscan, split screen, scroll fino y buffers
/// dobles, pero la demo base no debe pagar esa complejidad todavia.
struct StaticEhbSceneConfig {
	const EhbPalette* base_palette = nullptr;
	const EhbPaletteZone* zones = nullptr;
	u8 zone_count = 0;
	u32 copper_bytes = 1024;
};

/// Superficie EHB estatica con copperlist propia.
///
/// La clase no reserva memoria por si misma al sistema operativo. Recibe una
/// `MemorySystem` ya creada por el backend y toma bloques de Chip RAM desde ahi.
/// Esto mantiene visible la restriccion clave del Amiga: bitplanes y copperlists
/// deben estar en Chip RAM para que el chipset pueda leerlos por DMA.
class StaticEhbScene {
public:
	static constexpr GraphicsDriverId id = GraphicsDriverId::EhbScene;
	static constexpr u16 width = 320;
	static constexpr u16 height = 256;
	static constexpr u16 bytes_per_row = width / 8;
	static constexpr u8 plane_count = 6;
	static constexpr u32 plane_bytes = static_cast<u32>(bytes_per_row) * height;
	static constexpr u32 bitplane_bytes = plane_bytes * plane_count;
	static constexpr u8 max_palette_zone_bindings = 8;

	/// Reserva bitplanes y copperlist en Chip RAM y construye la copperlist.
	///
	/// El contenido de los bitplanes queda a cero por la politica actual de
	/// `MinimalBackend::configure_memory`, que usa `MEMF_CLEAR`. El llamador puede
	/// obtener `bitplanes()` y copiar/escribir assets planares cocinados.
	bool init(MemorySystem& memory, const StaticEhbSceneConfig& config) {
		m_bitplane_block = memory.chip.allocate(bitplane_bytes, 16);
		m_copper_block = memory.chip.allocate(config.copper_bytes, 16);
		m_bitplanes = static_cast<u8*>(m_bitplane_block.data);

		if (!m_bitplane_block.valid() || !m_copper_block.valid() || config.base_palette == nullptr) {
			m_ok = false;
			return false;
		}

		return rebuild_copper(config);
	}

	/// Reconstruye la copperlist sobre la memoria Chip ya reservada.
	///
	/// Este metodo es el puente minimo hacia efectos runtime. Un ciclo de paleta,
	/// una luz o una transicion pueden generar una paleta temporal y pedir al driver
	/// que regenere la lista sin volver a reservar bitplanes. Es mas caro que un
	/// parche incremental, pero es perfecto para las primeras pruebas porque deja
	/// toda la lista final visible y auditable.
	bool rebuild_copper(const StaticEhbSceneConfig& config) {
		if (!m_bitplane_block.valid() || !m_copper_block.valid() || config.base_palette == nullptr) {
			m_ok = false;
			return false;
		}

		m_base_palette_value_word = 0;
		m_zone_binding_count = 0;

		copper::Scheduler scheduler { m_copper_block };
		// 320x256 lowres PAL, 6 planos EHB (geometrÃ­a paramÃ©trica del scheduler).
		scheduler.emit_planes_display(0x2c81, 0x2cc1, 0x0038, 0x00d0, 40u, 0x6200, 6, m_bitplanes, plane_bytes);
		m_base_palette_value_word = static_cast<u16>(scheduler.words_used() + 1u);
		scheduler.emit_palette(config.base_palette->color);
		for (u8 i = 0; i < config.zone_count; ++i) {
			const EhbPaletteZone& zone = config.zones[i];
			if (zone.palette != nullptr) {
				if (m_zone_binding_count < max_palette_zone_bindings) {
					m_zone_bindings[m_zone_binding_count++] = {
						zone.line,
						static_cast<u16>(scheduler.words_used() + 3u),
					};
				}
				scheduler.emit_palette_zone(zone.line, zone.palette->color);
			}
		}

		scheduler.wait_line(0xf8);
		scheduler.move(copper::Register::COLOR00, 0x0000);
		scheduler.end();

		m_copper_words = scheduler.words_used();
		m_copper_words_ptr = scheduler.data();
		m_copper_report = scheduler.report();
		m_ok = scheduler.ok();
		return m_ok;
	}

	/// Aplica los parches de paleta de un `FramePlan`.
	///
	/// Este metodo no cambia la estructura de la copperlist: solo sustituye las
	/// words de valor de MOVEs `COLORxx` que ya existen. Es el camino barato para
	/// ciclos de paleta y luces. Si un efecto necesitase anadir nuevos `WAIT` o
	/// nuevos registros, entonces si habria que recompilar la lista o usar doble
	/// buffer de copperlists.
	bool apply_frame_plan(const FramePlan& plan) {
		if (!m_ok || !plan.ok()) {
			return false;
		}

		for (u8 i = 0; i < plan.palette_patch_count(); ++i) {
			const PalettePatch& patch = plan.palette_patch(i);
			if (patch.target == PalettePatchTarget::Base) {
				if (!patch_palette_values(m_base_palette_value_word, patch.first, patch.count, patch.colors)) {
					return false;
				}
			} else {
				const u16 value_word = find_zone_palette_value_word(patch.line);
				if (value_word == 0 || !patch_palette_values(value_word, patch.first, patch.count, patch.colors)) {
					return false;
				}
			}
		}

		return true;
	}

	/// Instala la copperlist del driver.
	///
	/// Es un template para no introducir una interfaz virtual de backend. Cualquier
	/// plataforma futura que quiera ejecutar este driver debera exponer un metodo
	/// compatible o adaptar el driver en su propia capa.
	template <typename Backend>
	void install(Backend& backend) const {
		if (m_ok && m_copper_words_ptr != nullptr) {
			backend.install_copper_list(m_copper_words_ptr);
		}
	}

	/// Hooks de driver para encajar con el contrato `GraphicsDriver`.
	void begin_frame(RenderContext&) {}
	void end_frame(RenderContext&) {}

	constexpr bool ok() const { return m_ok; }
	constexpr u8* bitplanes() const { return m_bitplanes; }
	constexpr u16 copper_words() const { return m_copper_words; }
	constexpr const u16* copper_words_ptr() const { return m_copper_words_ptr; }
	constexpr const copper::ScheduleReport& copper_report() const { return m_copper_report; }

private:
	struct PaletteBinding {
		u8 line = 0;
		u16 first_value_word = 0;
	};

	bool patch_palette_values(u16 first_value_word, u8 first, u8 count, const u16* colors) {
		if (first_value_word == 0 || colors == nullptr || first >= 32u) {
			return false;
		}
		if (first + count > 32u) {
			count = static_cast<u8>(32u - first);
		}

		u16* words = static_cast<u16*>(m_copper_block.data);
		for (u8 i = 0; i < count; ++i) {
			const u16 word_index = static_cast<u16>(first_value_word + static_cast<u16>(first + i) * 2u);
			if (word_index >= m_copper_words) {
				return false;
			}
			words[word_index] = colors[first + i];
		}
		return true;
	}

	u16 find_zone_palette_value_word(u8 line) const {
		for (u8 i = 0; i < m_zone_binding_count; ++i) {
			if (m_zone_bindings[i].line == line) {
				return m_zone_bindings[i].first_value_word;
			}
		}
		return 0;
	}

	MemoryBlock m_bitplane_block {};
	MemoryBlock m_copper_block {};
	u8* m_bitplanes = nullptr;
	const u16* m_copper_words_ptr = nullptr;
	copper::ScheduleReport m_copper_report {};
	PaletteBinding m_zone_bindings[max_palette_zone_bindings] {};
	u16 m_base_palette_value_word = 0;
	u16 m_copper_words = 0;
	u8 m_zone_binding_count = 0;
	bool m_ok = false;
};

} // namespace eng::graphics::drivers
