#pragma once

/// \file map_sprite.hpp
/// Colocación de BOBs ligados al mapa en el framebuffer visible.
///
/// Un objeto de mapa no se dibuja en coordenadas de pantalla arbitrarias: su
/// posición es de **mundo** y el campo la traduce a la ventana visible y al
/// anillo del corkscrew. Este tipo concentra esa traducción para que props,
/// actores decorativos y animaciones de tiles usen la misma regla.
///
/// El punto de referencia (`anchor_x/anchor_y`) es el píxel del arte que se
/// coloca sobre `world_x/world_y`; el valor por defecto es la esquina superior
/// izquierda. El dibujo usa `bob_draw_ring`, que parte el objeto en la costura
/// vertical del anillo sin que el llamador tenga que hacerlo.

#include <eng/graphics/bob_ring.hpp>
#include <eng/graphics/frame_plan.hpp>
#include <eng/graphics/sprite_asset.hpp>
#include <eng/memory/mem_bank.hpp>

namespace eng::graphics {

/// Ventana y anillo donde se coloca un sprite de mapa.
struct MapSpriteView {
	s32 viewport_x = 0;       ///< Columna de mundo del borde izquierdo visible.
	s32 viewport_y = 0;       ///< Origen Y del anillo (display_offset).
	u16 map_width = 0u;       ///< Ancho del mapa en píxeles (wrap horizontal).
	u16 display_height = 0u;  ///< Alto del anillo (wrap vertical).
	u16 viewport_w = 0u;      ///< Ancho visible del campo (sin HUD).
	u16 viewport_h = 0u;      ///< Alto visible del campo.
	u16 target_width = 0u;    ///< Ancho del bitmap destino.
	u16 target_height = 0u;   ///< Alto del bitmap destino (anillo).
	u16 ring_height = 0u;     ///< Alto del bucle vertical; 0 = destino lineal.
};

/// Posición resuelta de un sprite de mapa.
struct MapSpritePlacement {
	s16 target_x = 0;   ///< X en el bitmap destino.
	s16 target_y = 0;   ///< Y en el bitmap destino (línea del anillo).
	u16 visible_x = 0;  ///< X relativa a la ventana visible.
	u16 visible_y = 0;  ///< Y relativa a la ventana visible.
	bool target_fits = false; ///< La caja cabe en el bitmap destino (sin cruzar el borde).
	bool visible = false;
};

/// Resuelve la posición de un sprite de mapa.
///
/// `world_x/world_y` son el punto de mundo que se alinea con el ancla;
/// `anchor_x/anchor_y` se restan para obtener la esquina superior izquierda del
/// arte. El sprite se considera visible solo cuando cabe completo en la ventana
/// horizontal/vertical; el recorte parcial queda fuera de este contrato.
///
/// \param world_x X de mundo del punto de referencia.
/// \param world_y Y de mundo del punto de referencia.
/// \param w ancho visible del arte.
/// \param h alto visible del arte.
/// \param view parámetros de cámara, mapa y destino.
/// \param anchor_x píxel del arte que se coloca en `world_x` (0 = borde izq.).
/// \param anchor_y píxel del arte que se coloca en `world_y` (0 = borde sup.).
/// \return posición en destino y visibilidad; un sprite no visible conserva
///         coordenadas resueltas para diagnóstico.
[[nodiscard]] constexpr MapSpritePlacement place_map_sprite(
	s16 world_x, s16 world_y, u16 w, u16 h, const MapSpriteView& view,
	s16 anchor_x = 0, s16 anchor_y = 0) noexcept {
	MapSpritePlacement out {};
	if (view.map_width == 0u || view.display_height == 0u ||
	    view.target_width == 0u || view.target_height == 0u) {
		return out;
	}
	const auto wrap = [](s32 value, u16 span) -> u16 {
		s32 v = value % static_cast<s32>(span);
		if (v < 0) v += static_cast<s32>(span);
		return static_cast<u16>(v);
	};
	const u16 vx = wrap(static_cast<s32>(world_x) - view.viewport_x, view.map_width);
	const u16 vy = wrap(static_cast<s32>(world_y) - view.viewport_y, view.display_height);
	out.visible_x = vx;
	out.visible_y = vy;
	out.target_x = static_cast<s16>(wrap(static_cast<s32>(world_x) - anchor_x, view.target_width));
	out.target_y = static_cast<s16>(wrap(static_cast<s32>(world_y) - anchor_y, view.target_height));
	out.target_fits = static_cast<s32>(out.target_x) + static_cast<s32>(w) <=
			  static_cast<s32>(view.target_width);
	out.visible = out.target_fits && vx + w <= view.viewport_w &&
		      vy + h <= view.viewport_h;
	return out;
}

/// Dibuja un sprite ya colocado. El llamador solo aporta el asset, el frame y
/// el resultado de `place_map_sprite`.
///
/// \param plan plan del frame.
/// \param sprite asset del BOB.
/// \param target geometría del bitmap destino.
/// \param frame índice de frame.
/// \param placement posición resuelta.
/// \param ring_height alto del anillo; 0 usa el camino lineal.
/// \return `false` si el asset/plan no son válidos o el frame no cabe.
[[nodiscard]] inline bool draw_map_sprite(
	FramePlan& plan, const Sprite& sprite, const BobTarget& target, u8 frame,
	const MapSpritePlacement& placement, u16 ring_height) noexcept {
	if (!sprite.valid() || !placement.visible) return true;
	if (static_cast<s32>(placement.target_x) + static_cast<s32>(sprite.width()) >
	    static_cast<s32>(target.width)) {
		return true; // el borde horizontal no envuelve: culling seguro
	}
	if (frame >= sprite.frames()) return false;
	if (ring_height == 0u) {
		return sprite.draw(plan, target, frame, placement.target_x, placement.target_y);
	}
	return bob_draw_ring(plan, sprite.bob(), frame, placement.target_x,
			     placement.target_y, ring_height, target);
}

/// Sprite de decorado con posición de mundo y punto de referencia.
///
/// El estado de visibilidad/pintado se mantiene en la instancia para que el
/// llamador pueda saltarse el dibujo cuando no cambió nada. Este tipo no posee
/// memoria ni guarda el fondo; para un BOB que se mueve o tapa a otro se usa
/// save-under/ordenación en la capa que lo contiene.
struct MapSprite {
	Sprite sprite {};
	s16 world_x = 0;
	s16 world_y = 0;
	s16 anchor_x = 0;
	s16 anchor_y = 0;
	u16 footprint_w = 0u; ///< Ancho lógico para culling (0 = ancho del sprite).
	u16 footprint_h = 0u; ///< Alto lógico para culling (0 = alto del sprite).
	u8 frame = 0u;
	u8 painted_frame = 0xffu;
	s16 painted_x = 0;
	s16 painted_y = 0;
	bool visible = false;
	bool saved = false;

	[[nodiscard]] constexpr bool valid() const noexcept { return sprite.valid(); }
	[[nodiscard]] constexpr u16 cull_w() const noexcept {
		return footprint_w != 0u ? footprint_w : sprite.width();
	}
	[[nodiscard]] constexpr u16 cull_h() const noexcept {
		return footprint_h != 0u ? footprint_h : sprite.height();
	}
	[[nodiscard]] constexpr MapSpritePlacement placement(
		const MapSpriteView& view) const noexcept {
		return place_map_sprite(world_x, world_y, cull_w(), cull_h(), view,
					anchor_x, anchor_y);
	}

	/// Resuelve y dibuja el sprite. Repite el dibujo solo si cambió el frame o
	/// la visibilidad; `force` lo pinta aunque no haya cambiado (p. ej. otro BOB
	/// pasó por detrás).
	/// \param plan plan del frame.
	/// \param target destino del BOB.
	/// \param view cámara/mapa/anillo.
	/// \param force fuerza el repintado.
	/// \return `false` si el dibujo no pudo encolarse.
	[[nodiscard]] bool draw(FramePlan& plan, const BobTarget& target,
				const MapSpriteView& view, bool force = false) noexcept {
		// Mezclar el camino sin save-under con uno que ya guardó fondo dejaría
		// el píxel viejo sin restaurar; el llamador debe seguir usando
		// `draw_saved` o restaurar explícitamente.
		if (saved) return false;
		const MapSpritePlacement placement = this->placement(view);
		visible = placement.visible;
		if (!visible) {
			painted_frame = 0xffu;
			saved = false;
			return true;
		}
		if (!force && painted_frame == frame) return true;
		if (!draw_map_sprite(plan, sprite, target, frame, placement, view.ring_height)) {
			return false;
		}
		painted_frame = frame;
		painted_x = placement.target_x;
		painted_y = placement.target_y;
		saved = false;
		return true;
	}

	/// Dibuja con **save-under**: antes de pintar el frame nuevo restaura el
	/// fondo guardado bajo la posición anterior. Solo emite trabajos si cambió
	/// el frame, la posición o `force` (por ejemplo, otro BOB pasó por detrás).
	/// Al quedar fuera de la ventana restaura el fondo y libera el estado de
	/// guardado. El buffer `save` debe tener capacidad para `w*h` con el layout
	/// del destino; `save_height` debe cubrir el alto del sprite.
	/// \param plan plan del frame.
	/// \param target destino del BOB.
	/// \param view cámara/mapa/anillo.
	/// \param save buffer de guardado (memoria del llamador).
	/// \param save_words_per_row palabras por fila del buffer.
	/// \param save_height filas del buffer.
	/// \param force fuerza la restauración y el repintado.
	/// \return `false` si falla un save/restore/dibujo o el frame no existe.
	[[nodiscard]] bool draw_saved(FramePlan& plan, const BobTarget& target,
				      const MapSpriteView& view, ChipView<BobTag> save,
				      u16 save_words_per_row, u16 save_height,
				      bool force = false) noexcept {
		const MapSpritePlacement placement = this->placement(view);
		visible = placement.visible;
		if (!visible) {
			if (!saved) {
				painted_frame = 0xffu;
				return true;
			}
			return restore_saved(plan, target, view, save, save_words_per_row,
					     save_height);
		}
		const bool moved = saved && (painted_x != placement.target_x ||
					     painted_y != placement.target_y);
		if (!force && !moved && painted_frame == frame) return true;
		if (saved && !restore_saved(plan, target, view, save, save_words_per_row,
					    save_height)) {
			return false;
		}
		if (frame >= sprite.frames()) return false;
		if (!bob_save_box_ring(plan, sprite.bob(), sprite.width(), sprite.height(),
				       placement.target_x, placement.target_y,
				       view.ring_height, target, save, save_words_per_row,
				       save_height)) {
			return false;
		}
		if (!draw_map_sprite(plan, sprite, target, frame, placement, view.ring_height)) {
			return false;
		}
		painted_frame = frame;
		painted_x = placement.target_x;
		painted_y = placement.target_y;
		saved = true;
		return true;
	}

	/// Restaura el fondo guardado sin pintar otro frame. La capa lo usa al
	/// principio del frame para limpiar todos los BOBs antes de ordenarlos.
	[[nodiscard]] bool restore_saved(FramePlan& plan, const BobTarget& target,
					 const MapSpriteView& view, ChipView<BobTag> save,
					 u16 save_words_per_row, u16 save_height) noexcept {
		if (!saved) return true;
		if (!bob_restore_box_ring(plan, sprite.bob(), sprite.width(),
					  sprite.height(), painted_x, painted_y,
					  view.ring_height, target, save,
					  save_words_per_row, save_height)) {
			return false;
		}
		saved = false;
		painted_frame = 0xffu;
		return true;
	}

	/// Fuerza que el próximo `draw`/`draw_saved` repinte la instancia. Con
	/// save-under, el siguiente dibujo restaura primero el fondo anterior.
	void invalidate() noexcept { painted_frame = 0xffu; }
};

/// Pool fijo de BOBs de mapa con save-under y liberación por inactividad.
///
/// La capa registra una instancia por BOB (varias instancias pueden compartir
/// `group` para un decorado de varios tiles). El buffer de save-under se reserva
/// del banco Chip la primera vez que el BOB es visible y se devuelve al banco
/// cuando lleva `idle_release` frames fuera de la ventana o restaurado. Así el
/// número de buffers vivos sigue a los BOBs visibles, no al total del mapa.
template <u16 Capacity>
class MapSpriteCache {
public:
	struct Entry {
		MapSprite sprite {};
		Block<BobTag, MemoryKind::Chip> save {};
		u16 save_words_per_row = 0u;
		u16 save_height = 0u;
		u16 group = 0u;
		u16 idle = 0u;
		bool used = false;
	};

	void configure(MemBank<MemoryKind::Chip>* chip, u16 idle_release = 90u) noexcept {
		m_chip = chip;
		m_idle_release = idle_release;
	}
	[[nodiscard]] constexpr u16 capacity() const noexcept { return Capacity; }
	[[nodiscard]] constexpr u16 used() const noexcept {
		u16 n = 0u;
		for (u16 i = 0u; i < Capacity; ++i) n += m_entries[i].used ? 1u : 0u;
		return n;
	}
	[[nodiscard]] constexpr u32 bytes_held() const noexcept {
		u32 n = 0u;
		for (u16 i = 0u; i < Capacity; ++i) {
			if (m_entries[i].save.valid()) n += m_entries[i].save.size();
		}
		return n;
	}
	[[nodiscard]] constexpr bool active(u16 index) const noexcept {
		return index < Capacity && m_entries[index].used;
	}
	[[nodiscard]] constexpr bool visible(u16 index) const noexcept {
		return index < Capacity && m_entries[index].used &&
		       m_entries[index].sprite.visible;
	}

	/// Registra una instancia. `group` agrupa varias instancias para el mismo
	/// decorado; `footprint` permite declarar un área lógica menor que la hoja
	/// empaquetada (p. ej. un actor interleaved pair).
	[[nodiscard]] u16 add(const Sprite& sprite, s16 world_x, s16 world_y,
			      s16 anchor_x = 0, s16 anchor_y = 0, u16 group = 0u,
			      u16 footprint_w = 0u, u16 footprint_h = 0u) noexcept {
		for (u16 i = 0u; i < Capacity; ++i) {
			Entry& e = m_entries[i];
			if (e.used) continue;
			e = Entry {};
			e.sprite.sprite = sprite;
			e.sprite.world_x = world_x;
			e.sprite.world_y = world_y;
			e.sprite.anchor_x = anchor_x;
			e.sprite.anchor_y = anchor_y;
			e.sprite.footprint_w = footprint_w;
			e.sprite.footprint_h = footprint_h;
			e.group = group;
			e.used = true;
			return i;
		}
		return 0xffffu;
	}

	void set_world(u16 index, s16 world_x, s16 world_y) noexcept {
		if (index >= Capacity || !m_entries[index].used) return;
		m_entries[index].sprite.world_x = world_x;
		m_entries[index].sprite.world_y = world_y;
	}

	void set_frame(u16 index, u8 frame) noexcept {
		if (index >= Capacity || !m_entries[index].used) return;
		m_entries[index].sprite.frame = frame;
	}

	/// Dibuja una instancia visible; reserva su save-under si hace falta.
	/// \return `false` si no hay banco/espacio o falla el blit.
	[[nodiscard]] bool draw(FramePlan& plan, const BobTarget& target,
				const MapSpriteView& view, u16 index,
				bool force = false) noexcept {
		if (index >= Capacity || !m_entries[index].used) return false;
		Entry& e = m_entries[index];
		const MapSpritePlacement p = e.sprite.placement(view);
		if (!p.visible) {
			if (!restore(plan, target, view, index)) return false;
			++e.idle;
			return true;
		}
		if (!ensure_save(e, target)) return false;
		if (!e.sprite.draw_saved(plan, target, view, e.save.mem_view(),
					e.save_words_per_row, e.save_height, force)) {
			return false;
		}
		e.idle = 0u;
		return true;
	}

	/// Dibuja todas las instancias del mismo grupo (decorado de varios tiles).
	/// Cada instancia conserva su propio save-under; el llamador puede usar
	/// `force` cuando otro BOB cruza por detrás del grupo completo.
	[[nodiscard]] bool draw_group(FramePlan& plan, const BobTarget& target,
				      const MapSpriteView& view, u16 group,
				      bool force = false) noexcept {
		bool ok = true;
		for (u16 i = 0u; i < Capacity; ++i) {
			const Entry& e = m_entries[i];
			if (!e.used || e.group != group) continue;
			if (!draw(plan, target, view, i, force)) ok = false;
		}
		return ok;
	}

	/// Restaura el fondo guardado de una instancia (si lo hay).
	[[nodiscard]] bool restore(FramePlan& plan, const BobTarget& target,
				   const MapSpriteView& view, u16 index) noexcept {
		if (index >= Capacity || !m_entries[index].used) return false;
		Entry& e = m_entries[index];
		if (!e.sprite.saved) return true;
		if (!e.save.valid()) return false;
		if (!e.sprite.restore_saved(plan, target, view, e.save.mem_view(),
					    e.save_words_per_row, e.save_height)) {
			return false;
		}
		e.sprite.visible = false;
		++e.idle;
		return true;
	}

	/// Restaura el fondo de todas las instancias del grupo (antes de reordenar
	/// props/actores o al ocultar el grupo).
	[[nodiscard]] bool restore_group(FramePlan& plan, const BobTarget& target,
					 const MapSpriteView& view, u16 group) noexcept {
		bool ok = true;
		for (u16 i = 0u; i < Capacity; ++i) {
			const Entry& e = m_entries[i];
			if (!e.used || e.group != group) continue;
			if (!restore(plan, target, view, i)) ok = false;
		}
		return ok;
	}

	/// Avanza la edad de las instancias ocultas y devuelve buffers al banco.
	/// Llamar una vez por frame, después de dibujar/restaurar.
	void tick() noexcept {
		for (u16 i = 0u; i < Capacity; ++i) {
			Entry& e = m_entries[i];
			if (!e.used || e.sprite.visible || e.sprite.saved) continue;
			if (++e.idle <= m_idle_release) continue;
			release_buffer(e);
		}
	}

	void hide(u16 index) noexcept {
		if (index >= Capacity || !m_entries[index].used) return;
		m_entries[index].sprite.visible = false;
		m_entries[index].sprite.invalidate();
	}

	void remove(u16 index) noexcept {
		if (index >= Capacity || !m_entries[index].used) return;
		release_buffer(m_entries[index]);
		m_entries[index] = Entry {};
	}

private:
	[[nodiscard]] bool ensure_save(Entry& e, const BobTarget& target) noexcept {
		if (e.save.valid()) return true;
		if (m_chip == nullptr || !e.sprite.valid() || target.plane_count == 0u) return false;
		const u16 words = bob_detail::box_words(e.sprite.sprite.bob(),
							e.sprite.sprite.width(), 0);
		const u32 bytes = static_cast<u32>(words) * 2u *
				  static_cast<u32>(e.sprite.sprite.height()) *
				  static_cast<u32>(target.plane_count);
		e.save = m_chip->template reserve<BobTag>(bytes, 16u);
		if (!e.save.valid()) return false;
		e.save_words_per_row = words;
		e.save_height = e.sprite.sprite.height();
		return true;
	}
	void release_buffer(Entry& e) noexcept {
		if (e.save.valid() && m_chip != nullptr) m_chip->release(e.save);
		e.save = {};
		e.save_words_per_row = 0u;
		e.save_height = 0u;
		e.idle = 0u;
	}

	Entry m_entries[Capacity] {};
	MemBank<MemoryKind::Chip>* m_chip = nullptr;
	u16 m_idle_release = 90u;
};

// Borde horizontal: un sprite que no cabe en el bitmap destino se culla, no
// escribe fuera de la reserva. Es la garantía que usa el llamador antes de
// ordenar props junto al borde.
constexpr MapSpriteView kMapSpriteEdgeProbe {
	0, 0, 640u, 288u, 320u, 208u, 352u, 288u, 288u,
};
static_assert(!place_map_sprite(0, 0, 32u, 16u, kMapSpriteEdgeProbe, -340, 0).visible);

} // namespace eng::graphics
