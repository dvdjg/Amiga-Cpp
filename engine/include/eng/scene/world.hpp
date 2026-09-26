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
#include <eng/scene/actor.hpp>
#include <eng/scene/virtual_scene.hpp>

namespace eng::scene {

/// **Contenido de una capa del mundo**: actores (por defecto) o un tilemap (reusa `TileLayer`).
enum class WorldLayerKind : u8 {
	Actors,  ///< capa de actores (BOBs/sprites) hermanada por `ActorStore`
	Tilemap, ///< capa de tiles (contenido en `TileLayer`)
};

/// **Algoritmo de scroll pedido por una capa**. La capa lo **pide**; el planner lo acepta,
/// degrada o rechaza según el presupuesto de Copper/planos (ver `OBJECT_SYSTEM.md` §15).
enum class LayerScroll : u8 {
	Static,      ///< sin scroll (Copper mínimo)
	Fine,        ///< `BPLCON1` (delay fino, barato en Copper)
	XLimited,    ///< desplazamiento por `BPLxPT`/módulo (bitmap ring)
	XYUnlimited, ///< ring + split por línea (caro en Copper; una por banda)
};

/// **Playfield preferido** de una capa (el planner decide la materialización final).
enum class LayerPlayfield : u8 {
	Any,         ///< el planner elige
	Pf1,         ///< playfield 1 (delante en DPF)
	Pf2,         ///< playfield 2 (detrás en DPF)
	SpriteLayer, ///< capa de sprites hardware (fondo sprite-as-playfield)
};

/// **Región vertical** del display (banda de líneas) con su playfield/modo/planos. Permite
/// expresar un DPF + una banda de otra altura/planos (reconfiguración por Copper en `top`).
struct WorldRegion {
	u16 top = 0;
	u16 bottom = 0;
	LayerPlayfield playfield = LayerPlayfield::Pf1;
	graphics::composition::SceneMode mode = graphics::composition::SceneMode::Standard;
	u8 planes = 0;
	[[nodiscard]] constexpr bool ok() const noexcept { return bottom > top; }
};

/// Una capa del mundo: identidad, profundidad, cámara, **contenido** (actores o tilemap) y el
/// **algoritmo de scroll pedido** + playfield preferido. El engine la materializa (planner);
/// el juego describe y lee su cámara.
class Layer {
public:
	constexpr void configure(const char* id, u8 depth) noexcept {
		m_id = id;
		m_depth = depth;
	}
	[[nodiscard]] constexpr const char* id() const noexcept { return m_id; }
	[[nodiscard]] constexpr u8 depth() const noexcept { return m_depth; }
	/// Cámara de la capa: su `scroll_x`/`scroll_y` es la ventana al mundo (`PUBLIC_GAME_API.md` §2.1.3).
	[[nodiscard]] constexpr Camera2D& camera() noexcept { return m_camera; }
	[[nodiscard]] constexpr const Camera2D& camera() const noexcept { return m_camera; }

	/// **Contenido**: actores (por defecto) o tilemap.
	[[nodiscard]] constexpr WorldLayerKind kind() const noexcept { return m_kind; }
	[[nodiscard]] constexpr bool is_tilemap() const noexcept { return m_kind == WorldLayerKind::Tilemap; }
	/// Liga el contenido de tilemap (reusa `TileLayer`); pasa la capa a `Tilemap`.
	constexpr void bind_tilemap(const TileLayer& t) noexcept {
		m_tile = t;
		m_kind = WorldLayerKind::Tilemap;
	}
	[[nodiscard]] constexpr TileLayer& tilemap() noexcept { return m_tile; }
	[[nodiscard]] constexpr const TileLayer& tilemap() const noexcept { return m_tile; }

	/// **Scroll pedido** y playfield preferido (los valida el planner).
	[[nodiscard]] constexpr LayerScroll scroll() const noexcept { return m_scroll; }
	constexpr void set_scroll(LayerScroll s) noexcept { m_scroll = s; }
	[[nodiscard]] constexpr LayerPlayfield prefer() const noexcept { return m_prefer; }
	constexpr void set_prefer(LayerPlayfield p) noexcept { m_prefer = p; }

private:
	const char* m_id = "";
	u8 m_depth = 0;
	WorldLayerKind m_kind = WorldLayerKind::Actors;
	LayerScroll m_scroll = LayerScroll::Static;
	LayerPlayfield m_prefer = LayerPlayfield::Any;
	TileLayer m_tile {};
	Camera2D m_camera {};
};

/// **Mundo**: conjunto fijo de capas (sin heap) y de actores. El orden de dibujo lo fija la
/// profundidad de capa (menor = al fondo) y, dentro del plan, el `z` del actor; el planner
/// lo usará al componer.
template <u8 MaxLayers = 8u, u8 MaxActors = 16u, u8 MaxRegions = 8u>
class World {
public:
	/// Añade una capa. Devuelve `Ref<Layer>` inválido si el mundo está lleno (no hay fallo
	/// silencioso: comprueba `if (fondo)`).
	[[nodiscard]] Ref<Layer> add_layer(const char* id, u8 depth) noexcept {
		if (m_count >= MaxLayers) {
			return {};
		}
		Layer& l = m_layers[m_count];
		l.configure(id, depth);
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
