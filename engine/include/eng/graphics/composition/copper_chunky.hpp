#pragma once

/// \file copper_chunky.hpp
/// **Copper chunky**: display sin bitplanes en el que el Copper escribe `COLOR00` por bloques
/// de `block_h` líneas a lo largo de cada fila, re-ejecutando la misma "línea de color" vía
/// `COP2LC`/`COPJMP2` y saliendo con un `SKIP`. Porte de `effects/plasma`.
///
/// `CopperChunkyLayer` encapsula el patrón completo sobre una escena en modo
/// `SceneMode::CopperChunky` (`planes == 0`): la **estructura** de la lista se emite UNA vez
/// en los dos bloques del `copper::Plan`, y por frame el efecto **solo escribe colores** en
/// el bloque inactivo (coste `cols*rows` words/frame, no re-emitir la lista). El API es:
///
/// ```cpp
/// scene::SceneResources res = scene::planar(288, 256, 0);   // sin bitplanes
/// res.mode = scene::SceneMode::CopperChunky;
/// res.copper_bytes = 12288;
/// scene::compose(scene, memory, res, limits);
/// layer.attach(scene, {.cols = 36, .rows = 64});   // estructura en ambos bloques
/// layer.takeover(scene, backend);
/// // por frame:
/// layer.begin_frame(scene);                        // destino = bloque inactivo
/// for (y...) { u16* p = layer.row(y); ... }        // escribe los colores del frame
/// layer.end_frame(scene, backend);                 // flip + install
/// ```
///
/// Estructura de la lista, por fila (verbatim de `MakeCopperList`):
/// `MOVE32 COP2LC -> label` ; `label: WAIT Y(y*block_h),X(-4)` ; `cols+1` x `MOVE COLOR00` ;
/// `SKIP Y(y*block_h+3),LASTHP` ; `MOVE COPJMP2`.

#include <eng/core/types/types.hpp>
#include <eng/graphics/composition/compose.hpp>
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

/// Capa copper-chunky sobre un `Scene`: emite la estructura y expone los colores por fila.
template <eng::u8 MaxCols = 64, eng::u8 MaxRows = 64>
class CopperChunkyLayer {
public:
	[[nodiscard]] static constexpr eng::u8 max_cols() { return MaxCols; }
	[[nodiscard]] static constexpr eng::u8 max_rows() { return MaxRows; }

	/// Prepara la capa sobre `scene` (debe estar en modo `CopperChunky`): emite la estructura
	/// en los **dos** bloques del `Plan`. `false` si la geometría no cabe en la capa.
	[[nodiscard]] bool attach(Scene& scene, CopperChunkyConfig cfg) {
		if (cfg.cols == 0u || cfg.rows == 0u || cfg.cols > MaxCols || cfg.rows > MaxRows) {
			m_ok = false;
			return false;
		}
		m_cfg = cfg;
		m_ok = true;
		scene.begin_build();
		emit(scene.scheduler());
		scene.end_build();
		scene.begin_build();
		emit(scene.scheduler());
		scene.end_build();
		return true;
	}

	/// Toma el display mostrando el bloque activo (una vez, tras `attach`).
	template <typename Backend>
	void takeover(Scene& scene, Backend& backend) const {
		scene.takeover(backend);
	}

	/// Inicia el frame: el destino de `row()` pasa a ser el bloque **inactivo**.
	void begin_frame(Scene& scene) { m_base = scene.inactive_words(); }

	/// Puntero al `data` del bloque 0 de la fila `row` (válido entre `begin_frame`/`end_frame`).
	/// Para rellenar la fila de golpe se escriben los `cols` colores con **paso de 2 words**
	/// (cada `COLOR00` son `[registro, data]`): `p[0] = c; p += 2;`.
	[[nodiscard]] eng::u16* row(eng::u8 r) const {
		if (!m_ok || m_base == nullptr || r >= m_cfg.rows) {
			return nullptr;
		}
		return const_cast<eng::u16*>(m_base + m_slot[static_cast<eng::u16>(r) * MaxCols] + 1u);
	}

	/// Cierra el frame: voltea al bloque recién escrito y lo instala (swap de `COP1LC`).
	template <typename Backend>
	void end_frame(Scene& scene, Backend& backend) {
		scene.flip_copper();
		scene.present(backend);
	}

	[[nodiscard]] constexpr eng::u8 cols() const { return m_cfg.cols; }
	[[nodiscard]] constexpr eng::u8 rows() const { return m_cfg.rows; }
	[[nodiscard]] constexpr bool ok() const { return m_ok; }

private:
	/// Emite la **estructura** de la lista en el bloque que emite `s` y guarda el índice de
	/// cada `COLOR00` (los offsets son los mismos en cualquier bloque). Interno.
	void emit(copper::Scheduler& s) {
		if (!m_ok) {
			return;
		}
		s.wait_raw(m_cfg.first_line, 0u, 0xfffeu); // CopWait(Y(0), HP(0))
		for (eng::u16 y = 0; y < m_cfg.rows; ++y) {
			const eng::u16 row_line = static_cast<eng::u16>(
				static_cast<eng::u16>(y) * m_cfg.block_h + m_cfg.first_line);
			const eng::u16 cop2lc = s.move32(copper::Register::COP2LCH, eng::Address<eng::MemoryKind::Chip> {});
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

	CopperChunkyConfig m_cfg {};
	const eng::u16* m_base = nullptr; ///< bloque destino del frame (inactivo)
	eng::u16 m_slot[MaxRows * MaxCols] {}; ///< índice de la instrucción `COLOR00` de (r,c)
	bool m_ok = false;
};

} // namespace eng::graphics::composition
