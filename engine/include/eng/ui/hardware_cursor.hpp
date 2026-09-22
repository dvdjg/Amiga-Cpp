#pragma once

/// \file hardware_cursor.hpp
/// **Cursor de ratón por sprite de hardware** (`eng::ui`): un sprite Amiga 16×16 de 1 palabra por
/// línea. Encapsula la **estructura DMA** (POS, CTL, DAT/DATB por línea y terminador) que Agnus
/// lee vía `SPRxPT`, y la emisión de `SPRxPT` + `DMACON` (SPREN) a la copperlist.
///
/// No posee memoria: el llamador le da un buffer de **Chip RAM** con `bind` (los sprites solo ven
/// Chip RAM). El cursor sigue al ratón reescribiendo POS/CTL en la estructura (Agnus la recarga
/// desde `SPRxPT`). Ver `docs/engine/architecture/GUI_LIBRARY.md` y `ROADMAP_GUI.md` (G8).

#include <eng/core/types.hpp>
#include <eng/graphics/copper/scheduler.hpp>

namespace eng::ui {

/// Cursor 16×16 (1 palabra por línea) sobre el par de sprites `kChannel`.
struct HardwareCursor {
	static constexpr eng::u16 kSize = 16u;   ///< lado en píxeles (= número de líneas)
	static constexpr eng::u16 kChannel = 0u; ///< par de sprites (0/1)
	static constexpr eng::u16 kWords = static_cast<eng::u16>(2u + kSize * 2u + 2u); ///< POS+CTL+DAT/DATB+term
	static constexpr eng::u32 kBytes = static_cast<eng::u32>(kWords) * 2u;

	/// Enlaza el cursor a `bytes` de **Chip RAM** (>= `kBytes`) y prepara el terminador DMA.
	bool bind(eng::u8* chip, eng::u32 bytes) noexcept {
		if (chip == nullptr || bytes < kBytes) {
			return false;
		}
		m_base = chip;
		m_words = reinterpret_cast<eng::u16*>(chip);
		m_words[2u + kSize * 2u + 0u] = 0u; // terminador del canal DMA
		m_words[2u + kSize * 2u + 1u] = 0u;
		set_position(0, 0);
		return true;
	}

	/// Fija el bitmap: `dat` = cuerpo (color 1), `datb` = contorno (color 2). `nullptr` deja 0.
	void set_bitmap(const eng::u16* dat, const eng::u16* datb) noexcept {
		if (m_words == nullptr) {
			return;
		}
		for (eng::u16 l = 0u; l < kSize; ++l) {
			m_words[2u + l * 2u + 0u] = (dat != nullptr) ? dat[l] : 0u;
			m_words[2u + l * 2u + 1u] = (datb != nullptr) ? datb[l] : 0u;
		}
	}

	/// Fija la posición (píxel/línea de raster). HSTART se reparte: `X/2` en POS y `X[0]` en CTL.
	void set_position(eng::s16 x, eng::s16 y) noexcept {
		m_x = x;
		m_y = y;
		if (m_words == nullptr) {
			return;
		}
		const eng::u16 ux = static_cast<eng::u16>(x);
		m_words[0] = static_cast<eng::u16>((static_cast<eng::u16>(y) << 8) | ((ux >> 1) & 0xffu));
		m_words[1] = static_cast<eng::u16>(
			(static_cast<eng::u16>(y + static_cast<eng::s16>(kSize)) << 8) | (ux & 1u));
	}

	/// Emite `SPRxPT` → estructura y `DMACON` con SPREN (SPRITE) a la copperlist de la escena.
	void emit_into(eng::copper::Scheduler& s) const noexcept {
		if (m_base == nullptr) {
			return;
		}
		const eng::uintptr sp = reinterpret_cast<eng::uintptr>(m_base);
		const eng::u16 pth = static_cast<eng::u16>(0x120u + kChannel * 4u);
		s.move(pth, static_cast<eng::u16>(sp >> 16));
		s.move(static_cast<eng::u16>(pth + 2u), static_cast<eng::u16>(sp & 0xffffu));
		s.move(eng::copper::Register::DMACON,
		       static_cast<eng::u16>(eng::copper::DmaSetClear | eng::copper::DmaMaster |
					     eng::copper::DmaCopper | eng::copper::DmaBitplane |
					     eng::copper::DmaSprite));
	}

	[[nodiscard]] bool valid() const noexcept { return m_base != nullptr; }
	[[nodiscard]] eng::s16 x() const noexcept { return m_x; }
	[[nodiscard]] eng::s16 y() const noexcept { return m_y; }
	/// Acceso a la estructura DMA (para self-tests; no usar para dibujar).
	[[nodiscard]] eng::u16* words() noexcept { return m_words; }

private:
	eng::u8* m_base = nullptr;
	eng::u16* m_words = nullptr;
	eng::s16 m_x = 0;
	eng::s16 m_y = 0;
};

} // namespace eng::ui
