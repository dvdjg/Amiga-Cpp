#pragma once

/// \file dirty.hpp
/// **Lista de regiones sucias** (`eng::ui::DirtyList`) con fusión simple y capacidad fija
/// (sin heap). Cuando se desborda, cae a repintado total. Ver
/// `docs/engine/architecture/GUI_LIBRARY.md` §9.

#include <eng/core/box.hpp>
#include <eng/core/types.hpp>
#include <eng/ui/theme.hpp> // Rect

namespace eng::ui {

/// Región de repintado total (fallback al desbordar la lista). El llamador puede sustituirla por
/// el rect de su pantalla real.
inline constexpr Rect kFullRepaint {0, 0, 320u, 256u};

/// Lista de regiones sucias con fusión al añadir (si solapa con una existente, se fusiona).
template <eng::u8 Max = 8u>
struct DirtyList {
	Rect rects[Max] {};
	eng::u8 count = 0u;

	void clear() noexcept { count = 0u; }

	void add(Rect r) noexcept {
		if (r.empty()) {
			return;
		}
		for (eng::u8 i = 0u; i < count; ++i) {
			if (eng::overlaps(rects[i], r)) {
				rects[i] = eng::merge(rects[i], r);
				return;
			}
		}
		if (count < Max) {
			rects[count++] = r;
		} else {
			rects[0] = kFullRepaint;
			count = 1u;
		}
	}
};

} // namespace eng::ui
