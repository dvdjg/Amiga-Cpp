#pragma once

/// \file virtual_scene.hpp
/// Escena virtual retenida para juegos 2D.
///
/// Esta cabecera es el primer puente entre la parte "engine moderno" y el Amiga
/// real. La logica de juego no deberia pedir "haz un MOVE a BPLCON1" ni "lanza un
/// blit de tal modulo"; deberia mover una camara dentro de un mundo virtual,
/// activar capas, marcar tiles sucios y dejar que el backend de cada maquina
/// compile esa intencion a cobre, blitter, sprites hardware o CPU.
///
/// El diseno es deliberadamente pequeno:
///
/// - no usa heap ni STL, porque el runtime Amiga es freestanding;
/// - trabaja con memoria aportada por el juego, UAF-R o una arena del engine;
/// - permite que la misma escena conceptual pueda tener otro backend futuro
///   para Mega Drive, NeoGeo u otra maquina 2D;
/// - mantiene los detalles OCS fuera de esta capa, aunque deja pistas para que el
///   driver Amiga sepa si debe preparar margenes, doble buffer o scroll fino.

#include <amg/core/types.hpp>
#include <amg/graphics/tilemap/tile_scroll.hpp>

namespace amg::scene {

/// Rectangulo entero en coordenadas de mundo.
///
/// Para un plataformas grande, estas coordenadas son pixels logicos dentro del
/// escenario completo, no pixels visibles. Un mapa de 200x32 tiles de 16 pixels
/// mide 3200x512 pixels aunque el A500 solo muestre una ventana pequena cada frame.
struct WorldRect {
	u16 x = 0;
	u16 y = 0;
	u16 width = 0;
	u16 height = 0;

	constexpr bool valid() const {
		return width != 0 && height != 0;
	}
};

/// Camara 2D con posicion anterior.
///
/// Guardar la posicion previa evita que las capas altas calculen deltas a mano.
/// El driver de tiles usara esos deltas para decidir si solo debe recomponer una
/// columna/fila oculta o si necesita invalidar una zona mayor.
class Camera2D {
public:
	constexpr void reset(WorldRect world, Size2u viewport) {
		m_world = world;
		m_viewport = viewport;
		m_x = world.x;
		m_y = world.y;
		m_previous_x = m_x;
		m_previous_y = m_y;
		clamp_to_world();
	}

	constexpr void begin_frame() {
		m_previous_x = m_x;
		m_previous_y = m_y;
	}

	constexpr void set_position(u16 x, u16 y) {
		m_x = x;
		m_y = y;
		clamp_to_world();
	}

	constexpr void move_by(s16 dx, s16 dy) {
		m_x = add_signed_clamped(m_x, dx);
		m_y = add_signed_clamped(m_y, dy);
		clamp_to_world();
	}

	constexpr void center_on(u16 x, u16 y) {
		const u16 half_w = static_cast<u16>(m_viewport.width / 2u);
		const u16 half_h = static_cast<u16>(m_viewport.height / 2u);
		m_x = x > half_w ? static_cast<u16>(x - half_w) : m_world.x;
		m_y = y > half_h ? static_cast<u16>(y - half_h) : m_world.y;
		clamp_to_world();
	}

	constexpr graphics::tilemap::ScrollPosition scroll_position() const {
		return {m_x, m_y};
	}

	constexpr s16 delta_x() const {
		return static_cast<s16>(static_cast<s32>(m_x) - static_cast<s32>(m_previous_x));
	}

	constexpr s16 delta_y() const {
		return static_cast<s16>(static_cast<s32>(m_y) - static_cast<s32>(m_previous_y));
	}

	constexpr bool moved() const {
		return m_x != m_previous_x || m_y != m_previous_y;
	}

	constexpr WorldRect world() const { return m_world; }
	constexpr Size2u viewport() const { return m_viewport; }
	constexpr u16 x() const { return m_x; }
	constexpr u16 y() const { return m_y; }

private:
	static constexpr u16 add_signed_clamped(u16 value, s16 delta) {
		const s32 next = static_cast<s32>(value) + static_cast<s32>(delta);
		if (next <= 0) {
			return 0;
		}
		if (next >= 0xffff) {
			return 0xffff;
		}
		return static_cast<u16>(next);
	}

	constexpr void clamp_to_world() {
		const u16 max_x = m_world.width > m_viewport.width
			? static_cast<u16>(m_world.x + (m_world.width - m_viewport.width))
			: m_world.x;
		const u16 max_y = m_world.height > m_viewport.height
			? static_cast<u16>(m_world.y + (m_world.height - m_viewport.height))
			: m_world.y;

		if (m_x < m_world.x) {
			m_x = m_world.x;
		} else if (m_x > max_x) {
			m_x = max_x;
		}

		if (m_y < m_world.y) {
			m_y = m_world.y;
		} else if (m_y > max_y) {
			m_y = max_y;
		}
	}

	WorldRect m_world {};
	Size2u m_viewport {};
	u16 m_x = 0;
	u16 m_y = 0;
	u16 m_previous_x = 0;
	u16 m_previous_y = 0;
};

/// Clase funcional de una capa de escena.
///
/// Hoy solo compilamos capas de tilemap, pero el vocabulario ya deja sitio a
/// objetos, colisiones y regiones que aporten intenciones de Copper. Eso sera
/// importante para escenas ricas estilo aventura grafica EHB o plataformas con
/// zonas de paleta distintas por franja.
enum class LayerKind : u8 {
	Tilemap,
	Collision,
	ObjectSpawn,
	Trigger,
	CopperRegion,
};

/// Politica de framebuffer para un tile layer.
///
/// `HiddenMargins` es el caso Amiga tipico: se mantiene una superficie algo mayor
/// que la ventana visible y se dibujan tiles en el area oculta antes de que la
/// camara llegue a ella. Otros backends podrian escoger nametables, chunks o VRAM.
enum class TileFramebufferStrategy : u8 {
	Static,
	HiddenMargins,
	DoubleBufferedHiddenMargins,
	FullRedraw,
};

/// Contrato runtime de scroll de una capa de tiles.
///
/// UAF deberia poder exportar un bloque muy parecido a este: dice que tipo de
/// superficie necesita el runtime, si permite scroll fino y cuantos tiles extra
/// debe preparar alrededor del viewport.
struct TileScrollStrategy {
	TileFramebufferStrategy framebuffer = TileFramebufferStrategy::HiddenMargins;
	u8 tile_size = graphics::tilemap::TileMap16::tile_size;
	u8 margin_tiles_x = 1;
	u8 margin_tiles_y = 1;
	bool hardware_fine_scroll = true;
	bool double_buffered = true;
};

/// Capa de tiles enlazada a un mapa retenido.
///
/// No contiene punteros a bitplanes ni registros: solo describe el mundo logico.
/// El compilador Amiga leera `map`, `strategy` y `target_buffer` para producir
/// trabajos `FramePlan::add_tile_block_copy` y, mas adelante, cambios de Copper.
struct TileLayer {
	const char* id = nullptr;
	graphics::tilemap::TileMap16* map = nullptr;
	TileScrollStrategy strategy {};
	u8 target_buffer = 0;
	u8 priority = 0;

	constexpr bool ok() const {
		return id != nullptr && map != nullptr && map->ok();
	}
};

/// Resultado retenido de una capa de tiles para un frame.
///
/// Esta estructura es intencionadamente parecida a un "render graph" pequeno:
/// todavia no contiene blits reales, solo cuantos tiles hay que considerar y como
/// queda descompuesto el scroll. Asi se puede probar en C++ puro antes de tocar
/// hardware.
struct TileLayerFrame {
	const TileLayer* layer = nullptr;
	graphics::tilemap::TileScrollFrame scroll {};
};

/// Plan de escena virtual para un frame.
///
/// Las demos actuales solo usan una capa, pero el limite fijo permite tener varios
/// playfields, una capa de colision visible en debug y una capa de triggers sin
/// asignaciones dinamicas.
struct VirtualSceneFrame {
	static constexpr u8 max_tile_layers = 4;

	TileLayerFrame tile_layers[max_tile_layers] {};
	u8 tile_layer_count = 0;
	bool overflow = false;
};

/// Escena virtual 2D.
///
/// Es la fachada que deberia consumir la logica del juego: se configura con una
/// camara y capas, y cada frame produce un plan retenido. El siguiente nivel del
/// engine sera un `TileScrollDriver` Amiga que traduzca este plan a blitter,
/// punteros de bitplane, `BPLCON1` y zonas Copper.
class VirtualScene {
public:
	constexpr bool reset(Camera2D camera, TileLayer* tile_layers, u8 tile_layer_count) {
		if (tile_layer_count > VirtualSceneFrame::max_tile_layers) {
			m_tile_layers = tile_layers;
			m_tile_layer_count = VirtualSceneFrame::max_tile_layers;
			m_camera = camera;
			m_overflow = true;
			return false;
		}

		m_camera = camera;
		m_tile_layers = tile_layers;
		m_tile_layer_count = tile_layer_count;
		m_overflow = false;
		return true;
	}

	constexpr Camera2D& camera() { return m_camera; }
	constexpr const Camera2D& camera() const { return m_camera; }

	constexpr VirtualSceneFrame prepare_frame() {
		VirtualSceneFrame frame {};
		frame.overflow = m_overflow;

		for (u8 i = 0; i < m_tile_layer_count; ++i) {
			TileLayer& layer = m_tile_layers[i];
			if (!layer.ok()) {
				frame.overflow = true;
				continue;
			}

			const u16 visible_tiles_x = tiles_needed(
				m_camera.viewport().width,
				layer.strategy.tile_size,
				layer.strategy.margin_tiles_x
			);
			const u16 visible_tiles_y = tiles_needed(
				m_camera.viewport().height,
				layer.strategy.tile_size,
				layer.strategy.margin_tiles_y
			);

			TileLayerFrame& layer_frame = frame.tile_layers[frame.tile_layer_count++];
			layer_frame.layer = &layer;
			layer_frame.scroll = layer.map->prepare_frame(
				m_camera.scroll_position(),
				visible_tiles_x,
				visible_tiles_y,
				layer.target_buffer
			);
			frame.overflow = frame.overflow || layer_frame.scroll.overflow;
		}

		return frame;
	}

private:
	static constexpr u16 tiles_needed(u16 pixels, u8 tile_size, u8 margin_tiles) {
		const u16 base = static_cast<u16>((pixels + tile_size - 1u) / tile_size);
		return static_cast<u16>(base + margin_tiles);
	}

	Camera2D m_camera {};
	TileLayer* m_tile_layers = nullptr;
	u8 m_tile_layer_count = 0;
	bool m_overflow = false;
};

} // namespace amg::scene
