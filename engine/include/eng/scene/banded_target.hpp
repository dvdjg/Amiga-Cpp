#pragma once

/// \file banded_target.hpp
/// **Dibujo split-aware**: reparte un rectángulo de pantalla entre las **bandas** de un display y
/// llama al callback con cada trozo ya recortado (`clip_to_band`). Es el primitivo con el que un
/// `fill`, un BOB o un gráfico por CPU se **reparten por banda** (cada trozo va a la superficie de su
/// campo), sin que el juego lo gestione —«la gracia del engine». Etapa 4 del planner
/// (`ROADMAP_GAME_API.md` §7).
///
/// Con un solo campo (`Single`) hay una banda y el trozo es el rect entero; con bandas apiladas
/// (`Bands`/split-screen) el rect que cruza la línea se parte en dos y cada mitad se dirige a su
/// banda. Es **puro** (geometría + callback), host-testable con las bandas de `plan_bands`.

#include <eng/core/types/box.hpp>
#include <eng/core/types/span.hpp>
#include <eng/scene/band_plan.hpp>

namespace eng::scene {

/// Llama `fn(band_index, part)` por cada banda que solape `screen`, con `part` = `screen` recortado
/// a esa banda (`clip_to_band`). No llama nada para bandas sin solape.
template <class Fn>
void for_each_band_part(const eng::Box& screen, eng::Span<const BandSpan> bands, Fn&& fn) {
	for (eng::u16 i = 0u; i < static_cast<eng::u16>(bands.size()); ++i) {
		const eng::Box part = clip_to_band(screen, bands[i]);
		if (!part.empty()) {
			fn(i, part);
		}
	}
}

} // namespace eng::scene
