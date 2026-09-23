#pragma once

/// \file animation.hpp
/// Modelo de contenido de sprites/animaciones, INDEPENDIENTE de cómo se materialicen
/// (sprite hardware, BOB, CPU o playfield; lo decide el engine). El avance es por
/// TIEMPO DE JUEGO fijo (determinista), no por frame de vídeo.
///
/// Ver `docs/engine/architecture/CONTENT_AND_TILEMAP.md` §3.

#include <eng/core/types/span.hpp>
#include <eng/core/types/types.hpp>

namespace eng::graphics {

/// Celda de un sprite sheet.
struct Frame {
	eng::u16 x = 0, y = 0, w = 0, h = 0;  // rectángulo en el sheet (píxeles)
	eng::u16 ticks = 1;                    // duración en ticks de juego (>= 1)
	eng::u16 event = 0;                    // evento opcional (p. ej. sonido); 0 = ninguno
};

/// Secuencia de frames con avance determinista por ticks.
struct Animation {
	eng::Span<const Frame> frames {};
	bool loop = true;

	struct State {
		eng::u16 index = 0;
		eng::u16 elapsed = 0;   // ticks consumidos del frame actual
		bool finished = false;
	};

	void reset(State& s) const { s = State {}; }

	/// Avanza `ticks` ticks. Devuelve true si el frame actual cambió.
	bool advance(State& s, eng::u16 ticks) const {
		if (frames.empty() || s.finished) return false;
		const eng::u16 before = s.index;
		eng::u32 remaining = ticks;
		while (remaining > 0u) {
			const eng::u16 dur = frames.at(s.index).ticks ? frames.at(s.index).ticks : 1u;
			const eng::u16 left = static_cast<eng::u16>(dur - s.elapsed);
			if (remaining < left) {
				s.elapsed = static_cast<eng::u16>(s.elapsed + remaining);
				break;
			}
			remaining -= left;
			s.elapsed = 0;
			if (static_cast<eng::u32>(s.index) + 1u < frames.size()) {
				++s.index;
			} else if (loop) {
				s.index = 0;
			} else {
				s.finished = true;
				break;
			}
		}
		return s.index != before;
	}

	const Frame& current(const State& s) const { return frames.at(s.index); }
};

/// Sprite sheet: píxeles planares + metadatos; identidad de contenido (vista sobre
/// un blob UAF, sin copia). `pixels` mide `width * height * planes` en planos
/// contiguos.
struct SpriteSheet {
	eng::Span<const eng::u16> pixels {};
	eng::u16 width = 0, height = 0;
	eng::u8 planes = 0;
};

} // namespace eng::graphics
