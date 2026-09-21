#pragma once

/// \file effects.hpp
/// **Efectos de display de alto nivel** (borrador): envuelven las etapas internas para que el
/// juego pida un efecto, no una secuencia de primitivas de Copper. Ver
/// `docs/engine/architecture/PUBLIC_GAME_API.md`.

#include <eng/graphics/composition/compose.hpp>
#include <eng/graphics/composition/copper_chunky.hpp>
#include <eng/graphics/effects/raster_gradient.hpp>
#include <eng/graphics/effects/rotozoom.hpp>

namespace eng::effects {

/// **Display copper chunky** de alto nivel: compone la escena (sin bitplanes, modo
/// `SceneMode::CopperChunky`) y emite la estructura de la lista; el juego escribe los colores
/// del frame con `row(y)`.
///
/// ```cpp
/// eng::effects::CopperChunky fx;
/// fx.init(scene, memory, limits, {.cols = 36, .rows = 64});
/// // por frame:
/// fx.begin_frame(scene);
/// for (y...) { u16* p = fx.row(y); ... }   // escribe los colores
/// fx.end_frame(scene, backend);            // flip + install
/// ```
template <eng::u8 MaxCols = 64, eng::u8 MaxRows = 64>
class CopperChunky {
public:
	/// Compone la escena copper-chunky en `scene` (con `memory`/`limits`) y emite la estructura
	/// de la lista en los dos bloques del `Plan`. `false` si no cabe (geometría o memoria).
	[[nodiscard]] bool init(graphics::composition::Scene& scene, eng::MemorySystem& memory,
				const graphics::composition::DisplayLimits& limits,
				graphics::composition::CopperChunkyConfig cfg,
				eng::u16 width = 288u, eng::u16 height = 256u,
				eng::u32 copper_bytes = 12288u) {
		graphics::composition::SceneResources res =
			graphics::composition::planar(width, height, 0u);
		res.mode = graphics::composition::SceneMode::CopperChunky;
		res.copper_bytes = copper_bytes;
		if (!graphics::composition::compose(scene, memory, res, limits)) {
			return false;
		}
		return m_layer.attach(scene, cfg);
	}

	/// Inicia el frame (destino = bloque inactivo de la copperlist).
	void begin_frame(graphics::composition::Scene& scene) { m_layer.begin_frame(scene); }

	/// `data` del bloque 0 de la fila `row` para escribir los colores (paso de 2 words).
	[[nodiscard]] eng::u16* row(eng::u8 r) const { return m_layer.row(r); }

	/// Cierra el frame: voltea al bloque escrito y lo publica (swap de `COP1LC`).
	template <typename Backend>
	void end_frame(graphics::composition::Scene& scene, Backend& backend) {
		m_layer.end_frame(scene, backend);
	}

	[[nodiscard]] eng::u8 cols() const { return m_layer.cols(); }
	[[nodiscard]] eng::u8 rows() const { return m_layer.rows(); }

private:
	graphics::composition::CopperChunkyLayer<MaxCols, MaxRows> m_layer {};
};

/// **Degradado por banda de alto nivel**: envuelve `RasterGradientEffect` para que el juego
/// pida el efecto, no la lista de `CopperIntent`. Cumple el contrato `Effect` (ver
/// `docs/engine/architecture/EFFECT_MODEL.md`).
///
/// ```cpp
/// eng::effects::Gradient sky;
/// sky.attach({.first_line = 0x2c, .band_height = 8, .bands = 24, .first = 0}, keys);
/// // por frame:
/// sky.set_phase(frame);        // anima el degradado
/// sky.frame(scene);            // aporta las intenciones al plan de la escena
/// ```
class Gradient {
public:
	/// Configura rango y colores clave. `false` si no hay claves (nada que emitir).
	[[nodiscard]] bool attach(graphics::effects::RasterGradientRange range,
				  eng::Span<const eng::u16> keys, bool cyclic = true) {
		m_fx.configure(range);
		m_fx.set_keys(keys);
		m_fx.set_cyclic(cyclic);
		return m_fx.key_count() != 0u;
	}

	/// Avanza el estado temporal con el `tick` (índice de frame). El degradado anima por
	/// `set_phase`; aquí solo se satisface el contrato `Effect::update`.
	void update(eng::u16) noexcept {}

	/// Desplaza el muestreo en unidades de clave (animación del degradado).
	void set_phase(eng::u16 phase) noexcept { m_fx.set_phase(phase); }

	/// Aporta las intenciones de Copper a cualquier plan con `add(CopperIntent*, u16)`.
	template <typename Plan>
	void apply_into(Plan& plan) {
		m_fx.apply_into(plan);
	}

	/// Aporta al plan de la escena (azúcar de `apply_into(scene.plan())`).
	void frame(graphics::composition::Scene& scene) { m_fx.apply_into(scene.plan()); }

	[[nodiscard]] eng::u16 bands() const noexcept { return m_fx.bands(); }

private:
	graphics::effects::RasterGradientEffect m_fx {};
};

/// **Rotozoom de alto nivel**: estado (ángulo/zoom/offset) + render de la textura indexada
/// a un `ChunkyBuffer` (para C2P). Envuelve `graphics::rotozoom_into`
/// (`graphics/effects/rotozoom.hpp`); es un `RenderEffect` (escribe píxeles, no Copper).
class Rotozoom {
public:
	/// Fija los parámetros (punto fijo 16.16; `angle` en fases 0..255).
	void configure(graphics::Rotozoom r) noexcept { m_r = r; }
	/// Anima la rotación (fase 0..255).
	void set_angle(eng::u16 phase) noexcept { m_r.angle = phase; }
	[[nodiscard]] const graphics::Rotozoom& params() const noexcept { return m_r; }

	/// Rota/escala la textura `TW×TH` a `dst` (`w×h` índices; `w` múltiplo de 16).
	template <eng::u16 TW, eng::u16 TH>
	void render(eng::IndexedTexture tex, eng::ChunkyBuffer dst, eng::u16 w, eng::u16 h) const {
		graphics::rotozoom_into<TW, TH>(tex, m_r, dst, w, h);
	}

private:
	graphics::Rotozoom m_r {};
};

/// **Capa de fondo con canales de sprite rearmados horizontalmente** (sprite-as-playfield):
/// los canales se reposicionan y recargan su DATA a lo largo de cada línea para cubrir el
/// ancho de la pantalla, **sin coste de bitplanes**. Es una capa de **fondo**: los sprites
/// quedan detrás del playfield (prioridad `BPLCON2`). Ver
/// `docs/reference/amiga/techniques/sprite-horizontal-multiplex.md`.
class SpriteLayer {
public:
	struct Config {
		u16 first_line = 0; ///< primera línea del efecto
		u16 lines = 0;      ///< nº de líneas que cubre
		u8 channels = 8;    ///< canales usados (1..8)
		u16 hpos0 = 0;      ///< x del primer tramo (low-res px)
		u16 hpos_step = 0;  ///< separación entre tramos (>=16 px; columnas contiguas)
		u16 data_high = 0;  ///< SPRxDATA (primera palabra de la fila) — canales Copper
		u16 data_low = 0;   ///< SPRxDATB (segunda palabra) — canales Copper
		u16 bplcon2 = 0;    ///< prioridad (`BPLCON2`); sprites detrás del playfield = fondo
		/// **Canales DMA** (los `dma_channels` primeros): sprite **alto** (toda la banda) cuya
		/// estructura en Chip RAM lleva **cabecera `POS`+`CTL`** (`[POS, CTL, DAT0, DATB0, …,
		/// 0, 0]`); `SPRxPT` apunta al **inicio** de la estructura y Agnus recarga POS/CTL de
		/// ahí. `SpriteLayer` parchea el POS de la cabecera (scroll). Ver
		/// `spr_layer/Sprite_Layer/` (Jeroen Knoester).
		u8 dma_channels = 0;
		u16* dma_data = nullptr; ///< estructuras DMA (una por canal, `dma_stride` words cada una)
		u16 dma_stride = 0;      ///< words por estructura (`2 + dma_height*2 + 2`)
	};

	/// Configura la capa. `false` si `lines == 0`, `channels` fuera de 1..8, `hpos_step < 16`,
	/// `dma_channels > channels` o hay canales DMA sin `dma_data`/`dma_stride`.
	[[nodiscard]] bool attach(Config cfg) {
		if (cfg.lines == 0u || cfg.channels == 0u || cfg.channels > 8u ||
		    cfg.hpos_step < 16u || cfg.dma_channels > cfg.channels ||
		    (cfg.dma_channels > 0u && (cfg.dma_data == nullptr || cfg.dma_stride == 0u))) {
			return false;
		}
		m_cfg = cfg;
		return true;
	}

	/// Desplaza la capa horizontalmente (px low-res; el paso entre columnas no cambia).
	void set_scroll(u16 x) noexcept { m_scroll = x; }

	/// Emite la capa completa. **Canales DMA**: `SPRxPT`+`CTL`+`POS` (columna alta, posición
	/// con el scroll; no se rearman por línea). **Canales Copper**: patrón de Jeroen Knoester
	/// (`spr-layer.html`) — **un `WAIT` al inicio de cada línea** y luego **ráfaga** de
	/// `SPRxPOS`+`SPRxDATB`+`SPRxDATA` (el `SPRxCTL` se fija una vez por banda; `DATA` arma).
	template <class Sched>
	void emit_into(Sched& sched) const {
		sched.move(copper::Register::BPLCON2, m_cfg.bplcon2); // prioridad de fondo
		const u16 vstop = static_cast<u16>(m_cfg.first_line + m_cfg.lines);

		// --- Canales DMA: estructura con CABECERA POS+CTL; SPRxPT apunta a ella ---
		if (m_cfg.dma_channels > 0u) {
			const eng::uintptr base = reinterpret_cast<eng::uintptr>(m_cfg.dma_data);
			for (u8 ch = 0u; ch < m_cfg.dma_channels; ++ch) {
				const u16 hpos = static_cast<u16>(m_cfg.hpos0 +
								  static_cast<u16>(ch) * m_cfg.hpos_step + m_scroll);
				const u16 pos = static_cast<u16>(((m_cfg.first_line & 0xffu) << 8u) |
								 ((hpos >> 1u) & 0xffu));
				// Parchear el POS de la cabecera DMA (memoria) y apuntar PT a la estructura.
				m_cfg.dma_data[static_cast<eng::u32>(ch) * m_cfg.dma_stride] = pos;
				const eng::uintptr addr =
					base + static_cast<eng::uintptr>(ch) * m_cfg.dma_stride * 2u;
				sched.move(static_cast<copper::Register>(0x120u + ch * 4u),
					   static_cast<u16>(addr >> 16));                 // SPRxPTH
				sched.move(static_cast<copper::Register>(0x122u + ch * 4u),
					   static_cast<u16>(addr & 0xffffu));             // SPRxPTL
			}
		}

		// --- Canales Copper: rearm por línea (WAIT + CTL/POS/DATB/DATA) ---
		for (u16 line = m_cfg.first_line; line < vstop; ++line) {
			sched.wait_line_safe(line);
			const u16 lstop = static_cast<u16>(line + 1u);
			const u16 ctl = static_cast<u16>(((lstop & 0xffu) << 8u) |
							 (((line >> 8u) & 0x1u) << 2u) |
							 (((lstop >> 8u) & 0x1u) << 1u));
			for (u8 ch = m_cfg.dma_channels; ch < m_cfg.channels; ++ch) {
				const u16 hpos = static_cast<u16>(m_cfg.hpos0 +
								  static_cast<u16>(ch) * m_cfg.hpos_step + m_scroll);
				const u16 pos = static_cast<u16>(((line & 0xffu) << 8u) |
								 ((hpos >> 1u) & 0xffu));
				sched.move(static_cast<copper::Register>(0x142u + ch * 8u), ctl);          // SPRxCTL
				sched.move(static_cast<copper::Register>(0x140u + ch * 8u), pos);          // SPRxPOS
				sched.move(static_cast<copper::Register>(0x146u + ch * 8u), m_cfg.data_low);  // SPRxDATB
				sched.move(static_cast<copper::Register>(0x144u + ch * 8u), m_cfg.data_high); // SPRxDATA (arma)
			}
		}
	}

	/// Emite la capa al plan de la escena (azúcar de `emit_into(scene.scheduler())`).
	void frame(graphics::composition::Scene& scene) { emit_into(scene.scheduler()); }

	[[nodiscard]] const Config& config() const noexcept { return m_cfg; }
	/// Huella estimada en palabras de Copper (para `EffectCost`): `BPLCON2` + 2 por canal DMA
	/// (PT H/L) + por línea [`WAIT` (2) + 4 MOVEs por canal Copper].
	[[nodiscard]] u16 words_estimate() const noexcept {
		const u32 cop = static_cast<u32>(m_cfg.channels - m_cfg.dma_channels);
		return static_cast<u16>(1u + static_cast<u32>(m_cfg.dma_channels) * 2u +
					static_cast<u32>(m_cfg.lines) * (2u + cop * 8u));
	}

private:
	Config m_cfg {};
	u16 m_scroll = 0;
};

} // namespace eng::effects
