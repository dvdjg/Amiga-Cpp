#pragma once

/// \file tile_field.hpp
/// API de campos de tiles con scroll infinito por doble página.
///
/// Diseño documentado en `docs/architecture/TILE_FIELD_API.md`. Resumen:
///
/// - Cada playfield (campo) es un controlador independiente con su propio mapa
///   de tiles (índices u16), su framebuffer y su cámara. No conoce a los demás.
/// - El scroll infinito en un eje se consigue con un framebuffer del DOBLE del
///   viewport en ese eje: dos páginas del tamaño del viewport. La cámara scrollea
///   dentro del framebuffer; la página offscreen se va rellenando con los tiles
///   que se revelarán (repartidos según el presupuesto), y al cruzar el límite de
///   página el display "salta" a la página ya preparada (invisible).
/// - `begin(offset_absoluto)` encola el estampado inicial de todas las páginas;
///   la demo bombea `pump` hasta que `busy()` es false (sin límite de tiempo real
///   en init). `update(delta)` se llama cada frame con el desplazamiento respecto
///   al frame anterior (tope `max_delta_x/y`, configurable y a priori).
/// - El dibujo de tiles se hace SIEMPRE con el Blitter vía `FramePlan` (jobs
///   `TileBlockCopy`), reutilizando las rutinas del backend: nunca se estampa
///   por CPU.
/// - El `DpfDisplayComposer` es la ÚNICA capa que toca los registros compartidos
///   (BPLxPT, BPLCON1, módulos, BPLCON2, DPF). Un campo bitmap opcional
///   (`BitmapFieldConfig`) sirve para naves/HUD/BOBs.
///
/// La API no impone DPF: un solo `TileFieldController` con 5 bitplanes es un
/// campo de 32 colores normal.
///
/// ## Geometría del framebuffer y del display (doble página horizontal)
///
/// El framebuffer de un eje con scroll tiene `2 * viewport_px` de ancho
/// (640 px para un viewport de 320). El display muestra una ventana de
/// `viewport_px` con:
///
///   - módulo = row_bytes - fetch_bytes, donde fetch_bytes = 40 (320 px) y
///     row_bytes = 640/8 = 80; cada fila del display avanza una fila de mundo
///     completa (80 bytes) aunque solo lea 40;
///   - puntero = fb + página_activa*40 + coarse + y*row_bytes, donde coarse es
///     el desplazamiento de 16 px dentro de la página y `BPLCON1` el fine.
///
/// Cuando `world_x - page_origin[active]` alcanza el viewport, el puntero "salta"
/// a la otra página (ya preparada) y la página vacante se redibuja con el tramo
/// siguiente. `FieldHardwareView` expone todo lo que el compositor necesita.

#include <eng/core/span.hpp>
#include <eng/core/types.hpp>
#include <eng/graphics/copper/scheduler.hpp>
#include <eng/graphics/frame_plan.hpp>
#include <eng/memory/arena.hpp>

namespace eng::field {

/// Mapa de tiles del mundo (índices u16 -> tilesets de miles de patrones).
///
/// `cells` es una vista contigua segura (Span): sin aritmética de punteros en
/// la API. `wrap_x`/`wrap_y` > 0 hacen que la coordenada de mundo repita cada N
/// tiles (mundo periódico); 0 significa borde del mundo (fuera del mapa se
/// devuelve `edge_tile`).
struct TileLayerMap {
	eng::Span<const eng::u16> cells {};
	eng::u16 width = 0;
	eng::u16 height = 0;
	eng::u16 wrap_x = 0;
	eng::u16 wrap_y = 0;
	eng::u16 edge_tile = 0;

	eng::u16 tile_at(eng::s32 tx, eng::s32 ty) const {
		if (cells.empty() || width == 0 || height == 0) {
			return edge_tile;
		}
		eng::u32 x, y;
		if (wrap_x != 0) {
			x = static_cast<eng::u32>(((tx % static_cast<eng::s32>(wrap_x)) + wrap_x) % wrap_x);
		} else {
			if (tx < 0 || tx >= static_cast<eng::s32>(width)) return edge_tile;
			x = static_cast<eng::u32>(tx);
		}
		if (wrap_y != 0) {
			y = static_cast<eng::u32>(((ty % static_cast<eng::s32>(wrap_y)) + wrap_y) % wrap_y);
		} else {
			if (ty < 0 || ty >= static_cast<eng::s32>(height)) return edge_tile;
			y = static_cast<eng::u32>(ty);
		}
		return cells.at(x + y * width); // at() con trap ante violación de rango
	}
};

/// Desplazamiento (x, y) en píxeles. En `begin` es el offset absoluto desde la
/// esquina superior izquierda; en `update` es el DELTA respecto al frame previo.
struct TileScrollOffset {
	eng::s16 x = 0;
	eng::s16 y = 0;
};

/// Configuración estática de un campo de tiles.
struct TileFieldConfig {
	TileLayerMap map;
	const eng::u16* tileset = nullptr;   // patrones en Chip RAM (índice u16)
	eng::u16 tileset_count = 0;
	eng::u8 tileset_planes = 6;          // planos del campo (5 => 32 colores)
	/// Anchura de los tiles en px, MÚLTIPLO DE 16 (16, 32, 48, ...). Un tile de
	/// 32px se copia en una sola pasada del Blitter (words_por_fila = 2), sin
	/// pasadas extra de 16px. La asignación de tiles ANCHOS individuales a slots
	/// del mapa es una fase posterior; aquí la anchura es global al campo.
	eng::u16 tile_width = 16;
	eng::u16 tile_size = 16;             // alto de tile (filas)
	eng::u16 viewport_w = 320;
	eng::u16 viewport_h = 256;
	/// Contrato de rendimiento (a priori): tope del salto por frame por eje.
	/// max_delta pequeño (p. ej. 2) da muchos frames para repartir el redibujo
	/// de la página offscreen; grande (15) exige más tiles por frame.
	eng::s16 max_delta_x = 15;
	eng::s16 max_delta_y = 15;
	eng::u8 max_tiles_per_frame = 4;     // presupuesto de dibujo por frame
	bool scroll_x = true;                // scroll infinito horizontal (2 páginas)
	bool scroll_y = true;                // scroll infinito vertical (2 páginas)
};

/// Franja de tiles pendiente de dibujar (repartida entre frames).
///
/// Una franja es una REGIÓN rectangular del framebuffer físico (origen fb + w/h
/// en tiles) que se rellena con los tiles de la región correspondiente del
/// mundo (origen world + w/h). `cursor` es el número de tiles ya dibujados;
/// `draw_pending` la recorre en fila-mayor. Una franja de scroll (página
/// vacante) es una banda de `viewport_w x fb_h` (X) o `fb_w x viewport_h` (Y).
struct TilePendingStrip {
	eng::s32 world_tile_x = 0;   // origen de mundo (tiles)
	eng::s32 world_tile_y = 0;
	eng::u16 fb_tile_x = 0;      // origen físico en el framebuffer (tiles)
	eng::u16 fb_tile_y = 0;
	eng::u16 width = 0;          // ancho de la región (tiles)
	eng::u16 height = 0;         // alto de la región (tiles)
	eng::u32 cursor = 0;         // tiles dibujados
	bool active = false;
};

/// Estado mutable de un campo (lo mantiene el controlador).
///
/// Cada página física (0/1 en cada eje) tiene un `page_origin_x/y` con el mundo
/// (px) de su borde superior-izquierdo: la página activa contiene
/// `[page_origin[active], page_origin[active]+viewport)`. En scroll
/// bidireccional (la cámara va y viene) la página opuesta se rellena con el
/// tramo que esté en la DIRECCIÓN DE VIAJE, y al invertir se reencola con el
/// tramo de la nueva dirección (con tiempo suficiente: el punto de inflexión
/// está lejos del cruce).
struct TileFieldState {
	eng::s32 world_x = 0;        // posición de mundo (píxeles), origen = TL
	eng::s32 world_y = 0;
	eng::s32 page_origin_x[2] = {0, 0};  // mundo del borde de cada página física
	eng::s32 page_origin_y[2] = {0, 0};
	eng::u8 active_page_x = 0;   // 0/1: página donde empieza la ventana visible
	eng::u8 active_page_y = 0;
	eng::s16 last_dir_x = 0;     // signo de la última velocidad (inversión)
	eng::s16 last_dir_y = 0;
	TilePendingStrip pending[8] {};
	eng::u8 pending_count = 0;
	bool initialized = false;
};

/// Vista hardware de un campo: lo único que el compositor necesita.
///
/// `display_byte_offset` es el desplazamiento (dentro de cada plano) donde
/// empieza la ventana visible: página activa + coarse de 16px + y*row_bytes.
/// `fine_x` es el nibble de `BPLCON1` (el compositor lo coloca en PF1/PF2).
struct FieldHardwareView {
	const eng::u8* bitplanes = nullptr;   // base del plano 0 del framebuffer
	eng::u32 plane_stride = 0;            // bytes entre planos de este campo
	eng::u32 display_byte_offset = 0;
	eng::u8 fine_x = 0;
	eng::u16 bpl1mod = 0;                 // row_bytes - fetch_bytes
	eng::u8 plane_count = 0;
	eng::u8 first_hardware_plane = 0;     // en DPF: 0 (PF1) o 1 (PF2)
};

/// Lienzo bitmap opcional (naves, HUD, BOBs): no conoce tiles.
struct BitmapFieldConfig {
	const eng::u8* bitmap = nullptr;
	eng::u16 width = 0;
	eng::u16 height = 0;
	eng::u8 planes = 0;
	bool scrolling = false;
};

/// Controlador de un campo de tiles.
///
/// No conoce DPF ni a otros campos. Gestiona el framebuffer (doble página por
/// eje infinito), la cámara y el reparto de franjas. Los tiles se dibujan con el
/// Blitter a través del `FramePlan` (jobs `TileBlockCopy`), reutilizando las
/// rutinas del backend: el controlador no toca píxeles por CPU.
class TileFieldController {
public:
	TileFieldController() = default;

	/// Encola el estampado inicial de todas las páginas del framebuffer para un
	/// offset absoluto inicial.
	///
	/// El dibujo es por Blitter y el plan limita a `max_blit_jobs` jobs por
	/// llamada: la demo bombea `pump(plan, budget)` en un bucle en init hasta
	/// que `busy()` sea false (no hay límite de tiempo real durante el arranque).
	/// La página visible queda lista antes de mostrar el primer frame.
	bool begin(
		eng::MemorySystem& memory,
		const TileFieldConfig& config,
		TileScrollOffset initial
	) {
		m_config = config;
		m_fb_w = config.viewport_w * (config.scroll_x ? 2u : 1u);
		m_fb_h = config.viewport_h * (config.scroll_y ? 2u : 1u);
		m_row_bytes = m_fb_w / 8u;
		m_plane_bytes = static_cast<eng::u32>(m_row_bytes) * m_fb_h;

		m_framebuffer = memory.chip.allocate(
			m_plane_bytes * config.tileset_planes, 16
		);
		if (!m_framebuffer.valid()) {
			return false;
		}

		m_state.world_x = initial.x;
		m_state.world_y = initial.y;
		// Página 0: mundo alineado a viewport desde el offset inicial. Página 1:
		// el tramo siguiente (o anterior) según el scroll del eje.
		const eng::s32 base_x = (initial.x / static_cast<eng::s32>(config.viewport_w)) *
			static_cast<eng::s32>(config.viewport_w);
		const eng::s32 base_y = (initial.y / static_cast<eng::s32>(config.viewport_h)) *
			static_cast<eng::s32>(config.viewport_h);
		m_state.page_origin_x[0] = base_x;
		m_state.page_origin_x[1] = base_x + (config.scroll_x ? static_cast<eng::s32>(config.viewport_w) : 0);
		m_state.page_origin_y[0] = base_y;
		m_state.page_origin_y[1] = base_y + (config.scroll_y ? static_cast<eng::s32>(config.viewport_h) : 0);
		m_state.active_page_x = static_cast<eng::u8>(
			(initial.x >= base_x + static_cast<eng::s32>(config.viewport_w)) ? 1u : 0u
		);
		m_state.active_page_y = static_cast<eng::u8>(
			(initial.y >= base_y + static_cast<eng::s32>(config.viewport_h)) ? 1u : 0u
		);
		m_state.last_dir_x = 0;
		m_state.last_dir_y = 0;
		m_state.pending_count = 0;

		enqueue_initial_strips();
		m_state.initialized = true;
		return true;
	}

	/// Sigue dibujando franjas pendientes (estampado inicial o scroll) con el
	/// Blitter a través del plan, hasta `budget` tiles. Devuelve si queda algo.
	bool pump(eng::graphics::FramePlan& plan, eng::u8 budget) {
		if (!m_state.initialized) {
			return false;
		}
		draw_pending(plan, budget);
		return has_pending();
	}

	/// ¿Queda trabajo de dibujo pendiente (inicial o de scroll)?
	bool busy() const {
		return m_state.initialized && has_pending();
	}

	/// Avanza el campo un frame. `delta` es el desplazamiento respecto al frame
	/// anterior (se clampa a max_delta_x/y y a 0 si scroll_x/y es false).
	/// Encola las franjas de tiles que la cámara revelará y consume el
	/// presupuesto (max_tiles_per_frame) dibujando parte de las pendientes.
	bool update(const TileFieldConfig& config, TileScrollOffset delta, eng::graphics::FramePlan& plan) {
		if (!m_state.initialized) {
			return false;
		}
		m_config = config;

		// 1) Clamp del delta al contrato y a la posibilidad de scroll.
		eng::s16 dx = config.scroll_x ? clamp16(delta.x, config.max_delta_x) : 0;
		eng::s16 dy = config.scroll_y ? clamp16(delta.y, config.max_delta_y) : 0;
		if (config.scroll_x && !can_scroll_x(dx)) dx = 0;
		if (config.scroll_y && !can_scroll_y(dy)) dy = 0;

		// 2) Detecta INVERSIÓN de la cámara: al cambiar de signo, la página
		//    opuesta se reencola con el tramo de la NUEVA dirección (trasero si
		//    ahora retrocede, delantero si avanza). La inversión ocurre lejos del
		//    cruce, así que el redibujado llega a tiempo.
		if (config.scroll_x && dx != 0) {
			const eng::s16 dir = dx < 0 ? -1 : 1;
			if (m_state.last_dir_x != 0 && dir != m_state.last_dir_x) {
				reprep_page_x(dir);
			}
			m_state.last_dir_x = dir;
		}
		if (config.scroll_y && dy != 0) {
			const eng::s16 dir = dy < 0 ? -1 : 1;
			if (m_state.last_dir_y != 0 && dir != m_state.last_dir_y) {
				reprep_page_y(dir);
			}
			m_state.last_dir_y = dir;
		}

		// 3) Avanza la cámara y detecta cruce de página.
		m_state.world_x += dx;
		m_state.world_y += dy;
		const eng::s32 half_w = static_cast<eng::s32>(config.viewport_w);
		const eng::s32 half_h = static_cast<eng::s32>(config.viewport_h);
		const eng::s32 act_x = m_state.active_page_x;
		const eng::s32 act_y = m_state.active_page_y;
		const eng::s32 mod_x = m_state.world_x - m_state.page_origin_x[act_x];
		if (config.scroll_x && mod_x >= half_w) {
			// Cruzó hacia adelante: la página que queda atrás (la actual) se
			// rellena con el tramo que estará tras la nueva activa.
			const eng::u8 vacated = static_cast<eng::u8>(act_x);
			m_state.active_page_x = static_cast<eng::u8>(act_x ^ 1);
			m_state.page_origin_x[vacated] =
				m_state.page_origin_x[m_state.active_page_x] + half_w;
			enqueue_page_x(vacated, m_state.page_origin_x[vacated]);
		} else if (config.scroll_x && mod_x < 0) {
			// Cruzó hacia atrás: la página que queda atrás (la actual) se rellena
			// con el tramo que estará antes de la nueva activa.
			const eng::u8 vacated = static_cast<eng::u8>(act_x);
			m_state.active_page_x = static_cast<eng::u8>(act_x ^ 1);
			m_state.page_origin_x[vacated] =
				m_state.page_origin_x[m_state.active_page_x] - half_w;
			enqueue_page_x(vacated, m_state.page_origin_x[vacated]);
		}
		const eng::s32 mod_y = m_state.world_y - m_state.page_origin_y[act_y];
		if (config.scroll_y && mod_y >= half_h) {
			const eng::u8 vacated = static_cast<eng::u8>(act_y);
			m_state.active_page_y = static_cast<eng::u8>(act_y ^ 1);
			m_state.page_origin_y[vacated] =
				m_state.page_origin_y[m_state.active_page_y] + half_h;
			enqueue_page_y(vacated, m_state.page_origin_y[vacated]);
		} else if (config.scroll_y && mod_y < 0) {
			const eng::u8 vacated = static_cast<eng::u8>(act_y);
			m_state.active_page_y = static_cast<eng::u8>(act_y ^ 1);
			m_state.page_origin_y[vacated] =
				m_state.page_origin_y[m_state.active_page_y] - half_h;
			enqueue_page_y(vacated, m_state.page_origin_y[vacated]);
		}

		// 4) Consume el presupuesto: dibuja hasta max_tiles_per_frame por Blitter.
		draw_pending(plan, config.max_tiles_per_frame);
		return true;
	}

	/// Vista hardware (punteros + scroll + módulo) para el compositor.
	///
	/// Usa la fórmula canónica del driver de scroll (ACE/HRM), probada en las
	/// demos 101-104: `BPLCON1=(16-fine)&15` y puntero = `(scroll-1)&~15`, con
	/// `DDFSTRT=$30` (fetch de 42 bytes = 40 visibles + 2 de margen). Así
	/// `display_start == scroll` de forma continua en todo el rango, sin salto en
	/// el cruce de tile (fine 15 -> 0).
	FieldHardwareView hardware_view(eng::u8 first_hardware_plane) const {
		FieldHardwareView v;
		v.bitplanes = static_cast<const eng::u8*>(m_framebuffer.data);
		v.plane_stride = m_plane_bytes;
		const eng::s32 page_x = m_state.world_x - m_state.page_origin_x[m_state.active_page_x];
		const eng::s32 page_y = m_state.world_y - m_state.page_origin_y[m_state.active_page_y];
		// fetch_x = (scroll - 1) & ~15; a scroll=0 se clampa a 0 (el margen de 2
		// bytes de DDFSTRT=$30 ya cubre la palabra inicial).
		const eng::s32 fetch_px = page_x > 0 ? ((page_x - 1) & ~15) : 0;
		v.display_byte_offset =
			static_cast<eng::u32>(m_state.active_page_x) * (m_config.viewport_w / 8u) +
			static_cast<eng::u32>(fetch_px / 8) +
			static_cast<eng::u32>(m_state.active_page_y) *
				static_cast<eng::u32>(m_config.viewport_h) * m_row_bytes +
			static_cast<eng::u32>(page_y) * m_row_bytes;
		v.fine_x = static_cast<eng::u8>((16 - (page_x & 15)) & 15);
		// fetch de 42 bytes (DDFSTRT=$30): 40 visibles + 2 de margen.
		v.bpl1mod = static_cast<eng::u16>(m_row_bytes - (m_config.viewport_w / 8u + 2u));
		v.plane_count = m_config.tileset_planes;
		v.first_hardware_plane = first_hardware_plane;
		return v;
	}

	const eng::u8* bitplanes() const { return static_cast<const eng::u8*>(m_framebuffer.data); }
	const TileFieldState& state() const { return m_state; }

private:
	static eng::s16 clamp16(eng::s16 v, eng::s16 max) {
		if (v > max) return max;
		if (v < -max) return static_cast<eng::s16>(-max);
		return v;
	}

	bool can_scroll_x(eng::s16 dx) const {
		if (m_config.map.wrap_x != 0) return true;
		const eng::s32 page_cols = static_cast<eng::s32>(m_config.map.width) -
			m_state.page_origin_x[m_state.active_page_x] / static_cast<eng::s32>(m_config.tile_width);
		return dx < 0 || page_cols > static_cast<eng::s32>(m_config.viewport_w / m_config.tile_width);
	}

	bool can_scroll_y(eng::s16 dy) const {
		if (m_config.map.wrap_y != 0) return true;
		const eng::s32 page_rows = static_cast<eng::s32>(m_config.map.height) -
			m_state.page_origin_y[m_state.active_page_y] / static_cast<eng::s32>(m_config.tile_size);
		return dy < 0 || page_rows > static_cast<eng::s32>(m_config.viewport_h / m_config.tile_size);
	}

	/// Reencola la página opuesta con el tramo en la dirección `dir` tras una
	/// inversión de la cámara. `dir` = +1 (avanza) => tramo delantero;
	/// `dir` = -1 (retrocede) => tramo trasero.
	void reprep_page_x(eng::s16 dir) {
		const eng::u8 other = static_cast<eng::u8>(m_state.active_page_x ^ 1u);
		m_state.page_origin_x[other] = m_state.page_origin_x[m_state.active_page_x] +
			static_cast<eng::s32>(dir) * static_cast<eng::s32>(m_config.viewport_w);
		enqueue_page_x(other, m_state.page_origin_x[other]);
	}

	void reprep_page_y(eng::s16 dir) {
		const eng::u8 other = static_cast<eng::u8>(m_state.active_page_y ^ 1u);
		m_state.page_origin_y[other] = m_state.page_origin_y[m_state.active_page_y] +
			static_cast<eng::s32>(dir) * static_cast<eng::s32>(m_config.viewport_h);
		enqueue_page_y(other, m_state.page_origin_y[other]);
	}

	/// Encola el redibujado de la página física `slot` (X) con el tramo de mundo
	/// que empieza en `origin` (px).
	///
	/// La página X es una banda vertical de ancho viewport que cubre TODO el alto
	/// del framebuffer, que contiene dos páginas Y (si scroll_y). Cada mitad se
	/// rellena con su propio `world_y` (el de la página Y correspondiente), así
	/// que se encolan DOS franjas: una por página Y activa, otra por la opuesta.
	void enqueue_page_x(eng::u8 slot, eng::s32 origin) {
		const eng::s32 col = origin / static_cast<eng::s32>(m_config.tile_width);
		const eng::u16 w = static_cast<eng::u16>(m_config.viewport_w / m_config.tile_width);
		const eng::u16 ph = static_cast<eng::u16>(m_config.viewport_h / m_config.tile_size);
		const eng::u16 pw = static_cast<eng::u16>(slot * w);
		for (eng::u8 sy = 0; sy < (m_config.scroll_y ? 2u : 1u); ++sy) {
			const eng::u8 py = static_cast<eng::u8>(m_state.active_page_y ^ sy);
			const eng::s32 row = m_state.page_origin_y[py] / static_cast<eng::s32>(m_config.tile_size);
			enqueue_strip(col, row, pw, py * ph, w, ph);
		}
	}

	/// Encola el redibujado de la página física `slot` (Y) con el tramo de mundo
	/// que empieza en `origin` (px). Análogo a X: dos franjas, una por página X.
	void enqueue_page_y(eng::u8 slot, eng::s32 origin) {
		const eng::s32 row = origin / static_cast<eng::s32>(m_config.tile_size);
		const eng::u16 h = static_cast<eng::u16>(m_config.viewport_h / m_config.tile_size);
		const eng::u16 pw = static_cast<eng::u16>(m_config.viewport_w / m_config.tile_width);
		const eng::u16 ph = static_cast<eng::u16>(slot * h);
		for (eng::u8 sx = 0; sx < (m_config.scroll_x ? 2u : 1u); ++sx) {
			const eng::u8 px = static_cast<eng::u8>(m_state.active_page_x ^ sx);
			const eng::s32 col = m_state.page_origin_x[px] / static_cast<eng::s32>(m_config.tile_width);
			enqueue_strip(col, row, px * pw, ph, pw, h);
		}
	}

	/// Encola el estampado inicial: una franja por página física del framebuffer
	/// (grid de páginas de viewport x viewport). Cada una cubre un cuadrante.
	void enqueue_initial_strips() {
		const eng::u16 pw = static_cast<eng::u16>(m_config.viewport_w / m_config.tile_width);
		const eng::u16 ph = static_cast<eng::u16>(m_config.viewport_h / m_config.tile_size);
		for (eng::u16 sy = 0; sy < 2 && m_state.pending_count < 8; ++sy) {
			if (sy == 1 && !m_config.scroll_y) break;
			for (eng::u16 sx = 0; sx < 2 && m_state.pending_count < 8; ++sx) {
				if (sx == 1 && !m_config.scroll_x) break;
				const eng::s32 col = m_state.page_origin_x[sx] / static_cast<eng::s32>(m_config.tile_width);
				const eng::s32 row = m_state.page_origin_y[sy] / static_cast<eng::s32>(m_config.tile_size);
				enqueue_strip(col, row, sx * pw, sy * ph, pw, ph);
			}
		}
	}

	void enqueue_strip(eng::s32 world_tile_x, eng::s32 world_tile_y, eng::u16 fb_tile_x, eng::u16 fb_tile_y, eng::u16 width, eng::u16 height) {
		if (width == 0 || height == 0) {
			return;
		}
		// Slot libre: reutilizar el primer slot INACTIVO (ya terminado).
		//
		// IMPORTANTE: si hay una franja ACTIVA escribiendo al MISMO slot físico
		// (mismo fb_tile_x/fb_tile_y) pero con DISTINTO tramo de mundo, se
		// CANCELA: tras una inversión de la cámara el contenido correcto de esa
		// página es el nuevo tramo, y dejar dos franjas sobre el mismo slot
		// escribía contenido contradictorio ("petardazo" de plaquetas).
		eng::u8 free_slot = 0xff;
		for (eng::u8 i = 0; i < m_state.pending_count; ++i) {
			TilePendingStrip& strip = m_state.pending[i];
			if (!strip.active) {
				if (free_slot == 0xff) free_slot = i;
				continue;
			}
			if (strip.fb_tile_x == fb_tile_x && strip.fb_tile_y == fb_tile_y) {
				if (strip.world_tile_x == world_tile_x && strip.world_tile_y == world_tile_y) {
					return; // ya encolada con el mismo contenido
				}
				// Mismo slot físico, contenido distinto: cancelar la anterior.
				strip.active = false;
			}
		}
		if (free_slot == 0xff) {
			if (m_state.pending_count >= 8) {
				return; // sin slots libres: se descarta (poco frecuente)
			}
			free_slot = m_state.pending_count++;
		}
		m_state.pending[free_slot] = {
			world_tile_x, world_tile_y, fb_tile_x, fb_tile_y, width, height, 0, true,
		};
	}

	/// Construye un blit de copia de un tile desde el tileset al framebuffer.
	///
	/// El tileset guarda cada tile planar contiguo:
	///   [plano0: tile_size filas x words_por_fila] [plano1: ...] ...
	/// `tile_width_px` es múltiplo de 16: words_por_fila = tile_width/16, así un
	/// tile de 32/48/64px se copia en UNA pasada del Blitter (no en pasadas de
	/// 16px). El destino `fb_x_px`/`fb_y_px` son coordenadas del framebuffer
	/// (múltiple de 16 en X => word-aligned).
	eng::graphics::BlitJob make_tile_copy_job(
		eng::u16 tile_index,
		eng::s32 fb_x_px,
		eng::s32 fb_y_px,
		eng::u16 tile_width_px
	) const {
		const eng::u16 words_per_row = static_cast<eng::u16>(tile_width_px / 16u);
		const eng::u16 words_per_plane = static_cast<eng::u16>(m_config.tile_size * words_per_row);
		const eng::u16* src = m_config.tileset +
			static_cast<eng::u32>(tile_index & (m_config.tileset_count - 1u)) *
				static_cast<eng::u32>(m_config.tileset_planes * words_per_plane);
		eng::u16* dst = reinterpret_cast<eng::u16*>(
			static_cast<eng::u8*>(m_framebuffer.data) +
			static_cast<eng::u32>(fb_y_px) * m_row_bytes +
			static_cast<eng::u32>(fb_x_px) / 8u);
		return {
			eng::graphics::BlitJobKind::TileBlockCopy,
			nullptr,
			src,
			dst,
			words_per_row,
			m_config.tile_size,
			0, // source contiguo (módulo 0)
			static_cast<eng::s16>(m_row_bytes - words_per_row * 2u), // destino: cada fila salta al siguiente tile
			m_config.tileset_planes,
			0,
			static_cast<eng::u32>(words_per_plane) * sizeof(eng::u16), // stride fuente (bytes por plano)
			m_plane_bytes,                                            // stride destino
		};
	}

	/// Dibuja hasta `budget` tiles de las franjas pendientes (reparto entre
	/// frames) encolando blits TileBlockCopy en el FramePlan.
	void draw_pending(eng::graphics::FramePlan& plan, eng::u8 budget) {
		for (eng::u8 i = 0; i < m_state.pending_count && budget > 0; ++i) {
			TilePendingStrip& strip = m_state.pending[i];
			if (!strip.active) continue;
			const eng::u32 total = static_cast<eng::u32>(strip.width) * strip.height;
			while (strip.cursor < total && budget > 0) {
				const eng::u32 tx = strip.cursor % strip.width;
				const eng::u32 ty = strip.cursor / strip.width;
				const eng::s32 wx = strip.world_tile_x + static_cast<eng::s32>(tx);
				const eng::s32 wy = strip.world_tile_y + static_cast<eng::s32>(ty);
				const eng::u16 tile = m_config.map.tile_at(wx, wy);
				const eng::s32 fb_x = (static_cast<eng::s32>(strip.fb_tile_x) + static_cast<eng::s32>(tx)) *
					static_cast<eng::s32>(m_config.tile_width);
				const eng::s32 fb_y = (static_cast<eng::s32>(strip.fb_tile_y) + static_cast<eng::s32>(ty)) *
					static_cast<eng::s32>(m_config.tile_size);
				if (plan.add_tile_block_copy(
					make_tile_copy_job(tile, fb_x, fb_y, m_config.tile_width)
				)) {
					++strip.cursor;
					--budget;
				} else {
					break; // plan lleno: no más este frame
				}
			}
			if (strip.cursor >= total) {
				strip.active = false;
			}
		}
	}

	bool has_pending() const {
		for (eng::u8 i = 0; i < m_state.pending_count; ++i) {
			if (m_state.pending[i].active) {
				return true;
			}
		}
		return false;
	}

	TileFieldConfig m_config {};
	TileFieldState m_state {};
	eng::MemoryBlock m_framebuffer {};
	eng::u16 m_fb_w = 0;
	eng::u16 m_fb_h = 0;
	eng::u16 m_row_bytes = 0;
	eng::u32 m_plane_bytes = 0;
};

} // namespace eng::field
