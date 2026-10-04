#pragma once

/// \file effects.hpp
/// **Efectos de display de alto nivel** (borrador): envuelven las etapas internas para que el
/// juego pida un efecto, no una secuencia de primitivas de Copper. Ver
/// `docs/engine/architecture/PUBLIC_GAME_API.md`.

#include <eng/graphics/blit_job.hpp>
#include <eng/graphics/playfield_scroll.hpp>
#include <eng/graphics/composition/compose.hpp>
#include <eng/graphics/composition/copper_chunky.hpp>
#include <eng/graphics/effects/raster_gradient.hpp>
#include <eng/graphics/effects/rotozoom.hpp>
#include <eng/graphics/sprite_channel_window.hpp>
#include <eng/core/types/memory_kind.hpp>

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
	/// \param scene        escena a componer (modo `CopperChunky`).
	/// \param memory       gestor de memoria (Chip).
	/// \param limits       perfil de display (OCS/ECS/AGA).
	/// \param cfg          geometría del efecto `{.cols, .rows}`.
	/// \param width,height tamaño de pantalla del efecto.
	/// \param copper_bytes capacidad reservada de copperlist.
	/// \return `false` si no cabe.
	[[nodiscard]] bool init(graphics::composition::Scene& scene, eng::MemoryManager& memory,
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

	/// Aporta el degradado al plan del **setup** y **ata sus palabras de dato** (una sola vez):
	/// materializa las intenciones y registra dónde quedaron. Después, `patch` anima por frame
	/// reescribiendo solo esas palabras, sin re-emitir la copperlist (coste ~0 en el bucle).
	/// Equivale a la etapa que aporta intenciones fijas pero dejándolas **animables**.
	void bind(graphics::composition::Scene& scene) {
		m_fx.apply_into(scene.plan());
		scene.plan().materialize();
		m_fx.bind_slots(scene.plan());
	}

	/// Anima el degradado **parcheando** la lista ya construida (tras `bind`). Por frame.
	void patch(graphics::composition::Scene& scene) const { m_fx.patch_into(scene.plan()); }

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
		u16 arm_hpos = 0x40; ///< posición H del `WAIT` de rearmado: debe caer **después** del
		                     ///< fetch DMA de sprites (`DDFSTRT`) y **antes** de la primera
		                     ///< columna, para que la DATA del Copper gane a la del DMA.
		/// **Canales DMA** (los `dma_channels` primeros): sprite **alto** (toda la banda) cuya
		/// estructura en Chip RAM lleva **cabecera `POS`+`CTL`** (`[POS, CTL, DAT0, DATB0, …,
		/// 0, 0]`); `SPRxPT` apunta al **inicio** de la estructura y Agnus recarga POS/CTL de
		/// ahí. `SpriteLayer` parchea el POS de la cabecera (scroll). El resto de canales
		/// (Copper) rearman POS/DATA por línea, pero **también necesitan** su estructura:
		/// sin cabecera/terminador válidos el DMA del canal avanza por memoria y lee una
		/// cabecera basura (columna fantasma fuera de la banda). Ver
		/// `spr_layer/Sprite_Layer/` (Jeroen Knoester).
		u8 dma_channels = 0;
		u16* dma_data = nullptr; ///< estructura DMA por canal (`channels` estructuras de `dma_stride` words)
		u16 dma_stride = 0;      ///< words por estructura (`2 + dma_height*2 + 2`)
	};

	/// Configura la capa. `false` si `lines == 0`, `channels` fuera de 1..8, `hpos_step < 16`,
	/// `dma_channels > channels` o hay canales DMA sin `dma_data`/`dma_stride`. Si se da
	/// `dma_data`, debe haber **una estructura por canal** (`dma_stride` words cada una).
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

	/// Emite la capa completa. **Todos los canales** reciben `SPRxPT` apuntando a su
	/// estructura (`dma_data`), con `POS`+`CTL`: es lo que impide que el DMA de un canal
	/// Copper siga avanzando por memoria tras el terminador. **Canales DMA** (los primeros):
	/// solo la estructura (columna alta, posición parcheada con el scroll). **Canales
	/// Copper**: además, por cada línea, un `WAIT` en `arm_hpos` (después del fetch DMA) y
	/// una **ráfaga** de `SPRxCTL`+`SPRxPOS`+`SPRxDATB`+`SPRxDATA` que reescribe posición y
	/// data de la línea; `DATA` arma el sprite.
	template <class Sched>
	void emit_into(Sched& sched) const {
		sched.move(copper::Register::BPLCON2, m_cfg.bplcon2); // prioridad de fondo
		const u16 vstop = static_cast<u16>(m_cfg.first_line + m_cfg.lines);

		// --- Estructuras DMA: SPRxPT + cabecera POS+CTL para TODOS los canales ---
		// Cada canal (DMA y Copper) apunta a una estructura válida con cabecera y
		// terminador. Si un canal Copper quedara apuntando a una estructura nula corta,
		// el DMA del canal seguiría avanzando por memoria y leería basura como cabecera
		// (columna fantasma fuera de la banda). El Copper reescribe POS/DATA por línea
		// (más abajo), pero el DMA necesita una estructura consistente.
		if (m_cfg.dma_data != nullptr && m_cfg.dma_stride != 0u) {
			const eng::uintptr base = reinterpret_cast<eng::uintptr>(m_cfg.dma_data);
			for (u8 ch = 0u; ch < m_cfg.channels; ++ch) {
				const u16 hpos = static_cast<u16>(m_cfg.hpos0 +
								  static_cast<u16>(ch) * m_cfg.hpos_step + m_scroll);
				const u16 pos = static_cast<u16>(((m_cfg.first_line & 0xffu) << 8u) |
								 ((hpos >> 1u) & 0xffu));
				// Parchear el POS de la cabecera y apuntar PT a la estructura del canal.
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
			sched.wait_position_safe(line, static_cast<u8>(m_cfg.arm_hpos & 0xfeu));
			const u16 lstop = static_cast<u16>(line + 1u);
			const u16 ctl = static_cast<u16>(((lstop & 0xffu) << 8u) |
							 (((line >> 8u) & 0x1u) << 2u) |
							 (((lstop >> 8u) & 0x1u) << 1u));
			for (u8 ch = m_cfg.dma_channels; ch < m_cfg.channels; ++ch) {
				const u16 hpos = static_cast<u16>(m_cfg.hpos0 +
								  static_cast<u16>(ch) * m_cfg.hpos_step + m_scroll);
				const u16 pos = static_cast<u16>(((line & 0xffu) << 8u) |
								 ((hpos >> 1u) & 0xffu));
				if (m_binding && line == m_cfg.first_line && ch == m_cfg.dma_channels) {
					// Palabra de DATO del primer `SPRxPOS` (el MOVE son 2 palabras: registro +
					// valor; el POS es el 2.º de los 4 MOVEs del canal). `patch` reescribe desde
					// aquí con strides regulares (`+8` por canal, `+2` por el WAIT de la línea).
					// `if constexpr` para no exigir `words_used()` a schedulers de prueba.
					if constexpr (requires { sched.words_used(); }) {
						m_pos_base = static_cast<u16>(sched.words_used() + 3u);
						m_pos_valid = true;
					}
				}
				sched.move(static_cast<copper::Register>(0x142u + ch * 8u), ctl);          // SPRxCTL
				sched.move(static_cast<copper::Register>(0x140u + ch * 8u), pos);          // SPRxPOS
				sched.move(static_cast<copper::Register>(0x146u + ch * 8u), m_cfg.data_low);  // SPRxDATB
				sched.move(static_cast<copper::Register>(0x144u + ch * 8u), m_cfg.data_high); // SPRxDATA (arma)
			}
		}
	}

	/// Emite la capa al plan de la escena (azúcar de `emit_into(scene.scheduler())`).
	void frame(graphics::composition::Scene& scene) { emit_into(scene.scheduler()); }

	/// **Prepara el parcheo por frame**: emite la capa (como `emit_into`) **registrando** dónde
	/// caen las palabras `SPRxPOS`. Llamar **una vez** (setup). Después, `patch(words)` reescribe
	/// solo esas palabras con el scroll actual, sin re-emitir (coste ~0 por frame).
	template <class Sched>
	void bind(Sched& sched) {
		m_binding = true;
		emit_into(sched);
		m_binding = false;
	}

	/// Reescribe las palabras `SPRxPOS` (canales Copper, por línea) y el `POS` de cabecera de
	/// los canales DMA con el `m_scroll` actual. `words` = la copperlist ya materializada
	/// (p. ej. `scene.plan().active_words()`). Requiere haber llamado `bind` en el setup.
	void patch(u16* words) const noexcept {
		if (!m_pos_valid || words == nullptr) {
			return;
		}
		u16 idx = m_pos_base;
		const u16 vstop = static_cast<u16>(m_cfg.first_line + m_cfg.lines);
		for (u16 line = m_cfg.first_line; line < vstop; ++line) {
			for (u8 ch = m_cfg.dma_channels; ch < m_cfg.channels; ++ch) {
				words[idx] = pos_for(line, ch);
				idx = static_cast<u16>(idx + 8u);
			}
			idx = static_cast<u16>(idx + 2u); // WAIT de la línea siguiente
		}
		if (m_cfg.dma_data != nullptr && m_cfg.dma_stride != 0u) {
			for (u8 ch = 0u; ch < m_cfg.dma_channels; ++ch) {
				m_cfg.dma_data[static_cast<eng::u32>(ch) * m_cfg.dma_stride] =
					pos_for(m_cfg.first_line, ch);
			}
		}
	}

	/// Contrato `Effect`: avanza el estado temporal. La capa no anima por sí sola (el
	/// llamador fija el scroll con `set_scroll`), así que aquí no hace nada.
	void update(eng::u16) noexcept {}

	/// Tramo de raster que reclama la capa (`reserve_band`): la banda que cubre y la máscara
	/// de **sus canales**. Así `reserve_band` solo ve conflicto con otro efecto que use el
	/// mismo canal en líneas solapadas: dos fondos por sprites en canales distintos comparten
	/// scanlines (el límite es por canal, no por región de pantalla).
	[[nodiscard]] copper::BandScope band_scope() const noexcept {
		const u16 last = static_cast<u16>(m_cfg.first_line + m_cfg.lines - 1u);
		return copper::BandScope {m_cfg.first_line, last,
					  graphics::sprite_channel_register_mask(0u, m_cfg.channels)};
	}

	/// **Coste declarado** del efecto (huella estimada): nº de "aportaciones" (1 `BPLCON2`
	/// + un rearm por línea y canal Copper) y palabras de Copper (`words_estimate`).
	[[nodiscard]] copper::EffectCost effect_cost() const noexcept {
		const u32 copper_ch = static_cast<u32>(m_cfg.channels - m_cfg.dma_channels);
		const u32 intents = 1u + static_cast<u32>(m_cfg.lines) * copper_ch;
		return copper::EffectCost {
			static_cast<u16>(intents), static_cast<u16>(words_estimate())};
	}

	/// **Contrato `Effect`** sobre el plan de la escena: reserva la banda (`reserve_band`),
	/// anota el coste (`note_effect_cost`) y emite la capa. Devuelve `false` si la banda
	/// **solapa** con otro efecto o si el coste no cabe en el presupuesto; la lista se
	/// emite igualmente (el llamador decide abortar). Registrar como:
	/// `scene.add_effect([&layer](Scene& s) { layer.update(s.frame()); layer.apply_into(s.plan()); });`
	[[nodiscard]] bool apply_into(copper::Plan& plan) const {
		const copper::BandScope band = band_scope();
		const bool free = plan.reserve_band(band.first_line, band.last_line, band.register_mask);
		const bool fits = plan.note_effect_cost(effect_cost());
		emit_into(plan.scheduler());
		return free && fits;
	}

	[[nodiscard]] const Config& config() const noexcept { return m_cfg; }
	/// Huella estimada en palabras de Copper (para `EffectCost`): `BPLCON2` + 2 por canal
	/// con estructura (`SPRxPT` H/L) + por línea [`WAIT` (2) + 4 MOVEs por canal Copper].
	[[nodiscard]] u16 words_estimate() const noexcept {
		const u32 cop = static_cast<u32>(m_cfg.channels - m_cfg.dma_channels);
		const u32 struct_ch = (m_cfg.dma_data != nullptr && m_cfg.dma_stride != 0u)
					      ? static_cast<u32>(m_cfg.channels)
					      : static_cast<u32>(m_cfg.dma_channels);
		return static_cast<u16>(1u + struct_ch * 2u +
					static_cast<u32>(m_cfg.lines) * (2u + cop * 8u));
	}

private:
	/// `SPRxPOS` de la línea `line` y canal `ch` con el `m_scroll` actual.
	[[nodiscard]] u16 pos_for(u16 line, u8 ch) const noexcept {
		const u16 hpos = static_cast<u16>(m_cfg.hpos0 +
						  static_cast<u16>(ch) * m_cfg.hpos_step + m_scroll);
		return static_cast<u16>(((line & 0xffu) << 8u) | ((hpos >> 1u) & 0xffu));
	}

	Config m_cfg {};
	u16 m_scroll = 0;
	/// Palabra de DATO del primer `SPRxPOS` (canal Copper). La registra `bind` durante la
	/// emisión; `patch` reescribe desde ahí con strides regulares.
	mutable u16 m_pos_base = 0;
	mutable bool m_pos_valid = false; ///< `bind` registró la posición de parcheo
	bool m_binding = false;           ///< `emit_into` está registrando (lo activa `bind`)
};

/// **Fondo de patrón repetitivo por reposición de sprites (técnica *Risky Woods*).**
///
/// `channels` canales contiguos forman un patrón de una columna de `column_width` px cada
/// uno (típico 16). El canal se **arma una vez por línea** por DMA: su estructura en Chip
/// RAM lleva la cabecera `POS`/`CTL` y la DATA de cada línea; el Copper **solo reposiciona
/// `SPRxPOS`** para redibujar el patrón más a la derecha (1 MOVE por repetición, sin
/// recargar la DATA). Es más barato que el rearmado completo de `effects::SpriteLayer`
/// (técnica Free Form). Los canales del fondo se reservan en el `SpriteChannelLedger` (con
/// `SpriteChannelWindow`) y los restantes quedan libres para objetos. Ver
/// `docs/reference/amiga/techniques/sprite-horizontal-multiplex.md` y
/// `docs/engine/architecture/SPRITE_CHANNEL_WINDOWS.md`.
///
/// ```text
///   canal 2  ├─[16px]────────[16px]────────[16px]─┤   ← reposición de SPRxPOS (≥24 px)
///   canal 3  │  ... patrón de `channels` columnas que se repite a lo ancho ...
///   ...
///   canal 7  ├─[16px]────────[16px]────────[16px]─┤
///            canal 0,1 libres  → objetos tradicionales (sprite/BOB)
/// ```
class RiskyWoodsLayer {
public:
	/// Configuración de la capa.
	struct Config {
		u16 first_line = 0;      ///< primera línea de la banda (inclusive)
		u16 lines = 0;           ///< líneas que cubre la banda
		u8  channel_first = 0;   ///< primer canal del fondo (0..7)
		u8  channels = 0;        ///< nº de columnas = canales contiguos (1..8)
		u16 column_width = 16u;  ///< ancho de cada columna (px)
		u16 screen_width = 320u; ///< ancho a cubrir (px)
		u16 bplcon2 = 0u;        ///< prioridad (`BPLCON2`); fondo = detrás del playfield
		/// Posición H del `WAIT` de la línea (en unidades de hpos = px/2). Debe caer tras el
		/// fetch DMA de sprites (`DDFSTRT`) y antes de la primera columna. Risky Woods usa
		/// `0x30`; la primera columna cae `head_start` px más a la derecha.
		u8  arm_hpos = 0x30u;
		/// **Ventaja del haz**: px entre el `WAIT` y la primera columna. El Copper debe ganar
		/// al haz para escribir el primer `SPRxPOS` antes de que pase por esa X. ≈24 px.
		u16 head_start = 24u;
		u16* dma_data = nullptr; ///< `channels` estructuras de `dma_stride` words (cabecera POS+CTL + DATA + terminador)
		u16 dma_stride = 0u;     ///< words por estructura (`2 + lines*2 + 2`)
	};

	/// Configura la capa. `false` si la geometría no es válida o falta la estructura DMA.
	[[nodiscard]] bool attach(Config cfg) {
		if (cfg.lines == 0u || cfg.channels == 0u || cfg.channels > 8u ||
		    cfg.channel_first + cfg.channels > 8u || cfg.column_width == 0u ||
		    cfg.screen_width == 0u || cfg.dma_data == nullptr || cfg.dma_stride == 0u) {
			return false;
		}
		m_cfg = cfg;
		return true;
	}

	/// Desplaza la capa horizontalmente (px low-res). El patrón entra por la izquierda.
	void set_scroll(u16 x) noexcept { m_scroll = x; }

	/// Emite la capa: `BPLCON2`, `SPRxPT`+`SPRxCTL` de cada canal y, por línea, **un solo
	/// `WAIT`** seguido de una **ráfaga de `SPRxPOS`**. Es la carrera contra el haz de
	/// Risky Woods: el Copper escribe las posiciones de todo el ancho más rápido de lo que
	/// el haz avanza, ciclando los canales (`K % channels`) y con X creciente
	/// (`x0 + K*column_width`). La primera columna se coloca `head_start` px **por delante**
	/// del haz (`WAIT` en `hpos = x0 - head_start`); sin esa ventaja el haz adelanta a la
	/// primera columna y no se dibuja. **No** se reescribe `SPRxCTL` ni se intercalan WAITs
	/// (romperían la carrera). Ver `sprite-horizontal-multiplex.md` y la copperlist real de
	/// Risky Woods (WAIT hpos 0x30, primera columna 0x48, paso 16 px).
	template <class Sched>
	void emit_into(Sched& sched) const {
		sched.move(copper::Register::BPLCON2, m_cfg.bplcon2);

		// `SPRxPT` de cada canal a su estructura DMA (cabecera POS/CTL + DATA + terminador).
		// Sin cabecera válida el DMA del canal avanza por memoria y deja una columna fantasma
		// (ver `docs/reference/emulators/winuae/sprite-dma.md`).
		const eng::Address<eng::MemoryKind::Chip> base =
			eng::Address<eng::MemoryKind::Chip>::from_storage(m_cfg.dma_data);
		const u16 vstop0 = m_cfg.first_line + m_cfg.lines;
		for (u8 c = 0u; c < m_cfg.channels; ++c) {
			const eng::Address<eng::MemoryKind::Chip> addr = base + c * m_cfg.dma_stride * 2u;
			const u16 ch = m_cfg.channel_first + c;
			const u16 pt_reg = 0x120u + ch * 4u;
			sched.move(pt_reg, static_cast<u16>(addr.value >> 16u)); // SPRxPTH
			sched.move(pt_reg + 2u, static_cast<u16>(addr.value & 0xffffu)); // SPRxPTL
			// **Arma** el canal con un `SPRxCTL` válido (VSTART/VSTOP de la banda). Sin este
			// registro el canal queda con VSTOP=0 y no dibuja: la reposición por línea solo
			// escribe `SPRxPOS`, así que el VSTOP debe quedar fijado aquí. El DMA, al armar,
			// recarga POS/CTL de la cabecera de la estructura. El `SPRxPOS` de arranque se
			// pone **fuera de pantalla** (a la derecha): el Copper es el único que escribe las
			// X visibles en la ráfaga; si el arranque dejara una X visible, se vería una
			// columna "pegada" antes de la ráfaga.
			const u16 arm_x = static_cast<u16>((m_cfg.screen_width >> 1u) & 0xffu);
			sched.move(0x140u + ch * 8u, static_cast<u16>((m_cfg.first_line << 8u) | arm_x)); // SPRxPOS
			sched.move(0x142u + ch * 8u, static_cast<u16>(vstop0 << 8u));          // SPRxCTL
		}

		const u16 vstop = m_cfg.first_line + m_cfg.lines;
		// **Carrera contra el haz (patrón Risky Woods).** Por línea, un `WAIT` al inicio de
		// cada período del patrón y luego los `channels` MOVEs de `SPRxPOS` de ese período
		// (canales ciclando `2→…→7→2`, X creciente `column_width` px). La reutilización de un
		// canal cae a `period = channels*column_width` (96 px con 6×16), muy por encima del
		// mínimo ≈24 px.
		//
		// **Por qué funciona** (AHRM cap. 4 + análisis de Risky Woods): el comparador
		// horizontal de Denise está vivo; cuando el haz iguala el `SPRxPOS` del canal, el
		// sprite empieza a desplazar su DATA. Solo entonces es seguro reescribir `SPRxPOS` con
		// una X mayor, que volverá a disparar más tarde. El `WAIT` por período fija cada vuelta
		// del patrón a su X de forma determinista (el "Gromit colocando vías ante el tren"): la
		// variante de una sola ráfaga sin WAITs depende del robo de ciclos de la DMA de
		// bitplanes y colapsaba a 6 columnas en el emulador. El `SPRxCTL` NO se reescribe.
		const eng::s32 period = m_cfg.channels * m_cfg.column_width;
		// `arm_hpos` es el `WAIT` (px/2); la 1.ª columna cae `head_start` px después del
		// WAIT. Con `arm_hpos=0` y `head_start=24`, el patrón arranca a 24 px y cubre de
		// izquierda a derecha. Ajusta `arm_hpos` si el display empieza más adentro.
		const eng::s32 wait_px0 = static_cast<eng::s32>(m_cfg.arm_hpos) * 2;
		const eng::s32 first_x = wait_px0 + m_cfg.head_start;
		for (u16 line = m_cfg.first_line; line < vstop; ++line) {
			for (eng::s32 x0 = first_x; x0 - m_scroll < m_cfg.screen_width; x0 += period) {
				const eng::s32 wait_px = (x0 == first_x)
							 ? wait_px0
							 : (x0 - m_scroll - m_cfg.head_start);
				if (wait_px < 0) {
					continue;
				}
				sched.wait_position_safe(line, static_cast<u8>((wait_px >> 1u) & 0xfeu));
				for (u8 c = 0u; c < m_cfg.channels; ++c) {
					const eng::s32 px = x0 + c * m_cfg.column_width - m_scroll;
					if (px < 0 || px >= m_cfg.screen_width) {
						continue;
					}
					const u8 ch = static_cast<u8>((m_cfg.channel_first + c) & 7u);
					sched.move(0x140u + ch * 8u,
						   static_cast<u16>((line << 8u) | ((px >> 1u) & 0xffu)));
				}
			}
		}
	}

	/// Emite la capa al plan de la escena (azúcar de `emit_into(scene.scheduler())`).
	void frame(graphics::composition::Scene& scene) { emit_into(scene.scheduler()); }

	/// Contrato `Effect`: la capa no anima por sí sola (el llamador fija el scroll).
	void update(eng::u16) noexcept {}

	/// Tramo de raster que reclama la capa (`reserve_band`): la banda que cubre y la máscara
	/// de **sus canales** (0 con bits = los del fondo). Permite que otro fondo por sprites en
	/// canales disjuntos solape scanlines; conflicto solo si comparten canal.
	[[nodiscard]] copper::BandScope band_scope() const noexcept {
		const u16 last = m_cfg.first_line + m_cfg.lines - 1u;
		return copper::BandScope {m_cfg.first_line, last,
					  graphics::sprite_channel_register_mask(m_cfg.channel_first,
										 m_cfg.channels)};
	}

	/// **Contrato `Effect`** sobre el plan: reserva la banda, anota el coste y emite. `false`
	/// si la banda solapa con otro efecto o el coste no cabe (la lista se emite igual).
	[[nodiscard]] bool apply_into(copper::Plan& plan) const {
		const copper::BandScope band = band_scope();
		const bool free = plan.reserve_band(band.first_line, band.last_line, band.register_mask);
		const bool fits = plan.note_effect_cost(effect_cost());
		emit_into(plan.scheduler());
		return free && fits;
	}

	/// Coste declarado del efecto (intenciones y palabras de Copper estimadas).
	[[nodiscard]] copper::EffectCost effect_cost() const noexcept {
		const u16 instances = instances_per_line();
		const u16 intents = 1u + m_cfg.lines * instances;
		return copper::EffectCost {intents, words_estimate()};
	}

	[[nodiscard]] const Config& config() const noexcept { return m_cfg; }

	/// Huella estimada en palabras de Copper: `BPLCON2` (1 MOVE) + arranque (4 MOVEs por
	/// canal: `SPRxPTH/L`+`SPRxPOS`+`SPRxCTL`) + por línea [`periods` `WAIT` + `instances`
	/// MOVEs de `SPRxPOS`]. Cada instrucción son 2 palabras.
	[[nodiscard]] u16 words_estimate() const noexcept {
		const u32 arranque = 1u + static_cast<u32>(m_cfg.channels) * 4u;
		const u32 per_line = static_cast<u32>(periods_per_line()) + instances_per_line();
		// +2 words del `end()` (0xffff,0xfffe) que cierra la lista.
		return static_cast<u16>((arranque + static_cast<u32>(m_cfg.lines) * per_line) * 2u + 2u);
	}

private:
	/// Periodos del patrón con al menos una columna visible en la línea actual.
	[[nodiscard]] u16 periods_per_line() const noexcept {
		const eng::s32 wait_px0 = static_cast<eng::s32>(m_cfg.arm_hpos) * 2;
		const eng::s32 first_x = wait_px0 + m_cfg.head_start;
		const eng::s32 period = static_cast<eng::s32>(m_cfg.channels) * m_cfg.column_width;
		if (period <= 0) {
			return 0u;
		}
		u16 count = 0u;
		for (eng::s32 x0 = first_x; x0 - m_scroll < m_cfg.screen_width; x0 += period) {
			++count;
		}
		return count;
	}

	/// Tramos de columna por línea (MOVEs de `SPRxPOS`), desde la primera columna (tras el
	/// scroll) hasta `screen_width`. Sin división.
	[[nodiscard]] u16 instances_per_line() const noexcept {
		const eng::s32 first = m_cfg.arm_hpos * 2 + m_cfg.head_start - m_scroll;
		const eng::s32 seen = (first < 0) ? 0 : first;
		if (seen >= m_cfg.screen_width) {
			return 0u;
		}
		return static_cast<u16>((m_cfg.screen_width - seen + m_cfg.column_width - 1u) /
					m_cfg.column_width);
	}

	Config m_cfg {};
	u16 m_scroll = 0;
};

/// **Scroll horizontal fino de una capa planar** (`visible_words` words = 320 px). Mantiene el
/// estado (**fine 0..15** + **columna absoluta**) y produce la `BPLCON1` del frame y, al cruzar
/// el word, los `graphics::BlitJob` de **desplazamiento** + **columna nueva**. Se apoya en
/// `DDFSTRT = $30` (fetch de 1 word extra) y un buffer de `visible_words + 1` words/fila
/// (guarda + columna entrante): es el patrón del driver `graphics/drivers/tile_scroll.hpp`,
/// extraído como helper reutilizable. El contenido lo genera el llamador (procedural o tilemap).
///
/// ```cpp
/// eng::effects::FineScroll scroll;
/// scroll.attach({.plane = plane, .rows = 256});
/// // por frame (tras el arranque, con la lista montada con `scroll.ddfstrt()`/`bplcon1()`):
/// if (scroll.step()) {                        // fine cruzó 16: shift + columna
///     fill_column(scroll.column(), col);      // el llamador llena `col`
///     backend.blitter_submit(scroll.shift_job(), true);
///     backend.blitter_submit(scroll.column_job(col.data()), true);
/// }
/// bplcon1 = scroll.bplcon1();                 // p. ej. move(BPLCON1, bplcon1)
/// ```
class FineScroll {
public:
	struct Config {
		u16* plane = nullptr;    ///< base del plano (Chip RAM)
		u16 rows = 0;            ///< filas (256)
		u16 visible_words = 20u; ///< words visibles (320 px)
	};

	/// Configura la capa. `false` si `plane == nullptr`, `rows == 0` o `visible_words == 0`.
	/// \param cfg  `{plane (base Chip), rows, visible_words}`.
	/// \return `true` si la config es válida.
	[[nodiscard]] bool attach(Config cfg) {
		if (cfg.plane == nullptr || cfg.rows == 0u || cfg.visible_words == 0u) {
			return false;
		}
		m_cfg = cfg;
		m_fine = 0;
		m_column = cfg.visible_words;
		return true;
	}

	/// Bytes por fila del buffer (`visible_words + 1` words: guarda/entrante).
	[[nodiscard]] u16 row_bytes() const noexcept {
		return static_cast<u16>((static_cast<u32>(m_cfg.visible_words) + 1u) * 2u);
	}
	/// `DDFSTRT` del display con **1 word extra** a la izquierda (fine scroll).
	[[nodiscard]] static constexpr u16 ddfstrt() noexcept { return graphics::fine_scroll_ddfstrt; }

	/// Valor de `BPLCON1` del frame (`delay`, `(16 − fine) & 15`).
	[[nodiscard]] u16 bplcon1() const noexcept { return graphics::fine_delay(m_fine); }

	/// Avanza **1 px** el scroll. `true` si toca el **shift de columna** (fine cruzó 16).
	bool step() noexcept {
		if (++m_fine < 16u) {
			return false;
		}
		m_fine = 0;
		++m_column;
		return true;
	}

	/// Columna **absoluta** que entra (para generar su contenido).
	[[nodiscard]] u16 column() const noexcept { return m_column; }
	[[nodiscard]] const Config& config() const noexcept { return m_cfg; }

	/// `BlitJob` del **desplazamiento** de una columna a la izquierda (words 0..v−1 = 1..v).
	[[nodiscard]] graphics::BlitJob shift_job() const noexcept {
		graphics::BlitJob j {};
		j.kind = graphics::BlitJobKind::CopyRect;
		j.source = graphics::BlitPtr::from_storage(m_cfg.plane + 1u);
		j.destination = graphics::BlitPtr::from_storage(m_cfg.plane);
		j.words_per_row = m_cfg.visible_words;
		j.height = m_cfg.rows;
		j.source_modulo_bytes = 2;
		j.destination_modulo_bytes = 2;
		j.bitplane_count = 1u;
		return j;
	}

	/// `BlitJob` de la **columna nueva** (word `visible_words`), con `col` de `rows` words.
	[[nodiscard]] graphics::BlitJob column_job(const u16* col) const noexcept {
		graphics::BlitJob j {};
		j.kind = graphics::BlitJobKind::CopyRect;
		j.source = graphics::BlitPtr::from_storage(col);
		j.destination = graphics::BlitPtr::from_storage(m_cfg.plane + m_cfg.visible_words);
		j.words_per_row = 1u;
		j.height = m_cfg.rows;
		j.source_modulo_bytes = 0;
		j.destination_modulo_bytes = static_cast<s16>(row_bytes() - 2u);
		j.bitplane_count = 1u;
		return j;
	}

private:
	Config m_cfg {};
	u16 m_fine = 0;
	u16 m_column = 0;
};

} // namespace eng::effects

