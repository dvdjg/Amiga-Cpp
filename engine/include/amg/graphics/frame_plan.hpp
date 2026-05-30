#pragma once

/// \file frame_plan.hpp
/// Descripcion minima de trabajos de render para un frame.
///
/// Este archivo empieza a separar tres niveles:
///
/// - la logica de juego/effectos dice que quiere cambiar;
/// - el `FramePlan` recoge esas intenciones de forma portable;
/// - el driver Amiga decide si las materializa como parches de copperlist, blits,
///   sprites hardware, CPU writes o cualquier otro mecanismo.
///
/// De momento solo contiene parches de paleta porque es el siguiente cuello real:
/// `PaletteCycleEffect` no deberia reconstruir toda una copperlist cuando solo
/// cambian varios `COLORxx`. El mismo patron se ampliara con BOBs, sprites,
/// scroll de tiles, prioridades y efectos raster.

#include <amg/core/types.hpp>

namespace amg::graphics {

/// Lugar logico donde se aplicara un parche de paleta.
enum class PalettePatchTarget : u8 {
	Base,
	Zone,
};

/// Cambio de uno o varios colores fisicos RGB444.
///
/// `colors` apunta a una paleta completa de 32 entradas; `first/count` selecciona
/// el tramo que se quiere aplicar. La razon de apuntar a la paleta completa es
/// mantener el contrato igual que `CopperScheduler::emit_palette()`: un efecto
/// puede pedir "actualiza colores 1..7" sin crear arrays temporales.
struct PalettePatch {
	PalettePatchTarget target = PalettePatchTarget::Base;
	u8 line = 0;
	u8 first = 0;
	u8 count = 0;
	const u16* colors = nullptr;
};

/// Plan de render de un frame.
///
/// Es un contenedor fijo, sin heap y sin STL. Cuando se llene, `ok()` pasa a false
/// para que la demo o el driver pueda fallar de forma controlada. En un engine de
/// Amiga es mejor rechazar un plan demasiado grande que degradarse en silencio y
/// descubrir corrupcion visual varios sistemas mas tarde.
class FramePlan {
public:
	static constexpr u8 max_palette_patches = 8;

	void clear() {
		m_palette_patch_count = 0;
		m_ok = true;
	}

	bool add_base_palette_patch(const u16* colors, u8 first = 0, u8 count = 32) {
		return add_palette_patch({PalettePatchTarget::Base, 0, first, count, colors});
	}

	bool add_zone_palette_patch(u8 line, const u16* colors, u8 first = 0, u8 count = 32) {
		return add_palette_patch({PalettePatchTarget::Zone, line, first, count, colors});
	}

	constexpr bool ok() const { return m_ok; }
	constexpr u8 palette_patch_count() const { return m_palette_patch_count; }

	constexpr const PalettePatch& palette_patch(u8 index) const {
		return m_palette_patches[index];
	}

private:
	bool add_palette_patch(PalettePatch patch) {
		if (patch.colors == nullptr || patch.first >= 32u || patch.count == 0u) {
			m_ok = false;
			return false;
		}
		if (patch.first + patch.count > 32u) {
			patch.count = static_cast<u8>(32u - patch.first);
		}
		if (m_palette_patch_count >= max_palette_patches) {
			m_ok = false;
			return false;
		}

		m_palette_patches[m_palette_patch_count++] = patch;
		return true;
	}

	PalettePatch m_palette_patches[max_palette_patches] {};
	u8 m_palette_patch_count = 0;
	bool m_ok = true;
};

} // namespace amg::graphics
