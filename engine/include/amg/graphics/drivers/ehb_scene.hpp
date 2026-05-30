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

#include <amg/core/types.hpp>
#include <amg/graphics/copper/copper.hpp>
#include <amg/graphics/driver.hpp>
#include <amg/memory/arena.hpp>

namespace amg::graphics::drivers {

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

		copper::ListBuilder copper { m_copper_block };

		// Aseguramos que el estado de DMA queda bajo control del driver. Demos
		// anteriores podian depender accidentalmente del estado de AmigaDOS; un
		// driver reutilizable no debe hacerlo.
		copper.move(
			copper::Register::DMACON,
			static_cast<u16>(copper::DmaSetClear | copper::DmaMaster | copper::DmaCopper | copper::DmaBitplane)
		);

		// BPU=6 activa seis bitplanes. El bit color mantiene salida color OCS. HAM
		// queda apagado, por tanto Denise interpreta el sexto bitplane como EHB.
		copper.move(copper::Register::BPLCON0, 0x6200);
		copper.move(copper::Register::BPLCON1, 0x0000);
		copper.move(copper::Register::BPLCON2, 0x0000);
		copper.move(copper::Register::BPL1MOD, 0x0000);
		copper.move(copper::Register::BPL2MOD, 0x0000);

		// Ventana PAL lowres 320x256. Estos valores son los mismos que usaban las
		// demos, ahora encapsulados para que el juego no cargue con ellos.
		copper.move(copper::Register::DIWSTRT, 0x2c81);
		copper.move(copper::Register::DIWSTOP, 0x2cc1);
		copper.move(copper::Register::DDFSTRT, 0x0038);
		copper.move(copper::Register::DDFSTOP, 0x00d0);

		for (u8 plane = 0; plane < plane_count; ++plane) {
			copper.move_bitplane_pointer(plane, m_bitplanes + static_cast<u32>(plane) * plane_bytes);
		}

		emit_palette(copper, *config.base_palette);
		for (u8 i = 0; i < config.zone_count; ++i) {
			const EhbPaletteZone& zone = config.zones[i];
			if (zone.palette != nullptr) {
				copper.wait_line(zone.line);
				emit_palette(copper, *zone.palette);
			}
		}

		copper.wait_line(0xf8);
		copper.move(copper::Register::COLOR00, 0x0000);
		copper.end();

		m_copper_words = copper.words_used();
		m_copper_words_ptr = copper.data();
		m_ok = copper.ok();
		return m_ok;
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

private:
	static void emit_palette(copper::ListBuilder& copper, const EhbPalette& palette) {
		for (u8 i = 0; i < 32; ++i) {
			copper.move(copper::color_register(i), palette.color[i]);
		}
	}

	MemoryBlock m_bitplane_block {};
	MemoryBlock m_copper_block {};
	u8* m_bitplanes = nullptr;
	const u16* m_copper_words_ptr = nullptr;
	u16 m_copper_words = 0;
	bool m_ok = false;
};

} // namespace amg::graphics::drivers
