#pragma once

#include <eng/core/types/typed.hpp>
#include <eng/graphics/sprite_manager.hpp>

/// \file sprite_bank.hpp
/// `eng::scene::SpriteBank`: banco de sprites HW de capacidad fija. Agrupa los
/// `SpriteConfig` y la vista Chip de DATA que el `SpriteManager` consume; no posee
/// el bloque de memoria (lo aporta la escena/backend) ni crea actores.
///
/// La DATA es propiedad del escenario que reserva el bloque Chip; el banco solo
/// guarda la vista tipada y la rebanada por slot. Así no hay dos dueños del mismo
/// buffer y la API no acepta RAM sin banco.
///
namespace eng::scene {

/// \tparam MaxSprites canales/slots del banco (1..8; el hardware tiene 8 canales).
template <eng::u16 MaxSprites>
class SpriteBank {
	static_assert(MaxSprites > 0u && MaxSprites <= eng::graphics::kSpriteChannels,
		      "SpriteBank: como maximo 8 canales hardware");

public:
	/// Capacidad del banco (canales hardware).
	[[nodiscard]] static constexpr eng::u16 capacity() noexcept { return MaxSprites; }

	/// Liga la vista Chip de DATA que usará el banco.
	/// \param data bloque DATA completo, ya reservado en Chip por la escena.
	/// \return `false` si la vista está vacía.
	bool bind_data(eng::ChipView<eng::SpriteTag> data) noexcept {
		if (data.empty()) {
			return false;
		}
		m_sprite_data = data;
		return true;
	}

	/// Vista Chip del bloque completo (no propietaria).
	[[nodiscard]] eng::ChipView<eng::SpriteTag> data() const noexcept {
		return m_sprite_data;
	}

	/// Rebanada de DATA de un slot, en bytes.
	/// \param index slot 0..capacity()-1.
	/// \param bytes tamaño de la DATA del sprite (DAT+DATB por línea).
	/// \return vista vacía si el slot o el rango no caben.
	[[nodiscard]] eng::ChipView<eng::SpriteTag> slot_data(eng::u16 index,
							     eng::u16 bytes) const noexcept {
		if (index >= MaxSprites || bytes == 0u) {
			return {};
		}
		const eng::usize offset = index * bytes;
		if (offset + bytes > m_sprite_data.size()) {
			return {};
		}
		return m_sprite_data.subview(offset, bytes);
	}

	/// Configuración mutable de un slot; vacía si el índice no existe.
	[[nodiscard]] eng::Ref<eng::graphics::SpriteConfig> config(eng::u16 index) noexcept {
		return index < MaxSprites ? &m_sprites[index] : nullptr;
	}
	/// Configuración de solo lectura de un slot; vacía si el índice no existe.
	[[nodiscard]] eng::Ref<const eng::graphics::SpriteConfig> config(eng::u16 index) const noexcept {
		return index < MaxSprites ? &m_sprites[index] : nullptr;
	}

	/// Publica todas las configuraciones en el `SpriteManager` (no toca hardware).
	void apply(eng::graphics::SpriteManager& manager) noexcept {
		for (eng::u8 i = 0u; i < MaxSprites; ++i) {
			manager.set(i, m_sprites[i]);
		}
	}

protected:
	eng::graphics::SpriteConfig m_sprites[MaxSprites] {}; ///< configuración por slot
	eng::ChipView<eng::SpriteTag> m_sprite_data {}; ///< DATA Chip prestada; no propietaria
};


} // namespace eng::scene

namespace eng {
using scene::SpriteBank;
} // namespace eng
