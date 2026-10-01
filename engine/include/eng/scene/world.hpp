#pragma once

/// \file world.hpp
/// **Mundo retenido** (`eng::scene`): contenedor **aditivo** de **capas** (`Layer`), cada
/// una con su **cámara** (`Camera2D`) y su profundidad. Es el primer escalón del modelo
/// `SCENE_AND_RESOURCES.md` (`World` → `Layer` → cámara): hoy guarda y ordena las capas y
/// expone su cámara; el **planner** que las materializa (playfield/tilemap/efecto) y el
/// reparto de recursos llegan después, sobre este contenedor.
///
/// ```cpp
/// auto fondo = app.world().add_layer("fondo", 0);
/// if (fondo) {
///     fondo->camera().reset({{0, 0, 640, 256}}, {320, 256});
///     fondo->camera().set_scroll_x(128);        // `layer.camera().scroll_x`
/// }
/// ```
///
/// `add_layer` devuelve `Ref<Layer>` (**anulable**): si el mundo está lleno devuelve uno
/// inválido en vez de un `Layer&` a memoria basura, para no fallar en silencio.

#include <eng/core/types/ptr.hpp>
#include <eng/core/types/types.hpp>
#include <eng/graphics/bob.hpp>
#include <eng/graphics/composition/limits.hpp>
#include <eng/graphics/frame_plan.hpp>
#include <eng/graphics/bitmap_view.hpp>
#include <eng/scene/actor.hpp>
#include <eng/scene/virtual_scene.hpp>

namespace eng::scene {

	/// **Contenido de una capa del mundo**: actores, tilemap o región Fill.
enum class WorldLayerKind : u8 {
	Actors,  ///< capa de actores (BOBs/sprites) hermanada por `ActorStore`
	Tilemap, ///< capa de tiles (contenido en `TileLayer`)
	Fill,    ///< región de color opaco materializada antes del render del juego
	Bitmap,  ///< bitmap indexado opaco de un plano
};

/// **Técnica de scroll** de una región/capa (desplazamiento, independiente del modo de display).
/// La capa lo **pide**; el planner lo acepta, degrada o rechaza según su coste (ver `region_cost`).
enum class ScrollKind : u8 {
	None,            ///< sin scroll
	Fine,            ///< `BPLCON1` (delay fino); coste Copper mínimo
	BlitterColumns,  ///< columnas nuevas por Blitter (robocod) + fino por `BPLCON1`
	CopperRing,      ///< `BPLxPT`/módulo (xlimited): bitmap ring, sin split por línea
	CopperSplit,     ///< split por línea (xyunlimited): una por banda (caro en Copper)
};

/// **Playfield preferido** de una capa (el planner decide la materialización final).
enum class LayerPlayfield : u8 {
	Any,         ///< el planner elige
	Pf1,         ///< playfield 1 (delante en DPF)
	Pf2,         ///< playfield 2 (detrás en DPF)
	SpriteLayer, ///< capa de sprites hardware (fondo sprite-as-playfield)
};

/// **Coste declarado de una técnica de región**: lo que consume del frame. El planner lo usa
/// para validar y degradar (`OBJECT_SYSTEM.md` §15.8). Unidades aproximadas y comparables.
struct RegionCost {
	u16 copper_words_per_line = 0;  ///< MOVEs de Copper por línea (0 = banda estática)
	u16 planes = 0;                 ///< planos de bitplane de la región
	u16 blitter_words_per_frame = 0; ///< palabras de Blitter por frame (scroll por columnas)
	bool uses_sprite_layer = false;  ///< ocupa canales de sprite
};

/// **Coste de una técnica** (`mode` × `scroll` × `planes`). Es la función que el planner
/// consulta para aceptar/degradar; aquí solo se declara (no materializa).
[[nodiscard]] constexpr RegionCost region_cost(graphics::composition::SceneMode mode,
					       ScrollKind scroll, u8 planes) noexcept {
	using Mode = graphics::composition::SceneMode;
	RegionCost c {};
	c.planes = (mode == Mode::CopperChunky) ? 0u : planes;
	c.copper_words_per_line =
		(scroll == ScrollKind::CopperSplit) ? 4u : (scroll == ScrollKind::CopperRing ? 2u : 0u);
	c.blitter_words_per_frame = (scroll == ScrollKind::BlitterColumns) ? 1u : 0u;
	return c;
}

/// **Región vertical** del display (banda de líneas) con su **técnica** (modo de display +
/// scroll), su playfield preferido y sus planos. Permite expresar un DPF + una banda con otra
/// técnica (p. ej. *copper-chunky* en los 48 px inferiores) por Copper en `top`.
struct WorldRegion {
	u16 top = 0;
	u16 bottom = 0;
	LayerPlayfield playfield = LayerPlayfield::Pf1;
	graphics::composition::SceneMode mode = graphics::composition::SceneMode::Standard;
	ScrollKind scroll = ScrollKind::None;
	u8 planes = 0;
	u8 speed_px = 4u; ///< velocidad máxima de scroll pedida (px/frame); acota las guardas
	[[nodiscard]] constexpr bool ok() const noexcept { return bottom > top; }
	[[nodiscard]] constexpr RegionCost cost() const noexcept {
		return region_cost(mode, scroll, planes);
	}
};

/// Una capa del mundo: identidad, profundidad, cámara, **contenido** (actores o tilemap) y el
/// **algoritmo de scroll pedido** + playfield preferido. El engine la materializa (planner);
/// el juego describe y lee su cámara.
class Layer {
public:
	constexpr void configure(const char* id, u8 depth) noexcept {
		m_id = id;
		m_depth = depth;
		m_kind = WorldLayerKind::Actors;
		m_fill_bounds = {};
		m_fill_color = 0u;
	}
	constexpr void configure_fill(const char* id, u8 depth, const Box& bounds, u8 color) noexcept {
		configure(id, depth);
		m_kind = WorldLayerKind::Fill;
		m_fill_bounds = bounds;
		m_fill_color = color;
	}
	constexpr void configure_bitmap(const char* id, u8 depth,
					const graphics::BitmapView<eng::TextureTag>& bitmap) noexcept {
		configure(id, depth);
		m_kind = WorldLayerKind::Bitmap;
		m_bitmap = bitmap;
	}
	[[nodiscard]] constexpr const char* id() const noexcept { return m_id; }
	[[nodiscard]] constexpr u8 depth() const noexcept { return m_depth; }
	/// Cámara de la capa: su `scroll_x`/`scroll_y` es la ventana al mundo (`PUBLIC_GAME_API.md` §2.1.3).
	[[nodiscard]] constexpr Camera2D& camera() noexcept { return m_camera; }
	[[nodiscard]] constexpr const Camera2D& camera() const noexcept { return m_camera; }

	/// **Contenido**: actores (por defecto) o tilemap.
	[[nodiscard]] constexpr WorldLayerKind kind() const noexcept { return m_kind; }
	[[nodiscard]] constexpr bool is_tilemap() const noexcept { return m_kind == WorldLayerKind::Tilemap; }
	[[nodiscard]] constexpr bool is_fill() const noexcept { return m_kind == WorldLayerKind::Fill; }
	[[nodiscard]] constexpr const Box& fill_bounds() const noexcept { return m_fill_bounds; }
	[[nodiscard]] constexpr u8 fill_color() const noexcept { return m_fill_color; }
	[[nodiscard]] constexpr bool is_bitmap() const noexcept { return m_kind == WorldLayerKind::Bitmap; }
	[[nodiscard]] constexpr const graphics::BitmapView<eng::TextureTag>& bitmap() const noexcept { return m_bitmap; }
	/// Liga el contenido de tilemap (reusa `TileLayer`); pasa la capa a `Tilemap`.
	constexpr void bind_tilemap(const TileLayer& t) noexcept {
		m_tile = t;
		m_kind = WorldLayerKind::Tilemap;
	}
	[[nodiscard]] constexpr TileLayer& tilemap() noexcept { return m_tile; }
	[[nodiscard]] constexpr const TileLayer& tilemap() const noexcept { return m_tile; }

	/// **Scroll pedido** y playfield preferido (los valida el planner).
	[[nodiscard]] constexpr ScrollKind scroll() const noexcept { return m_scroll; }
	constexpr void set_scroll(ScrollKind s) noexcept { m_scroll = s; }
	[[nodiscard]] constexpr LayerPlayfield prefer() const noexcept { return m_prefer; }
	constexpr void set_prefer(LayerPlayfield p) noexcept { m_prefer = p; }

private:
	const char* m_id = "";
	u8 m_depth = 0;
	WorldLayerKind m_kind = WorldLayerKind::Actors;
	ScrollKind m_scroll = ScrollKind::None;
	LayerPlayfield m_prefer = LayerPlayfield::Any;
	TileLayer m_tile {};
	Camera2D m_camera {};
	Box m_fill_bounds {};
	u8 m_fill_color = 0u;
	graphics::BitmapView<eng::TextureTag> m_bitmap {};
};

/// **Mundo**: conjunto fijo de capas (sin heap) y de actores. El orden de dibujo lo fija la
/// profundidad de capa (menor = al fondo) y, dentro del plan, el `z` del actor; el planner
/// lo usará al componer.
template <u8 MaxLayers = 8u, u8 MaxActors = 16u, u8 MaxRegions = 8u>
class World {
public:
	/// Añade una capa de actores. Devuelve `Ref<Layer>` inválido si el mundo está lleno.
	[[nodiscard]] Ref<Layer> add_layer(const char* id, u8 depth) noexcept {
		if (m_count >= MaxLayers) {
			return {};
		}
		Layer& l = m_layers[m_count];
		l.configure(id, depth);
		++m_count;
		return l;
	}

	/// Añade una región de color opaco. Las capas Fill se pintan por profundidad ascendente antes
	/// de `Game::render`; el juego puede dibujar encima en el mismo frame. `false` si la región está vacía.
	[[nodiscard]] Ref<Layer> add_fill_layer(const char* id, u8 depth, const eng::Box& bounds,
						 u8 color) noexcept {
		if (m_count >= MaxLayers || bounds.empty()) return {};
		Layer& l = m_layers[m_count];
		l.configure_fill(id, depth, bounds, color);
		++m_count;
		return l;
	}

	/// Añade una vista bitmap no propietaria. El owner debe vivir mientras viva la capa; `App`
	/// conserva la reserva Chip del helper `add_bitmap_background`.
	[[nodiscard]] Ref<Layer> add_bitmap_layer(const char* id, u8 depth,
						 const graphics::BitmapView<eng::TextureTag>& bitmap) noexcept {
		if (m_count >= MaxLayers || id == nullptr || !bitmap.valid() ||
		    bitmap.layout != graphics::PlaneLayout::Contiguous || bitmap.plane_count != 1u ||
		    bitmap.row_bytes < bitmap.width) return {};
		Layer& l = m_layers[m_count];
		l.configure_bitmap(id, depth, bitmap);
		++m_count;
		return l;
	}

	/// Añade una **capa de tilemap** (contenido vía `TileLayer`). `Ref<Layer>` inválido si el
	/// mundo está lleno.
	[[nodiscard]] Ref<Layer> add_tile_layer(const char* id, u8 depth,
						const TileLayer& tile) noexcept {
		if (m_count >= MaxLayers) {
			return {};
		}
		Layer& l = m_layers[m_count];
		l.configure(id, depth);
		l.bind_tilemap(tile);
		++m_count;
		return l;
	}

	[[nodiscard]] constexpr u8 count() const noexcept { return m_count; }
	[[nodiscard]] constexpr u8 capacity() const noexcept { return MaxLayers; }
	[[nodiscard]] constexpr bool full() const noexcept { return m_count >= MaxLayers; }

	/// Materializa solo las capas Fill, con orden estable por profundidad (menor = fondo),
	/// trasladando coordenadas de mundo con la cámara de cada capa y recortando al viewport.
	/// Cada región es un rectángulo opaco: primero se limpia a color 0 y luego se aplica su color.
	template <class Sink>
	[[nodiscard]] bool materialize_fill_layers(Sink&& sink, u16 viewport_width,
						   u16 viewport_height) const {
		u8 order[MaxLayers] {};
		u8 count = 0u;
		for (u8 i = 0u; i < m_count; ++i) {
			if (!m_layers[i].is_fill()) continue;
			u8 at = count;
			while (at > 0u && m_layers[order[at - 1u]].depth() > m_layers[i].depth()) {
				order[at] = order[at - 1u];
				--at;
			}
			order[at] = i;
			++count;
		}
		for (u8 i = 0u; i < count; ++i) {
			const Layer& layer = m_layers[order[i]];
			const Box& world_bounds = layer.fill_bounds();
			const s32 left = static_cast<s32>(world_bounds.x) - layer.camera().scroll_x();
			const s32 top = static_cast<s32>(world_bounds.y) - layer.camera().scroll_y();
			const s32 right = left + world_bounds.w;
			const s32 bottom = top + world_bounds.h;
			const s32 clip_left = left > 0 ? left : 0;
			const s32 clip_top = top > 0 ? top : 0;
			const s32 clip_right = right < viewport_width ? right : viewport_width;
			const s32 clip_bottom = bottom < viewport_height ? bottom : viewport_height;
			if (clip_right <= clip_left || clip_bottom <= clip_top) continue;
			const Box screen_bounds {
				static_cast<s16>(clip_left), static_cast<s16>(clip_top),
				static_cast<u16>(clip_right - clip_left),
				static_cast<u16>(clip_bottom - clip_top)};
			if (!sink(screen_bounds, 0u) || !sink(screen_bounds, layer.fill_color())) return false;
		}
		return true;
	}

	/// Emite filas visibles de bitmaps indexados, trasladadas por la cámara y recortadas al viewport.
	/// El sink recibe (x, y, row) sin que World tome ownership del bitmap.
	template <class Sink>
	[[nodiscard]] bool materialize_bitmap_layers(Sink&& sink, u16 viewport_width,
						     u16 viewport_height) const {
		u8 order[MaxLayers] {};
		u8 count = 0u;
		for (u8 i = 0u; i < m_count; ++i) {
			if (!m_layers[i].is_bitmap()) continue;
			u8 at = count;
			while (at > 0u && m_layers[order[at - 1u]].depth() > m_layers[i].depth()) {
				order[at] = order[at - 1u];
				--at;
			}
			order[at] = i;
			++count;
		}
		for (u8 i = 0u; i < count; ++i) {
			const Layer& layer = m_layers[order[i]];
			const auto& bitmap = layer.bitmap();
			const s32 src_x = layer.camera().scroll_x();
			const s32 src_y = layer.camera().scroll_y();
			if (src_x >= bitmap.width || src_y >= bitmap.height) continue;
			const u16 copy_w = static_cast<u16>(bitmap.width - src_x < viewport_width
							    ? bitmap.width - src_x : viewport_width);
			const u16 copy_h = static_cast<u16>(bitmap.height - src_y < viewport_height
							    ? bitmap.height - src_y : viewport_height);
			for (u16 y = 0u; y < copy_h; ++y) {
				const eng::usize offset = static_cast<eng::usize>(src_y + y) * bitmap.row_bytes + src_x;
				if (!sink(0u, y, bitmap.planes.view().subspan(offset, copy_w))) return false;
			}
		}
		return true;
	}

	/// Capa por índice (`Ref` inválido si fuera de rango).
	[[nodiscard]] Ref<Layer> layer(u8 i) noexcept {
		return i < m_count ? Ref<Layer> {m_layers[i]} : Ref<Layer> {};
	}
	[[nodiscard]] Ref<const Layer> layer(u8 i) const noexcept {
		return i < m_count ? Ref<const Layer> {m_layers[i]} : Ref<const Layer> {};
	}
	/// Capa por id (`Ref` inválido si no existe).
	[[nodiscard]] Ref<Layer> find(const char* id) noexcept {
		for (u8 i = 0; i < m_count; ++i) {
			if (same_id(m_layers[i].id(), id)) {
				return Ref<Layer> {m_layers[i]};
			}
		}
		return {};
	}

	// --- Regiones del display (bandas verticales) ----------------------------
	/// Añade una **región** (banda vertical) con su playfield/modo/planos: es lo que permite
	/// expresar un DPF + una banda de otra altura. `false` si no cabe o es inválida.
	[[nodiscard]] bool add_region(const WorldRegion& r) noexcept {
		if (m_region_count >= MaxRegions || !r.ok()) {
			return false;
		}
		m_regions[m_region_count] = r;
		++m_region_count;
		return true;
	}
	[[nodiscard]] constexpr u8 region_count() const noexcept { return m_region_count; }
	[[nodiscard]] Ref<const WorldRegion> region(u8 i) const noexcept {
		return i < m_region_count ? Ref<const WorldRegion> {m_regions[i]}
					  : Ref<const WorldRegion> {};
	}

	// --- Actores retenidos ---------------------------------------------------
	/// Limpia los actores y fija el presupuesto de representación (canales de sprite /
	/// palabras de Blitter / capas). Llámalo antes de dar de alta actores.
	void reset_actors(s32 sprite_channels = 0, u16 bob_budget_words = 60000u,
			  u8 layer_slots = 0) noexcept {
		m_actors.reset();
		m_allocator.reset(RepresentationBudget {
			static_cast<u8>(sprite_channels < 0 ? 0 : sprite_channels), bob_budget_words,
			layer_slots});
	}
	/// Da de alta un actor; la **representación** (sprite/BOB/CPU/playfield) la elige el
	/// engine según el presupuesto. `ActorId` inválido si no cabe.
	[[nodiscard]] ActorId add_actor(const ActorDesc& desc) noexcept {
		return m_actors.add(desc, m_allocator);
	}
	[[nodiscard]] Ref<Actor> actor(ActorId id) noexcept { return m_actors.get(id); }
	[[nodiscard]] ActorStore<MaxActors>& actors() noexcept { return m_actors; }
	[[nodiscard]] const ActorStore<MaxActors>& actors() const noexcept { return m_actors; }

	/// **Emite los actores** al plan (orden por superficie/`z`) con el destino y el clip
	/// dados. Devuelve cuántos se dibujaron (0 si el orden no cabe o algo no cupo).
	[[nodiscard]] u16 emit(graphics::FramePlan& plan,
			       eng::Span<const graphics::BobTarget> targets,
			       graphics::DirtyRect clip, s16 cam_x = 0, s16 cam_y = 0,
			       u8 buffer = 0) noexcept {
		ActorEmitContext ctx {};
		ctx.targets = targets;
		ctx.clip = clip;
		ctx.cam_x = cam_x;
		ctx.cam_y = cam_y;
		ctx.buffer = buffer;
		return emit_actors_in_order(plan, m_actors, ctx,
					    eng::Span<ActorId> {m_order, MaxActors});
	}

private:
	/// Compara dos ids C sin `strcmp` (freestanding).
	[[nodiscard]] static bool same_id(const char* a, const char* b) noexcept {
		if (a == nullptr || b == nullptr) {
			return false;
		}
		while (*a != '\0' && *a == *b) {
			++a;
			++b;
		}
		return *a == *b;
	}

	Layer m_layers[MaxLayers] {};
	u8 m_count = 0u;
	ActorStore<MaxActors> m_actors {};
	RepresentationAllocator m_allocator {};
	ActorId m_order[MaxActors] {};
	WorldRegion m_regions[MaxRegions] {};
	u8 m_region_count = 0u;
};

} // namespace eng::scene
