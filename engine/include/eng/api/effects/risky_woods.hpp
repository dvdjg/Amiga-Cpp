#pragma once

#include <eng/graphics/blit_job.hpp>
#include <eng/graphics/playfield_scroll.hpp>
#include <eng/graphics/composition/compose.hpp>
#include <eng/graphics/composition/copper_chunky.hpp>
#include <eng/graphics/effects/raster_gradient.hpp>
#include <eng/graphics/effects/rotozoom.hpp>
#include <eng/graphics/sprite_channel_window.hpp>
#include <eng/core/types/memory_kind.hpp>

namespace eng::effects {

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
		/// al haz para escribir el primer `SPRxPOS` antes de que pase por esa X. Reposicionar
		/// un canal cuesta 2 `MOVE` (≈16 px lo-res) y el «impar» de un par *attached* debe
		/// llegar a tiempo o el par pierde los bits altos (cae a 4 colores): mínimo ≈16 px,
		/// con margen cómodo ≈32–56 px según el número de reposiciones. Ver
		/// `docs/debugging/investigaciones/consulta-ocs-attached-sprite-multiplexing-en.md`.
		u16 head_start = 32u;
		/// Si `true`, cada canal **impar** del tramo se marca como *attached* (bit 7 de su
		/// `SPRxCTL`): el par (par, impar) forma un sprite de 15 colores. El rearmado por
		/// línea (solo `SPRxPOS`) **conserva** el bit; el `head_start` debe permitir que la
		/// `SPRxPOS` del impar se escriba antes de que el haz alcance su X.
		bool attach = false;
		/// **Carrera de una sola ráfaga**: un `WAIT` al inicio de la línea y luego toda la ráfaga
		/// de `SPRxPOS` **sin** `WAIT`s intermedios. Necesario cuando el coste del Copper por
		/// periodo (`2N MOVE` + los 8 px del `WAIT`) supera el periodo del patrón (p. ej. 4 pares
		/// *attached*: `8 MOVE = 64 px = periodo`; el `WAIT` añadido daría 72 > 64 y el Copper se
		/// retrasaría 8 px por periodo hasta perder los bits altos del impar). Sin el `WAIT` el
		/// Copper corre **pareado** con el haz (`2N MOVE = periodo`). Los tramos no-*attached*
		/// deben dejarlo `false`: sin el `WAIT`, el `POS` de un canal pisa al del canal anterior.
		bool burst_no_wait = false;
		/// **Paleta por banda**: los valores a (re)cargar en los registros de color desde
		/// `palette_first_reg` (0 = `COLOR00`) al inicio de la banda. Permite que varias capas usen
		/// paletas distintas por franja (p. ej. la paleta de los objetos o un arcoiris en una sola
		/// banda) sin que el llamador emita los `MOVE` de `COLORxx` a mano.
		eng::Span<const eng::u16> palette {};
		u16 palette_first_reg = 0u;
		/// Si `true`, al final de la banda los canales quedan **desarmados** (`SPRxPOS`/`SPRxCTL`
		/// a `VSTART=VSTOP`): evita la **columna fantasma** / la franja sólida en la transición a la
		/// banda siguiente (ver `docs/reference/emulators/winuae/sprite-dma.md` §columna fantasma).
		bool reset_at_end = false;
		eng::Span<eng::u16> dma_data {}; ///< `channels` estructuras de `dma_stride` words (cabecera POS+CTL + DATA + terminador)
		u16 dma_stride = 0u;             ///< words por estructura (`2 + lines*2 + 2`)
	};

	/// Configura la capa. `false` si la geometría no es válida o falta la estructura DMA.
	[[nodiscard]] bool attach(Config cfg) {
		if (cfg.lines == 0u || cfg.channels == 0u || cfg.channels > 8u ||
		    cfg.channel_first + cfg.channels > 8u || cfg.column_width == 0u ||
		    cfg.screen_width == 0u || cfg.dma_data.empty() || cfg.dma_stride == 0u ||
		    cfg.dma_data.size() < static_cast<eng::usize>(cfg.channels) * cfg.dma_stride) {
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
		// Paleta por banda (si se pide): recarga los registros de color desde `palette_first_reg`.
		for (eng::usize i = 0u; i < m_cfg.palette.size(); ++i) {
			sched.move(static_cast<u16>(0x180u + (m_cfg.palette_first_reg + static_cast<u16>(i)) * 2u),
				   m_cfg.palette[i]);
		}

		// `SPRxPT` de cada canal a su estructura DMA (cabecera POS/CTL + DATA + terminador).
		// Sin cabecera válida el DMA del canal avanza por memoria y deja una columna fantasma
		// (ver `docs/reference/emulators/winuae/sprite-dma.md`).
		const eng::Address<eng::MemoryKind::Chip> base =
			eng::Address<eng::MemoryKind::Chip>::from_storage(m_cfg.dma_data.data());
		const u16 vstop0 = m_cfg.first_line + m_cfg.lines;
		for (u8 c = 0u; c < m_cfg.channels; ++c) {
			const eng::Address<eng::MemoryKind::Chip> addr = base + c * m_cfg.dma_stride * 2u;
			const u16 ch = m_cfg.channel_first + c;
			const u16 pt_reg = 0x120u + ch * 4u;
			// Guarda el **handle** (índice de la word de instrucción) para reescribir el `PT` por
			// frame sin re-emitir la lista (scroll suave a ~0 CPU; ver `patch_pt`).
			m_pt_handle[ch][0] = sched.move_at(pt_reg, static_cast<u16>(addr.value >> 16u)); // SPRxPTH
			m_pt_handle[ch][1] =
				sched.move_at(static_cast<u16>(pt_reg + 2u), static_cast<u16>(addr.value & 0xffffu)); // SPRxPTL
			// **Arma** el canal con un `SPRxCTL` válido (VSTART/VSTOP de la banda). Sin este
			// registro el canal queda con VSTOP=0 y no dibuja: la reposición por línea solo
			// escribe `SPRxPOS`, así que el VSTOP debe quedar fijado aquí. El DMA, al armar,
			// recarga POS/CTL de la cabecera de la estructura. El `SPRxPOS` de arranque se
			// pone **fuera de pantalla** (a la derecha): el Copper es el único que escribe las
			// X visibles en la ráfaga; si el arranque dejara una X visible, se vería una
			// columna "pegada" antes de la ráfaga.
			const u16 arm_x = static_cast<u16>((m_cfg.screen_width >> 1u) & 0xffu);
			sched.move(0x140u + ch * 8u, static_cast<u16>((m_cfg.first_line << 8u) | arm_x)); // SPRxPOS
			// Bit 7 = ATTACH en el canal IMPAR: el par (par, impar) forma 15 colores. Se fija
			// aquí (armado) y el rearmado por línea (solo `SPRxPOS`) lo conserva.
			const u16 ctl = static_cast<u16>((vstop0 << 8u) |
							 ((m_cfg.attach && (ch & 1u)) ? 0x80u : 0u));
			sched.move(0x142u + ch * 8u, ctl);                                     // SPRxCTL
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
				// Con `burst_no_wait` solo el primer periodo lleva `WAIT`: los demás van en la
				// misma ráfaga y el Copper corre pareado con el haz (ver el campo del `Config`).
				if (!m_cfg.burst_no_wait || x0 == first_x) {
					const eng::s32 wait_px = (x0 == first_x)
								 ? wait_px0
								 : (x0 - m_scroll - m_cfg.head_start);
					if (wait_px < 0) {
						continue;
					}
					sched.wait_position_safe(line, static_cast<u8>((wait_px >> 1u) & 0xfeu));
				}
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
		// Reset al final de la banda (si se pide): canales desarmados (sin columna fantasma).
		if (m_cfg.reset_at_end) {
			for (u8 c = 0u; c < m_cfg.channels; ++c) {
				const u8 ch = static_cast<u8>((m_cfg.channel_first + c) & 7u);
				sched.move(static_cast<u16>(0x142u + ch * 8u), 0xfe00u);
				sched.move(static_cast<u16>(0x140u + ch * 8u), 0xfe00u);
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

	/// Índice (en words) de la word de instrucción `SPRxPTH` (`hi=0`) o `SPRxPTL` (`hi=1`) de un
	/// canal, válido tras `emit_into`. Permite reescribir el `PT` por frame con `Sched::patch_data`
	/// para un scroll **~0 CPU** (sin re-emitir la lista). Ver `FreeFormSpriteLayer` (patch del scroll).
	[[nodiscard]] u16 pt_handle(u8 ch, u8 hi) const noexcept { return m_pt_handle[ch & 7u][hi & 1u]; }

	/// Huella estimada en palabras de Copper: `BPLCON2` (1 MOVE) + arranque (4 MOVEs por
	/// canal: `SPRxPTH/L`+`SPRxPOS`+`SPRxCTL`) + por línea [`periods` `WAIT` + `instances`
	/// MOVEs de `SPRxPOS`]. Cada instrucción son 2 palabras.
	[[nodiscard]] u16 words_estimate() const noexcept {
		const u32 arranque = 1u + static_cast<u32>(m_cfg.channels) * 4u +
				     static_cast<u32>(m_cfg.palette.size()) +
				     (m_cfg.reset_at_end ? static_cast<u32>(m_cfg.channels) * 2u : 0u);
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
	/// Handles de `SPRxPTH`/`SPRxPTL` capturados en `emit_into` (por canal), `mutable` porque el
	/// emisor es `const` pero sirve como caché de índices para el parcheo por frame.
	mutable u16 m_pt_handle[8][2] {};
};

} // namespace eng::effects
