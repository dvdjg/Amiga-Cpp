#pragma once

/// \file copper_chunky.hpp
/// **Copper chunky**: display sin bitplanes en el que el Copper escribe `COLOR00` por bloques
/// de `block_h` líneas a lo largo de cada fila, re-ejecutando la misma "línea de color" vía
/// `COP2LC`/`COPJMP2` y saliendo con un `SKIP`. Porte de `effects/plasma`.
///
/// Se usa con una escena en modo `SceneMode::CopperChunky` (`planes == 0`): la capa emite la
/// lista en el bloque **inactivo** del `copper::Plan` y expone los `data` de cada `COLOR00`
/// (`row()`), de modo que el efecto escribe los colores del frame. El bucle es:
///
/// ```cpp
/// scene.begin_build();
/// layer.emit(scene.scheduler(), scene.inactive_words());
/// for (y...) { u16* p = layer.row(y); for (x...) { p[0] = rgb; p += 2; } }
/// scene.end_build();
/// scene.present(backend);   // publica la copperlist nueva
/// ```
///
/// Estructura de la lista, por fila (verbatim de `MakeCopperList`):
/// `MOVE32 COP2LC -> label` ; `label: WAIT Y(y*block_h),X(-4)` ; `cols+1` x `MOVE COLOR00` ;
/// `SKIP Y(y*block_h+3),LASTHP` ; `MOVE COPJMP2`.

#include <eng/core/types.hpp>
#include <eng/graphics/copper/scheduler.hpp>

namespace eng::graphics::composition {

/// Configuración del display copper chunky.
struct CopperChunkyConfig {
	eng::u8 cols = 36;          ///< bloques por fila (HTILES)
	eng::u8 rows = 64;          ///< filas de bloques (VTILES)
	eng::u8 block_h = 4;        ///< líneas de barrido por fila
	eng::u16 first_line = 0x2cu;///< `Y(0)` (VPOS del primer WAIT)
	eng::u16 label_hpos = 0x84u;///< `X(-4)` en píxeles lowres (0x88-4, con DIWHP=0x88)
	eng::u16 skip_hpos = 0x1bcu;///< `LASTHP` en color-clock (fin de la re-ejecución)
};

/// Emite la lista copper-chunky sobre un `copper::Scheduler` y expone los slots de color.
template <eng::u8 MaxCols = 64, eng::u8 MaxRows = 64>
class CopperChunkyLayer {
public:
	[[nodiscard]] static constexpr eng::u8 max_cols() { return MaxCols; }
	[[nodiscard]] static constexpr eng::u8 max_rows() { return MaxRows; }

	/// Fija la geometría del display (`cols`/`rows`/`block_h`); `false` si no cabe en la
	/// capacidad de la capa (`MaxCols`/`MaxRows`).
	[[nodiscard]] bool init(CopperChunkyConfig cfg) {
		if (cfg.cols == 0u || cfg.rows == 0u || cfg.cols > MaxCols || cfg.rows > MaxRows) {
			m_ok = false;
			return false;
		}
		m_cfg = cfg;
		m_ok = true;
		return true;
	}

	/// Emite la lista completa en el bloque que emite `s` (`base` = `Scene::inactive_words()`).
	/// Guarda el índice de cada `COLOR00` para `row()`. Reejecutable cada frame.
	void emit(copper::Scheduler& s, const eng::u16* base) {
		if (!m_ok) {
			return;
		}
		m_base = base;
		s.wait_raw(m_cfg.first_line, 0u, 0xfffeu); // CopWait(Y(0), HP(0))
		for (eng::u16 y = 0; y < m_cfg.rows; ++y) {
			const eng::u16 row_line = static_cast<eng::u16>(
				static_cast<eng::u16>(y) * m_cfg.block_h + m_cfg.first_line);
			const eng::u16 cop2lc = s.move32(copper::Register::COP2LCH, eng::ChipAddress {});
			const eng::u16 label = s.wait_masked(
				static_cast<eng::u16>(row_line & 128u), m_cfg.label_hpos, 0u, 255u);
			s.patch_move32(cop2lc, s.instruction_address(label));
			for (eng::u16 x = 0; x <= m_cfg.cols; ++x) {
				const eng::u16 ins = s.move_at(copper::Register::COLOR00, 0u);
				if (x < m_cfg.cols) {
					m_slot[static_cast<eng::u16>(y) * MaxCols + x] = ins;
				}
			}
			s.skip(static_cast<eng::u16>(row_line + m_cfg.block_h - 1u), m_cfg.skip_hpos);
			s.move(copper::Register::COPJMP2, 0u);
		}
		// El cierre de la lista lo hace `Scene::end_build` (Plan::end_frame -> `Scheduler::end`).
	}

	/// Puntero al `data` del bloque 0 de la fila `row` en la lista recién emitida (válido hasta
	/// el siguiente `emit`). Para rellenar la fila de golpe se escriben los `cols` colores con
	/// **paso de 2 words** (cada `COLOR00` son `[registro, data]`): `p[0] = c; p += 2;`.
	[[nodiscard]] eng::u16* row(eng::u8 r) const {
		if (!m_ok || m_base == nullptr || r >= m_cfg.rows) {
			return nullptr;
		}
		return const_cast<eng::u16*>(m_base + m_slot[static_cast<eng::u16>(r) * MaxCols] + 1u);
	}

	[[nodiscard]] constexpr eng::u8 cols() const { return m_cfg.cols; }
	[[nodiscard]] constexpr eng::u8 rows() const { return m_cfg.rows; }
	[[nodiscard]] constexpr bool ok() const { return m_ok; }

private:
	CopperChunkyConfig m_cfg {};
	const eng::u16* m_base = nullptr; ///< base del bloque emitido (para `row()`)
	eng::u16 m_slot[MaxRows * MaxCols] {}; ///< índice de la instrucción `COLOR00` de (r,c)
	bool m_ok = false;
};

} // namespace eng::graphics::composition
