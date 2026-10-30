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

#include <eng/core/types/ptr.hpp>
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

	/// Liga la caché al banco Chip (referencia no propietaria; puede estar vacía).
	/// \param chip banco del que se reservan los save-under (Chip/DMA).
	/// \param idle_release frames oculto antes de devolver el buffer al banco.
	void configure(eng::Ref<MemBank<MemoryKind::Chip>> chip, u16 idle_release = 90u) noexcept {
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
		if (!m_chip.valid() || !e.sprite.valid() || target.plane_count == 0u) return false;
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
		if (e.save.valid() && m_chip.valid()) m_chip->release(e.save);
		e.save = {};
		e.save_words_per_row = 0u;
		e.save_height = 0u;
		e.idle = 0u;
	}

	Entry m_entries[Capacity] {};                  ///< instancias registradas (capacidad fija)
	eng::Ref<MemBank<MemoryKind::Chip>> m_chip {}; ///< banco de los save-under (no propietario)
	u16 m_idle_release = 90u;                      ///< frames oculto antes de devolver el buffer
};

/// **Save-under por celda viva del surface** (tiles animados y overlays de mapa).
///
/// El overlay de una celda se pinta con cookie-cut sobre el framebuffer; para
/// repintar el siguiente frame sin dejar restos hay que devolver el fondo REAL
/// que había bajo la celda. Este cache guarda, por clave estable (el índice de
/// celda del animador), un rectángulo `w×h` de todos los planos del destino.
///
/// Contrato de direccionamiento: usa el **mundo sin envolver** del corkscrew
/// (`x` es la columna de mundo en píxeles, no la columna visible). El bloque
/// base de una celda se pinta en esa misma dirección (`x` puede caer en la
/// zona "unrolled" del anillo), de modo que overlay y save-under comparten
/// píxeles aunque la cámara no esté alineada. No consulta el mapa cocinado.
///
/// Memoria: **un único bloque Chip** de `Capacity × cell_bytes` (el Blitter es
/// el que copia). El consumo es fijo y no fragmenta el `BlockPool` con una
/// reserva por celda. Al salir de la ventana, `release` libera la entrada SIN
/// restaurar: la zona ya no se ve y el scroll la repinta cuando la columna
/// vuelve a entrar.
///
/// **Invariante de captura**: el fondo se guarda una vez y se reutiliza en los
/// repintados mientras la celda siga visible (su posición física en el anillo es
/// única y el scroll no la toca). Si otra capa/actor modifica ese fondo, el
/// llamador debe `release` la clave (o re-capturarla) antes de volver a pintar.
///
/// \tparam Capacity nº máximo de celdas con save-under simultáneo.
/// \tparam KeyCount tamaño del espacio de claves (índices de celda del animador).
template <u16 Capacity, u16 KeyCount = 256u>
class MapCellSaveCache {
public:
	MapCellSaveCache() = default;

	/// Liga el cache al banco Chip y reserva **un único bloque** para todas las
	/// celdas (`Capacity × cell_bytes`). Una reserva por celda fragmenta el
	/// `BlockPool` (64 huecos) y hace fallar las capturas.
	/// \param chip       banco Chip del que se reserva el arena (referencia no propietaria; puede estar vacía).
	/// \param cell_bytes bytes de una celda `w×h` con todos los planos
	///        (`(w/16)·2·h·planos`); las capturas mayores se rechazan.
	void configure(eng::Ref<MemBank<MemoryKind::Chip>> chip, u32 cell_bytes) noexcept {
		// `chip` puede estar vacío: el cache queda sin arena y `capture` falla.
		if (m_storage.valid() && m_chip.valid()) m_chip->release(m_storage);
		m_storage = {};
		m_chip = chip;
		m_cell_stride = cell_bytes; // bytes reservados por celda (stride de la arena)
		for (u16 i = 0u; i < KeyCount; ++i) m_slot[i] = kNoSlot; // sin claves
		for (u16 i = 0u; i < Capacity; ++i) m_entries[i] = Entry {}; // sin entradas
		m_failures = 0u;
		if (chip.valid() && cell_bytes != 0u) {
			m_storage = chip->template reserve<BobTag>(
				static_cast<u32>(Capacity) * cell_bytes, 16u);
		}
	}

	/// ¿La clave tiene un fondo capturado? El fondo se captura UNA vez y se
	/// conserva mientras la celda siga en la ventana; el overlay se pinta encima.
	[[nodiscard]] bool saved(u16 key) const noexcept {
		const u16 slot = slot_of(key); // entrada asociada a `key` (o kNoSlot)
		return slot != kNoSlot && m_entries[slot].captured;
	}
	/// ¿El overlay de la clave está pintado ahora (hay que restaurar antes de
	/// repintarlo)? Una celda capturada pero no pintada no necesita restore.
	[[nodiscard]] bool painted(u16 key) const noexcept {
		const u16 slot = slot_of(key); // entrada asociada a `key` (o kNoSlot)
		return slot != kNoSlot && m_entries[slot].painted;
	}

	/// Captura el fondo actual de la celda `key` en `(x, y)`.
	///
	/// El fondo es estático mientras la celda permanece en la ventana (el scroll
	/// no repinta su posición física única). Se captura UNA vez: si `saved(key)`
	/// ya hay captura, esta llamada no emite trabajo. Antes de repintar un
	/// overlay ya pintado hay que llamar a `restore` (el llamador lo decide con
	/// `painted(key)`).
	///
	/// \param plan   plan del frame (recibe un `CopyRect` con todos los planos).
	/// \param target bitmap destino del overlay (interleaved del anillo).
	/// \param key    índice estable de la celda (0..KeyCount-1).
	/// \param x      columna de MUNDO en píxeles (múltiplo de 16).
	/// \param y      línea del anillo, en píxeles.
	/// \param w,h    tamaño de la celda en píxeles (`w` múltiplo de 16).
	/// \return `false` si no hay banco/capacidad/buffer o la caja no cabe en la
	///         reserva del destino; en ese caso no se emite ningún trabajo.
	[[nodiscard]] bool capture(FramePlan& plan, const BobTarget& target, u16 key,
				   u16 x, u16 y, u16 w, u16 h) noexcept {
		if (!m_chip.valid() || key >= KeyCount || target.plane_count == 0u ||
		    w == 0u || (w & 15u) != 0u || h == 0u || !box_within(target, x, y, w, h)) {
			++m_failures;
			return false;
		}
		u16 slot = slot_of(key); // entrada de la clave; kNoSlot = aún sin asignar
		if (slot != kNoSlot && m_entries[slot].captured) {
			// Ya hay fondo: no se recaptura (sería el overlay). La llamada
			// sirve de "overlay pintado" para el siguiente restore.
			m_entries[slot].painted = true;
			return true;
		}
		if (slot == kNoSlot) {
			slot = acquire(); // busca una entrada libre (o recuperable)
			if (slot == kNoSlot) {
				++m_failures;
				return false;
			}
			m_slot[key] = slot;
			m_entries[slot] = Entry {};
			m_entries[slot].key = key;
		}
		if (static_cast<u32>(plan.blit_job_count()) + 1u >
		    static_cast<u32>(plan.max_blit_jobs)) {
			++m_failures;
			return false;
		}
		// Bytes necesarios para esta celda: palabras por fila × 2 × alto × planos.
		const u32 need = static_cast<u32>(w / 16u) * 2u * h * target.plane_count;
		if (!m_storage.valid() || need > m_cell_stride) {
			if (slot_of(key) == slot) m_slot[key] = kNoSlot;
			m_entries[slot] = Entry {};
			++m_failures;
			return false;
		}
		Entry& e = m_entries[slot]; // entrada que describe la captura
		e.x = x;
		e.y = y;
		e.w = w;
		e.h = h;
		e.used = true;
		const bool ok = emit_copy(plan, target, e, slot, true);
		if (ok) {
			e.captured = true;
			e.painted = true; // el llamador pinta el overlay a continuación
		} else {
			++m_failures;
		}
		return ok;
	}

	/// Restaura el fondo guardado de `key` encima del overlay pintado. La
	/// captura se conserva (`saved(key)` sigue siendo true) para el siguiente
	/// frame; sólo se libera al salir de la ventana.
	/// \param plan   plan del frame (recibe un `RestoreRect`).
	/// \param target bitmap destino del overlay.
	/// \param key    índice estable de la celda.
	/// \return `false` si el trabajo no cabe en el plan; `true` si no había nada
	///         pintado o la restauración se encoló.
	[[nodiscard]] bool restore(FramePlan& plan, const BobTarget& target, u16 key) noexcept {
		const u16 slot = slot_of(key); // entrada de la clave (kNoSlot = no guardada)
		if (slot == kNoSlot || !m_entries[slot].painted || !m_entries[slot].captured) {
			return true;
		}
		if (static_cast<u32>(plan.blit_job_count()) + 1u >
		    static_cast<u32>(plan.max_blit_jobs)) {
			return false;
		}
		Entry& e = m_entries[slot]; // entrada con la captura a devolver
		if (!emit_copy(plan, target, e, slot, false)) return false;
		e.painted = false;
		return true;
	}

	/// Olvida una celda que sale de la ventana. NO restaura: su posición queda
	/// fuera del display y el scroll la repinta al volver a entrar.
	/// \param key índice estable de la celda.
	void release(u16 key) noexcept {
		const u16 slot = slot_of(key); // entrada de la clave (kNoSlot = nada que soltar)
		if (slot == kNoSlot) return;
		m_entries[slot] = Entry {};
		m_slot[key] = kNoSlot;
	}

	/// Celdas con entrada viva (buffer reservado o capturado).
	[[nodiscard]] u16 used() const noexcept {
		u16 n = 0u;
		for (u16 i = 0u; i < Capacity; ++i) n += m_entries[i].used ? 1u : 0u;
		return n;
	}
	/// Capturas rechazadas desde `configure` (diagnóstico de capacidad/plan).
	[[nodiscard]] u16 failures() const noexcept { return m_failures; }
	/// Bytes Chip retenidos por el arena de save-under.
	[[nodiscard]] u32 bytes_held() const noexcept {
		return m_storage.valid() ? static_cast<u32>(m_storage.size()) : 0u;
	}
	/// Bytes reservados por entrada (stride de la arena); `capture` rechaza
	/// cajas que necesiten más.
	[[nodiscard]] constexpr u32 cell_stride() const noexcept { return m_cell_stride; }

private:
	static constexpr u16 kNoSlot = 0xffffu;

	struct Entry {
		u16 key = 0u;         ///< clave de la celda (índice estable del animador)
		u16 x = 0u;           ///< columna de mundo del borde izquierdo (px)
		u16 y = 0u;           ///< línea del anillo del borde superior (px)
		u16 w = 0u;           ///< ancho capturado (px)
		u16 h = 0u;           ///< alto capturado (px)
		bool used = false;    ///< slot asignado a una celda
		bool captured = false;///< el buffer contiene el fondo real de la celda
		bool painted = false; ///< el overlay está pintado sobre esa captura
	};

	/// Entrada asociada a `key` (`kNoSlot` si no está en el cache).
	[[nodiscard]] u16 slot_of(u16 key) const noexcept {
		return key < KeyCount ? m_slot[key] : kNoSlot;
	}

	/// Busca una entrada libre; si no hay, recupera una **no pintada** (no hay
	/// nada en pantalla que dependa de su captura). Nunca descarta una pintada:
	/// el llamador debe liberar las celdas fuera de la ventana con `release`.
	/// \return índice de la entrada o `kNoSlot` si todas están pintadas.
	[[nodiscard]] u16 acquire() noexcept {
		for (u16 i = 0u; i < Capacity; ++i) {
			if (!m_entries[i].used) {
				m_entries[i].used = true;
				return i;
			}
		}
		for (u16 i = 0u; i < Capacity; ++i) {
			if (!m_entries[i].painted) {
				m_entries[i] = Entry {};
				m_entries[i].used = true;
				return i;
			}
		}
		return kNoSlot;
	}

	/// ¿La caja `w×h` en `(x,y)` cae dentro de la reserva real del destino?
	/// `x` puede superar el ancho de fila (walk horizontal del corkscrew); el
	/// límite es la reserva completa (`planes.size()`), no `width`.
	/// \param t destino interleaved del anillo (row_bytes/plane_count/planes).
	/// \param x columna de mundo del borde izquierdo (px, múltiplo de 16).
	/// \param y línea del anillo del borde superior (px).
	/// \param w ancho de la caja (px, múltiplo de 16).
	/// \param h alto de la caja (px).
	/// \return `true` si la caja completa cae en la reserva (sin desbordar).
	[[nodiscard]] static bool box_within(const BobTarget& t, u16 x, u16 y, u16 w, u16 h) noexcept {
		if (t.row_bytes == 0u || t.plane_count == 0u || t.planes.empty()) return false;
		const u32 row_step = static_cast<u32>(t.row_bytes) * t.plane_count; // bytes por fila del anillo
		const u32 base = static_cast<u32>(y) * row_step + (x >> 3u);      // offset de la palabra inicial
		const u32 last_plane = static_cast<u32>(t.plane_count - 1u) * t.row_bytes;
		const u32 end = base + last_plane +
				static_cast<u32>(h - 1u) * row_step + static_cast<u32>(w / 8u);
		return end <= static_cast<u32>(t.planes.size());
	}

	/// Copia la celda entre el destino y el buffer de save-under.
	///
	/// **Destino interleaved**: el buffer se guarda en el mismo orden
	/// `[fila][plano][palabra]` que el anillo. Así el paso de una fila-plano a
	/// la siguiente es constante en ambos lados (`row_bytes` en el anillo y
	/// `words×2` en el buffer) y basta **un solo blit** con
	/// `height = h × planos` y `bitplane_count = 1` (el truco de los BOB
	/// interleaved aplicado a una copia). Antes se encolaba un job por plano
	/// (6 BLTSIZE con sus esperas) para la misma celda.
	///
	/// **Destino planar**: se conserva el camino multi-plano con strides por
	/// plano, porque las filas de un plano no son contiguas entre planos.
	///
	/// \param plan destino de los trabajos (recibe un `CopyRect`/`RestoreRect`).
	/// \param t bitmap destino del anillo.
	/// \param e entrada con posición (`x,y`), tamaño (`w,h`) y `captured`/`painted`.
	/// \param slot índice en la arena de save-under (origen del stride `cell_stride`).
	/// \param capture `true` = destino→buffer (guardar); `false` = buffer→destino.
	/// \return `false` si el plan rechaza el trabajo.
	[[nodiscard]] bool emit_copy(FramePlan& plan, const BobTarget& t, const Entry& e,
				     u16 slot, bool capture) const noexcept {
		// Unidades: `words` palabras por fila de un plano; `row_step` bytes entre
		// filas de display del anillo; `save_row_bytes` bytes de una fila-plano
		// del buffer; `save_plane_step` bytes entre planos (solo destino planar).
		const u16 words = static_cast<u16>(e.w / 16u);
		const u32 row_step = static_cast<u32>(t.row_bytes) * t.plane_count;
		const u32 save_row_bytes = static_cast<u32>(words) * 2u;
		const u32 save_plane_step = save_row_bytes * e.h;
		// Direcciones calculadas como offset de byte desde la base de cada vista
		// Chip; `BlitPtr` las convierte a la dirección DMA sin punteros crudos.
		const u32 target_off = static_cast<u32>(e.y) * t.plane_count * t.row_bytes +
			(static_cast<u32>(e.x) >> 3u);
		const u32 save_off = static_cast<u32>(slot) * m_cell_stride;
		const BlitPtr target_ptr {t.planes, static_cast<s32>(target_off)};
		const BlitPtr save_ptr {m_storage.mem_view(), static_cast<s32>(save_off)};
		// `job` = ranura del plan; kind CopyRect (guardar) o RestoreRect (devolver).
		graphics::BlitJob& job = plan.begin_blit_job(capture
			? BlitJobKind::CopyRect : BlitJobKind::RestoreRect);
		if (t.interleaved()) {
			// Buffer `[fila][plano][palabra]`: el anillo avanza `row_bytes` por
			// fila-plano (las filas de display tienen los planos contiguos) y el
			// buffer avanza `words×2`. Un BLTSIZE recorre h×planos filas.
			// Diferencia pequeña y positiva (la celda cabe en la fila); el
			// `mod16` la recibe como s32 por promoción, sin cast de tipo.
			const s32 ring_mod = mod16(t.row_bytes - words * 2u);
			job.source = capture ? target_ptr : save_ptr;
			job.destination = capture ? save_ptr : target_ptr;
			job.source_modulo_bytes = capture ? ring_mod : 0;
			job.destination_modulo_bytes = capture ? 0 : ring_mod;
			job.words_per_row = words;
			job.height = static_cast<u16>(e.h * t.plane_count); // h*planos, contrato u16 de BlitJob
			job.bitplane_count = 1;
			job.source_shift = 0u;
			job.source_plane_stride_bytes = 0u;
			job.destination_plane_stride_bytes = 0u;
			job.source_words_per_row = 0u;
			job.descending = false;
			job.interleaved = true;
			job.minterm = 0u;
			bob_detail::mark_bob(job, e.y, e.h, t.height);
			return plan.commit_blit_job();
		}
		job.source = capture ? target_ptr : save_ptr;
		job.destination = capture ? save_ptr : target_ptr;
		job.source_modulo_bytes = capture
			? mod16(static_cast<s32>(row_step) - static_cast<s32>(words) * 2)
			: mod16(static_cast<s32>(save_row_bytes) - static_cast<s32>(words) * 2);
		job.destination_modulo_bytes = capture
			? mod16(static_cast<s32>(save_row_bytes) - static_cast<s32>(words) * 2)
			: mod16(static_cast<s32>(row_step) - static_cast<s32>(words) * 2);
		job.words_per_row = words;
		job.height = e.h;
		job.bitplane_count = t.plane_count;
		job.source_shift = 0u;
		job.source_plane_stride_bytes = capture ? t.plane_pointer_step() : save_plane_step;
		job.destination_plane_stride_bytes = capture ? save_plane_step : t.plane_pointer_step();
		job.source_words_per_row = 0u;
		job.descending = false;
		job.interleaved = false;
		job.minterm = 0u;
		bob_detail::mark_bob(job, e.y, e.h, t.height);
		return plan.commit_blit_job();
	}

	Entry m_entries[Capacity] {};                 ///< entradas de save-under (capacidad fija)
	u16 m_slot[KeyCount] {};                      ///< key → slot de `m_entries` (kNoSlot = sin asignar)
	Block<BobTag, MemoryKind::Chip> m_storage {}; ///< arena única de Capacity × cell_stride bytes
	eng::Ref<MemBank<MemoryKind::Chip>> m_chip {};///< banco del que se reserva la arena (no propietario)
	u32 m_cell_stride = 0u;                       ///< bytes reservados por celda en la arena
	u16 m_failures = 0u;                          ///< capturas rechazadas (diagnóstico)
};

/// **Save-under por celda con dos clases de tamaño** (celdas de tile y sprites
/// grandes fusionados). Mantiene dos arenas fijas independientes: la pequeña
/// cubre la mayoría de celdas con `SmallCapacity × small_cell_bytes` y la grande
/// reserva `LargeCapacity × large_cell_bytes` solo para las cajas mayores. Así
/// un sprite de 32×32 no multiplica el stride de las 96 celdas de 16×16.
///
/// La clave (`key`) es la misma que en `MapCellSaveCache`: índice estable de la
/// celda/grupo. Cada clave vive en una sola clase; `saved`/`painted`/`restore`
/// consultan ambas y `release` la libera de las dos.
///
/// \tparam SmallCapacity entradas de la arena pequeña (celdas de tile).
/// \tparam LargeCapacity entradas de la arena grande (sprites fusionados).
/// \tparam KeyCount tamaño del espacio de claves (índices de celda).
template <u16 SmallCapacity, u16 LargeCapacity, u16 KeyCount = 256u>
class MapCellSaveCacheSet {
public:
	static_assert(SmallCapacity != 0u && LargeCapacity != 0u, "capacidades no nulas");

	/// Liga el set al banco Chip y reserva las dos arenas.
	/// \param chip banco Chip de las capturas (referencia no propietaria).
	/// \param small_cell_bytes bytes por celda pequeña (tile × tile × planos / 8).
	/// \param large_cell_bytes bytes por sprite grande (ancho × alto × planos / 8).
	void configure(eng::Ref<MemBank<MemoryKind::Chip>> chip, u32 small_cell_bytes,
		       u32 large_cell_bytes) noexcept {
		m_small.configure(chip, small_cell_bytes);
		m_large.configure(chip, large_cell_bytes);
	}

	/// `true` si `key` tiene fondo capturado en cualquiera de las dos arenas.
	[[nodiscard]] bool saved(u16 key) const noexcept {
		return m_small.saved(key) || m_large.saved(key);
	}
	/// `true` si el overlay de `key` está pintado en cualquiera de las dos arenas.
	[[nodiscard]] bool painted(u16 key) const noexcept {
		return m_small.painted(key) || m_large.painted(key);
	}

	/// Captura la caja `w×h` en la clase que pueda contenerla.
	/// \param plan plan del frame.
	/// \param target bitmap destino.
	/// \param key índice estable de la celda/grupo.
	/// \param x columna de mundo del borde izquierdo (px, múltiplo de 16).
	/// \param y línea del anillo del borde superior (px).
	/// \param w ancho de la caja (px, múltiplo de 16).
	/// \param h alto de la caja (px).
	/// \return `false` si ninguna arena tiene hueco/espacio para la caja.
	[[nodiscard]] bool capture(FramePlan& plan, const BobTarget& target, u16 key,
				   u16 x, u16 y, u16 w, u16 h) noexcept {
		const u32 need = static_cast<u32>(w / 16u) * 2u * h *
				 static_cast<u32>(target.plane_count);
		if (need <= m_small.cell_stride()) {
			return m_small.capture(plan, target, key, x, y, w, h);
		}
		return m_large.capture(plan, target, key, x, y, w, h);
	}

	/// Restaura el fondo de `key` desde la arena que lo tenga.
	/// \param plan plan del frame.
	/// \param target bitmap destino.
	/// \param key índice estable de la celda/grupo.
	/// \return `false` si el plan rechaza el trabajo.
	[[nodiscard]] bool restore(FramePlan& plan, const BobTarget& target, u16 key) noexcept {
		return m_small.restore(plan, target, key) &&
		       m_large.restore(plan, target, key);
	}

	/// Libera `key` de ambas arenas (sin restaurar).
	/// \param key índice estable de la celda/grupo.
	void release(u16 key) noexcept {
		m_small.release(key);
		m_large.release(key);
	}

	/// Entradas vivas en las dos arenas.
	[[nodiscard]] u16 used() const noexcept {
		return static_cast<u16>(m_small.used() + m_large.used());
	}
	/// Capturas rechazadas de las dos arenas (diagnóstico).
	[[nodiscard]] u16 failures() const noexcept {
		return static_cast<u16>(m_small.failures() + m_large.failures());
	}
	/// Bytes Chip retenidos por ambas arenas.
	[[nodiscard]] u32 bytes_held() const noexcept {
		return m_small.bytes_held() + m_large.bytes_held();
	}
	/// Capacidad total de entradas (diagnóstico).
	[[nodiscard]] constexpr u32 capacity() const noexcept {
		return static_cast<u32>(SmallCapacity) + LargeCapacity;
	}

private:
	MapCellSaveCache<SmallCapacity, KeyCount> m_small {}; ///< celdas de tile
	MapCellSaveCache<LargeCapacity, KeyCount> m_large {}; ///< sprites grandes
};

// Borde horizontal: un sprite que no cabe en el bitmap destino se culla, no
// escribe fuera de la reserva. Es la garantía que usa el llamador antes de
// ordenar props junto al borde.
constexpr MapSpriteView kMapSpriteEdgeProbe {
	0, 0, 640u, 288u, 320u, 208u, 352u, 288u, 288u,
};
static_assert(!place_map_sprite(0, 0, 32u, 16u, kMapSpriteEdgeProbe, -340, 0).visible);

} // namespace eng::graphics
