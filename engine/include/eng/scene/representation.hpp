#pragma once

/// \file representation.hpp
/// Elección de la REPRESENTACIÓN de un actor (sprite hardware / BOB / playfield / CPU)
/// a partir de su descripción y de un presupuesto de recursos. La aplicación declara
/// QUÉ es un actor; el engine decide CÓMO se materializa y puede reasignarlo.
///
/// Lógica pura y determinista (host-testable). Ver
/// `docs/engine/architecture/SCENE_AND_RESOURCES.md` §2.

#include <eng/core/types.hpp>

namespace eng::scene {

/// Cómo se materializa un actor.
enum class Representation : eng::u8 {
	Cpu = 0,     // dibujado por CPU (marcadores, casos pequeños)
	Sprite = 1,  // sprite hardware
	Bob = 2,     // BOB por Blitter
	Layer = 3,   // capa/playfield propio (objeto grande con scroll; patrón Jim Power)
};

/// Descripción de un actor por parte de la aplicación (sin mecanismo).
struct ActorTemplate {
	eng::u16 width = 0, height = 0;
	eng::u8 planes = 1;
	Representation preferred = Representation::Sprite;
	eng::u8 priority = 0;     // mayor = más prioritario
	bool scrolls = false;     // ¿necesita scroll propio (candidato a Layer)?
};

/// Recursos disponibles para materializar actores.
struct RepresentationBudget {
	eng::u8 sprite_channels = 8;      // canales de sprite libres
	eng::u16 bob_budget_words = 0;    // palabras de Blitter para BOBs
	eng::u8 layer_slots = 0;         // capas promocionables (planos/DPF disponibles)
};

/// Límites de un sprite hardware (ancho fijo 16 px; alto según el caso).
constexpr eng::u16 kSpriteMaxWidth = 16;
constexpr eng::u16 kSpriteMaxHeight = 32;

constexpr bool fits_sprite(const ActorTemplate& a) {
	return a.width > 0u && a.width <= kSpriteMaxWidth && a.height <= kSpriteMaxHeight;
}

/// Elige la representación preferida que quepa; el orden es Layer > Sprite > BOB > CPU.
constexpr Representation choose_representation(const ActorTemplate& a,
                                               const RepresentationBudget& b) {
	if (a.preferred == Representation::Layer && a.scrolls && b.layer_slots > 0u) {
		return Representation::Layer;
	}
	if (fits_sprite(a) && b.sprite_channels > 0u) {
		return Representation::Sprite;
	}
	if (b.bob_budget_words >= a.height) {   // heurística: al menos una palabra por fila
		return Representation::Bob;
	}
	return Representation::Cpu;
}

/// Asignador con estado: elige y CONSUME el recurso; reasigna cuando se agota.
class RepresentationAllocator {
public:
	constexpr void reset(RepresentationBudget budget) { m_budget = budget; }

	constexpr Representation allocate(const ActorTemplate& a) {
		const Representation r = choose_representation(a, m_budget);
		switch (r) {
			case Representation::Layer:
				if (m_budget.layer_slots > 0u) --m_budget.layer_slots;
				break;
			case Representation::Sprite:
				if (m_budget.sprite_channels > 0u) --m_budget.sprite_channels;
				break;
			case Representation::Bob:
				m_budget.bob_budget_words = static_cast<eng::u16>(
					m_budget.bob_budget_words - a.height);
				break;
			case Representation::Cpu:
				break;
		}
		return r;
	}

	constexpr const RepresentationBudget& remaining() const { return m_budget; }

private:
	RepresentationBudget m_budget {};
};

} // namespace eng::scene
