#pragma once

/// \file copper_chunky.hpp
/// Driver de display **copper chunky** (porte de `effects/plasma`).
///
/// No usa bitplanes: el Copper escribe `COLOR00` por **bloque** de 8x4 a lo largo de cada
/// linea, re-ejecutando la misma "linea de color" `block_h` veces por fila via
/// `COP2LC`/`COPJMP2` y saliendo con un `SKIP` al final de la fila. El efecto rellena la
/// rejilla de colores (`set(row,col,rgb12)`) parcheando el `data` de las instrucciones
/// (equivale a `CopSetColor` por frame): barato para la CPU (~cols*rows words), el Copper
/// trabaja en paralelo.
///
/// Estructura de la lista, por fila (verbatim de `MakeCopperList`):
///   `MOVE32 COP2LC -> label` ; `label: WAIT Y(y*4),X(-4)` ; `cols+1` x `MOVE COLOR00` ;
///   `SKIP Y(y*4+3),LASTHP` ; `MOVE COPJMP2`.

#include <eng/core/types.hpp>
#include <eng/graphics/copper/copper.hpp>
#include <eng/graphics/driver.hpp>
#include <eng/memory/arena.hpp>

namespace eng::graphics::drivers {

/// Configuracion del display copper chunky.
struct CopperChunkyConfig {
	u8 cols = 36;             ///< bloques por fila (HTILES)
	u8 rows = 64;             ///< filas de bloques (VTILES)
	u8 block_h = 4;           ///< lineas de barrido por fila
	u16 first_line = 0x2cu;   ///< `Y(0)` (VPOS del primer WAIT)
	u16 label_hpos = 0x84u;   ///< `X(-4)` en pixeles lowres (0x88-4, con DIWHP=0x88)
	u16 skip_hpos = 0x1bcu;   ///< `LASTHP` en color-clock (fin de la re-ejecucion)
	u32 copper_bytes = 12288u; ///< bloque Chip de la copperlist (~11 KB para 36x64)
};

/// Superficie copper chunky con dos buffers (uno por instancia).
class CopperChunkyScene {
public:
	static constexpr GraphicsDriverId id = GraphicsDriverId::CopperChunky;
	static constexpr u8 max_cols = 64;
	static constexpr u8 max_rows = 64;

	bool init(MemorySystem& memory, const CopperChunkyConfig& config) {
		m_config = config;
		if (config.cols == 0u || config.rows == 0u || config.cols > max_cols || config.rows > max_rows) {
			m_ok = false;
			return false;
		}
		m_copper_block = memory.chip.allocate(config.copper_bytes, 16);
		if (!m_copper_block.valid()) {
			m_ok = false;
			return false;
		}
		return rebuild();
	}

	/// Reconstruye la copperlist sobre el bloque Chip ya reservado.
	bool rebuild() {
		copper::ListBuilder b { m_copper_block };
		b.wait_raw(m_config.first_line, 0u, 0xfffeu); // CopWait(Y(0), HP(0))
		for (u16 y = 0; y < m_config.rows; ++y) {
			const u16 row_line = static_cast<u16>(static_cast<u16>(y) * m_config.block_h + m_config.first_line);
			// `CopMove32(cop2lc, 0)` -> se parchea al label de la fila.
			const u16 cop2lc = b.move32(copper::Register::COP2LCH, nullptr);
			// `label = CopWaitH(Y(y*block_h), X(-4))` (codificacion fiel del original).
			const u16 label = b.wait_masked(static_cast<u16>(row_line & 128u), m_config.label_hpos, 0u, 255u);
			b.patch_move32(cop2lc, b.instruction_address(label));
			// `cols + 1` MOVEs COLOR00 (los `cols` primeros son bloques; el ultimo, fondo).
			for (u16 x = 0; x <= m_config.cols; ++x) {
				const u16 ins = b.move_at(copper::Register::COLOR00, 0u);
				if (x < m_config.cols) {
					m_slot[static_cast<u16>(y) * max_cols + x] = ins;
				}
			}
			// `CopSkip(Y(y*block_h + 3), LASTHP)` y `CopMove16(copjmp2, 0)`.
			b.skip(static_cast<u16>(row_line + m_config.block_h - 1u), m_config.skip_hpos);
			b.move(copper::Register::COPJMP2, 0u);
		}
		b.end();

		m_words = b.data();
		m_words_count = b.words_used();
		m_ok = b.ok();
		return m_ok;
	}

	constexpr u8 cols() const { return m_config.cols; }
	constexpr u8 rows() const { return m_config.rows; }

	/// Fija el color (RGB12) del bloque `(row, col)`.
	void set(u8 row, u8 col, u16 rgb12) {
		if (row >= m_config.rows || col >= m_config.cols || m_words == nullptr) {
			return;
		}
		m_words[m_slot[static_cast<u16>(row) * max_cols + col] + 1u] = rgb12;
	}

	template <typename Backend>
	void takeover(Backend& backend) const {
		if (m_ok && m_words != nullptr) backend.takeover_display(m_words);
	}
	template <typename Backend>
	void install(Backend& backend) const {
		if (m_ok && m_words != nullptr) backend.install_copper_list(m_words);
	}

	void begin_frame(RenderContext&) {}
	void end_frame(RenderContext&) {}

	constexpr bool ok() const { return m_ok; }
	constexpr const u16* copper_words_ptr() const { return m_words; }
	constexpr u16 copper_words() const { return m_words_count; }

private:
	CopperChunkyConfig m_config {};
	MemoryBlock m_copper_block {};
	u16* m_words = nullptr;
	u16 m_words_count = 0;
	u16 m_slot[max_rows * max_cols] {}; ///< indice de la instruccion COLOR00 de (r,c)
	bool m_ok = false;
};

} // namespace eng::graphics::drivers
