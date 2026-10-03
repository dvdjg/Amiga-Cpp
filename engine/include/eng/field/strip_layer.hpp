#pragma once

/// \file strip_layer.hpp
/// **Capa de scroll por tiras** de la fachada: agrupa en un solo objeto los buffers (anillo +
/// columna), el `StripScrollController` (CPU + Blitter) y el `StripComposer` (Copper). El juego
/// solo declara el mapa, el banco de tiles, la paleta y los tamaños, y conduce la capa con
/// `frame(scroll, prev)`; **no ve** el compositor, los `BPLxPT` ni los buffers.
///
/// Implementa `playfield::ScrollLayer<Backend>` (la **interfaz** que el `App` arranca y conduce por
/// frame): un juego sobre `App` la registra con `add_scroll_layer` y no ve el compositor, los
/// buffers ni el backend.

#include <eng/core/types/ptr.hpp>
#include <eng/core/types/types.hpp>
#include <eng/field/scroll_layer.hpp>
#include <eng/field/scroll_plan.hpp>
#include <eng/field/strip_composer.hpp>
#include <eng/field/strip_scroller.hpp>
#include <eng/field/tilemap_view.hpp>
#include <eng/graphics/palette.hpp>
#include <eng/memory/memory_manager.hpp>

namespace eng {
namespace playfield {

/// **Capa de scroll por tiras** de la fachada (ver doc del fichero). Implementa `ScrollLayer`.
template <class Geom, class Map, class Backend>
class StripScrollLayer : public ScrollLayer<Backend> {
public:
	/// El juego declara el mapa (observador, `Ref`).
	constexpr void set_map(Map& map) noexcept { m_map = map; }
	/// El juego aporta el **banco de tiles** ya empaquetado (contenido: atlas + repack) y su stride.
	/// \param bank          base del banco de tiles (Chip).
	/// \param stride_words  palabras por tile en el banco.
	constexpr void set_bank(const eng::u16* bank, eng::u16 stride_words) noexcept {
		m_bank = bank;
		m_bank_stride = stride_words;
	}
	/// Paleta del display (vistas no propietarias a palabras Amiga).
	constexpr void set_palette(eng::PaletteWords palette) noexcept { m_palette = palette; }
	/// **Liga un asset de tilemap** (`TilemapView`: banco + mapa + paleta) en una llamada y
	/// **deriva los tamaños de reserva** de la geometría `Geom` (anillo y columna): el juego no
	/// calcula bytes ni conoce la guarda/el *fetch*. Requiere `Map == TilemapView`.
	/// \param tm  asset de tilemap (banco + mapa + paleta); se guarda como observador.
	constexpr void set_tilemap(TilemapView& tm) noexcept {
		m_bank = tm.bank;
		m_bank_stride = tm.bank_stride_words;
		m_palette = tm.palette;
		m_map = tm;
		m_ring_bytes = static_cast<eng::u32>(m_geom.ring_w_bytes) * m_geom.planes * m_geom.ring_h;
		m_column_bytes = static_cast<eng::u32>(m_geom.column_planelines) * 2u;
	}

	/// **Fija la geometría** (instancia). El NTTP `StripScrollGeometry` la aporta por su tipo
	/// (miembros `static constexpr`); una `RuntimeScrollGeometry` (cargada en runtime, p. ej. de un
	/// editor) la trae por valores. Llámalo **antes** de `set_plan`/`set_tilemap` (derivan tamaños).
	/// \param g  la geometría (NTTP o `RuntimeScrollGeometry`).
	constexpr void set_geometry(const Geom& g) noexcept { m_geom = g; }

	/// **Setup declarativo**: liga el **contenido** del `ScrollPlan` (el `tilemap`) y deriva los
	/// tamaños; la geometría la fija el tipo (`Geom`) y la cámara, `track_camera`. Es el vocabulario
	/// común con el corcóscru (§7(e)). Requiere `Map == TilemapView`.
	/// \param plan  el `ScrollPlan` (el `tilemap` aporta banco/mapa/paleta).
	constexpr void set_plan(const ScrollPlan& plan) noexcept {
		m_plan_tilemap = plan.tilemap;
		set_tilemap(m_plan_tilemap);
	}
	/// Tamaños de reserva: anillo, columna y copperlist.
	/// \param ring_bytes    bytes del anillo.
	/// \param column_bytes  bytes de la columna de trabajo.
	/// \param copper_bytes  capacidad de la copperlist (def. 1536).
	constexpr void set_sizes(eng::u32 ring_bytes, eng::u32 column_bytes,
				 eng::u32 copper_bytes = 1536u) noexcept {
		m_ring_bytes = ring_bytes;
		m_column_bytes = column_bytes;
		m_copper_bytes = copper_bytes;
	}
	/// Sigue las variables de cámara (px) del juego: el `App` las leerá por frame. `y` puede ser
	/// `nullptr` (scroll puramente horizontal). Es el camino de un mapa **toroidal**, donde la
	/// posición X avanza sin recortarse (el motor envuelve por su cuenta).
	/// \param x  puntero a la posición X del juego (px de mundo).
	/// \param y  puntero a la Y (o `nullptr` para scroll solo horizontal).
	constexpr void track_camera(const eng::s32* x, const eng::s32* y = nullptr) noexcept {
		m_cam_x = x;
		m_cam_y = y;
		m_cam_read = nullptr;
	}

	/// Sigue una **cámara** (cualquier tipo con `x()`/`y()` enteros; `eng::scene::Camera2D` lo
	/// cumple): la capa lee su posición por frame, sin que el juego mantenga variables sueltas.
	/// Es el vocabulario de `PUBLIC_GAME_API.md` §2.1.3 para un mapa **acotado** (la cámara ya
	/// recorta a sus límites); el tipo es parámetro de plantilla, así que esta cabecera no depende
	/// de `eng/scene` (el lector es *type-erased*).
	/// \param cam  cámara con `x()`/`y()` enteros (p. ej. `scene::Camera2D`).
	template <class Camera>
	void follow_camera(const Camera& cam) noexcept {
		m_cam_obj = const_cast<Camera*>(&cam);
		m_cam_read = [](void* p, eng::s32& x, eng::s32& y) noexcept {
			const Camera& c = *static_cast<const Camera*>(p);
			x = static_cast<eng::s32>(c.x());
			y = static_cast<eng::s32>(c.y());
		};
		m_cam_x = nullptr;
		m_cam_y = nullptr;
	}

	/// **Setup**: reserva los buffers, monta y arranca la copperlist del compositor, liga el
	/// controlador y pre-pinta el anillo. Requiere un `Backend` y su `MemoryManager`.
	/// \param mm       gestor de memoria (anillo + columna + copperlist en Chip).
	/// \param backend  el backend (arranca la copperlist).
	/// \return `false` si falta el mapa/banco o no cabe la reserva.
	[[nodiscard]] bool begin(MemoryManager& mm, Backend& backend) noexcept override {
		if (!m_map.valid() || m_bank == nullptr) return false;
		auto& chip = mm.chip();
		m_ring = chip.template reserve<eng::PlaneTag>(m_ring_bytes, 16u);
		m_column = chip.template reserve<eng::PlaneTag>(m_column_bytes, 16u);
		if (!m_ring.valid() || !m_column.valid()) return false;
		m_ring_words = reinterpret_cast<eng::u16*>(m_ring.data());
		m_column_words = reinterpret_cast<eng::u16*>(m_column.data());
		if (!m_composer.init(mm, m_palette, m_copper_bytes)) return false;
		m_composer.set_ring(m_ring_words);
		m_composer.set_geometry(m_geom);
		if (!m_composer.build()) return false;
		m_composer.takeover(backend);
		m_ctrl.set_geometry(m_geom);
		m_ctrl.bind(m_ring_words, m_bank, m_column_words, m_bank_stride, *m_map.get(), backend);
		m_ctrl.fill_ring();
		m_ok = true;
		return true;
	}

	/// **Frame**: si la cámara X cruzó frontera de tile, pinta la columna entrante; la Y solo mueve
	/// la ventana vertical (el bitmap ya tiene todas las filas). Parchea la copperlist (fine
	/// `BPLCON1` + `BPLxPT` por plano, con el offset Y) y publica el bloque.
	/// \param backend  el backend.
	/// \param x,y      cámara actual (px de mundo).
	/// \param prev_x,prev_y  cámara del frame anterior (para el delta).
	void frame(Backend& backend, eng::s32 x, eng::s32 y, eng::s32 prev_x,
		   eng::s32 prev_y) noexcept {
		if (!m_ok) return;
		const auto fr = m_ctrl.tick(x, y, prev_x, prev_y);
		(void)m_composer.patch(strip_copper_values(m_geom, fr));
		m_composer.install(backend);
	}

	/// Conduce la capa siguiendo la cámara registrada (`track_camera`/`follow_camera`). Lo usa el `App`.
	void frame_from_source(Backend& backend) noexcept {
		eng::s32 x = 0;
		eng::s32 y = 0;
		if (m_cam_read != nullptr) {
			m_cam_read(m_cam_obj, x, y);
		} else {
			x = (m_cam_x != nullptr) ? *m_cam_x : 0;
			y = (m_cam_y != nullptr) ? *m_cam_y : 0;
		}
		frame(backend, x, y, m_prev_x, m_prev_y);
		m_prev_x = x;
		m_prev_y = y;
	}

	[[nodiscard]] bool ok() const noexcept { return m_ok; }
	[[nodiscard]] eng::u16* ring_words() noexcept { return m_ring_words; }

	/// **Override de `ScrollLayer`**: conduce un frame siguiendo la cámara registrada. Lo llama el
	/// `App`; el juego no.
	void frame(Backend& backend) noexcept override { frame_from_source(backend); }

private:
	eng::Block<eng::PlaneTag, eng::MemoryKind::Chip> m_ring {};
	eng::Block<eng::PlaneTag, eng::MemoryKind::Chip> m_column {};
	Geom m_geom {}; ///< geometría (NTTP o `RuntimeScrollGeometry`); ver `set_geometry`
	eng::playfield::StripComposer<Geom> m_composer {};
	eng::playfield::StripScrollController<Geom, Map, Backend> m_ctrl {};
	eng::Ref<Map> m_map {};
	TilemapView m_plan_tilemap {}; // soporte del plan (dueño de la vista ligada por `set_plan`)
	const eng::u16* m_bank = nullptr;
	eng::u16 m_bank_stride = 0u;
	eng::PaletteWords m_palette {};
	eng::u32 m_ring_bytes = 0u;
	eng::u32 m_column_bytes = 0u;
	eng::u32 m_copper_bytes = 1536u;
	eng::u16* m_ring_words = nullptr;
	eng::u16* m_column_words = nullptr;
	const eng::s32* m_cam_x = nullptr;
	const eng::s32* m_cam_y = nullptr;
	void* m_cam_obj = nullptr;
	void (*m_cam_read)(void*, eng::s32&, eng::s32&) noexcept = nullptr;
	eng::s32 m_prev_x = 0;
	eng::s32 m_prev_y = 0;
	bool m_ok = false;
};

} // namespace playfield
} // namespace eng
