// Lanzar:
//   Depurar   : bash ./tools/build/build-demo.sh demos/techniques/amiga/sprites/208_risky_woods --debug   && bash ./tools/run/run-demo.sh demos/techniques/amiga/sprites/208_risky_woods --keep-running
//   Optimizada: bash ./tools/build/build-demo.sh demos/techniques/amiga/sprites/208_risky_woods --release && bash ./tools/run/run-demo.sh demos/techniques/amiga/sprites/208_risky_woods --keep-running

// ============================================================================
// Demo 208 — fondo por sprites *Risky Woods* + 2 canales libres para objetos.
// ============================================================================
//
// Materializa el reparto híbrido de `docs/engine/architecture/SPRITE_BANDS.md`:
//
//   - `RiskyWoodsLayer` (`eng/api/effects.hpp`) dibuja una banda con **6 canales de
//     sprite** (2..7) que forman un patrón de 6 columnas de 16 px repetido a lo ancho.
//     El canal se arma por DMA (estructura con cabecera POS/CTL) y el Copper **solo
//     reposiciona `SPRxPOS`** por línea (técnica Risky Woods; 1 MOVE por repetición).
//   - La banda reserva esos 6 canales en el `SpriteChannelLedger` con
//     `plan_sprite_bands`; `SpriteAllocator::assign` reparte los **2 objetos** en los
//     canales que quedan libres (0 y 1), delante del fondo (menor canal = más prioridad).
//   - Los objetos son sprites hardware que rebotan; su posición se parchea en la
//     cabecera DMA cada frame (el DMA la relee al armar el sprite).
//
// La playfield va en color 0 (navy): el fondo y los objetos salen **solo de sprites**,
// dejando los bitplanes libres para un primer plano (patrón Jim Power / Risky Woods).

#include <eng/api/api.hpp>
#include <eng/api/effects.hpp>
#include <eng/graphics/copper/scheduler.hpp>
#include <eng/graphics/sprite_allocator.hpp>
#include <eng/graphics/sprite_band.hpp>
#include <eng/platform/amiga/backend.hpp>

#include <exec/execbase.h>
#include <proto/exec.h>

#include "support/gcc8_c_support.h"

struct ExecBase* SysBase = nullptr;

extern "C" {
__attribute__((used)) volatile eng::debug::RunStatus g_eng_run_status {
	eng::debug::run_status_magic,
	eng::debug::run_status_version,
	static_cast<eng::u16>(eng::debug::RunState::Cold),
	0,
	0,
};
}

namespace {

// Display 320x256, 4 planos.
constexpr eng::u16 kBytesPerRow = 40;
constexpr eng::u8  kPlanes = 4;
constexpr eng::u32 kPlaneBytes = static_cast<eng::u32>(kBytesPerRow) * 256u;
constexpr eng::u32 kBitplaneBytes = kPlaneBytes * kPlanes;

// Banda del fondo.
constexpr eng::u16 kBandTop = 80;
constexpr eng::u16 kBandLines = 96;
constexpr eng::u16 kBandBottom = static_cast<eng::u16>(kBandTop + kBandLines); // 176
constexpr eng::u8  kBgChannels = 6;   // columnas del patrón
constexpr eng::u8  kBgFirst = 2;      // canales 2..7
constexpr eng::u16 kColumnWidth = 16;
constexpr eng::u16 kScreenWidth = 320;
constexpr eng::u16 kPeriod = static_cast<eng::u16>(kBgChannels * kColumnWidth); // 96 px (potencia de 2 no; se usa mascara abajo)
// Estructura DMA por canal del fondo: cabecera POS+CTL + DATA por línea + terminador.
constexpr eng::u16 kBgStride = static_cast<eng::u16>(2u + kBandLines * 2u + 2u); // 196

// Objetos (canales libres 0 y 1).
constexpr eng::u8  kObjects = 2;
constexpr eng::u16 kObjY = 120;
constexpr eng::u8  kObjH = 16;
constexpr eng::u16 kObjVstop = static_cast<eng::u16>(kObjY + kObjH); // 136
constexpr eng::u16 kObjStride = static_cast<eng::u16>(2u + kObjH * 2u + 2u); // 36
constexpr eng::u16 kObjMaxX = static_cast<eng::u16>(kScreenWidth - kColumnWidth); // 304

constexpr eng::u16 kSpriteWords =
	static_cast<eng::u16>(kBgChannels * kBgStride + kObjects * kObjStride); // 1248

// COLOR00..15 playfield (todo color 0 = navy); COLOR16..31 sprites.
// Pares: 0/1 objetos (blanco/amarillo), 2/3 rojo/verde, 4/5 azul/cian, 6/7 magenta/naranja.
constexpr eng::Palette32 kPalette {{
	0x013, 0x111, 0x222, 0x333, 0x444, 0x555, 0x666, 0x777,
	0x888, 0x999, 0xaaa, 0xbbb, 0xccc, 0xddd, 0xeee, 0xfff,
	0x000, 0xfff, 0xff0, 0x000, // 16-19: obj0 blanco, obj1 amarillo
	0x000, 0xf00, 0x0f0, 0x000, // 20-23: ch2 rojo, ch3 verde
	0x000, 0x00f, 0x0ff, 0x000, // 24-27: ch4 azul, ch5 cian
	0x000, 0xf0f, 0xf80, 0x000, // 28-31: ch6 magenta, ch7 naranja
}};

struct RiskyWoodsDemo {
	void init(eng::amiga::AmigaBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		if (!backend.configure_memory({ 160u * 1024u, 8u * 1024u, 4u * 1024u })) {
			eng::debug::mark_failed(g_eng_run_status, 0x00020801u);
			return;
		}
		m_bitplane_block = backend.memory_manager().chip().reserve<eng::PlaneTag>(kBitplaneBytes, 16);
		m_copper_block = backend.memory_manager().chip().reserve<eng::CopperTag>(32768u, 16);
		m_sprite_block = backend.memory_manager().chip().reserve<eng::SpriteTag>(
			static_cast<eng::u32>(kSpriteWords) * 2u, 16);
		if (!m_bitplane_block.valid() || !m_copper_block.valid() || !m_sprite_block.valid()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00020802u);
			return;
		}

		eng::Words<eng::SpriteTag> data = m_sprite_block.view.as_words();
		build_background_data(data.data());
		build_object_data(data.data() + kBgChannels * kBgStride);

		// Fondo: reserva canales 2..7 en la banda con el ledger (reparto híbrido).
		eng::graphics::SpriteBand band {};
		band.top = kBandTop;
		band.bottom = kBandBottom;
		band.technique = eng::graphics::SpriteBackdropTechnique::RiskyWoods;
		band.channel_first = kBgFirst;
		band.channel_count = kBgChannels;
		m_ledger.reset();
		if (!eng::graphics::plan_sprite_bands({&band, 1u}, m_ledger).has_value()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00020803u);
			return;
		}

		// Objetos: el asignador les da los canales libres de la banda (0 y 1).
		eng::graphics::SpriteIntent intents[kObjects] {};
		for (eng::u8 i = 0; i < kObjects; ++i) {
			intents[i].top = kObjY;
			intents[i].bottom = kObjVstop;
			intents[i].hpos = m_obj_x[i];
		}
		eng::graphics::SpriteSlot slots[kObjects] {};
		eng::graphics::SpriteAllocator{}.assign({intents, kObjects}, slots, m_ledger);
		for (eng::u8 i = 0; i < kObjects; ++i) {
			if (slots[i].as_bob) {
				eng::debug::mark_failed(g_eng_run_status, 0x00020804u);
				return;
			}
			m_obj_channel[i] = slots[i].channel;
		}

		eng::effects::RiskyWoodsLayer::Config cfg {};
		cfg.first_line = kBandTop;
		cfg.lines = kBandLines;
		cfg.channel_first = kBgFirst;
		cfg.channels = kBgChannels;
		cfg.column_width = kColumnWidth;
		cfg.screen_width = kScreenWidth;
		cfg.bplcon2 = 0x0000u; // sprites delante del playfield (que es todo color 0)
		cfg.arm_hpos = 0x00u;  // WAIT al inicio de línea; 1.ª columna 24 px después (head_start)
		cfg.head_start = 24u;
		cfg.dma_data = data.data();
		cfg.dma_stride = kBgStride;
		if (!m_layer.attach(cfg)) {
			eng::debug::mark_failed(g_eng_run_status, 0x00020805u);
			return;
		}

		if (!build_copper()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00020806u);
			return;
		}
		backend.takeover_display(m_copper_ptr);

		// Telemetría: canales libres en la banda (0,1) y canales de los objetos.
		const eng::u32 free_mask = m_ledger.free_mask(kObjY);
		eng::debug::mark_ready(
			g_eng_run_status,
			(free_mask << 8u) | static_cast<eng::u32>(m_obj_channel[0]));
	}

	void update(eng::amiga::AmigaBackend& backend, eng::GameContext& context) {
		// **Scroll del fondo (3)**: avanza el patrón 1 px cada 2 frames y reconstruye la
		// copperlist (cabecera + reposiciones con las X desplazadas). En una versión de producción
		// se parchearían solo las palabras `SPRxPOS` con el Blitter; aquí se re-emite para
		// claridad, que es suficiente para la demo.
		const eng::u16 frame = context.frame.frame_index;
		// 96 px de periodo: se avanza 1 px cada 2 frames y se envuelve con `%` (no es potencia
		// de 2; el `%` va en el bucle de juego, no en el hot path de dibujo del frame).
		m_scroll = static_cast<eng::u16>((frame / 2u) % kPeriod);
		m_layer.set_scroll(m_scroll);

		// Rebote horizontal de los objetos: se parchea la cabecera POS de su estructura DMA
		// (el DMA la relee al armar el sprite cada frame).
		eng::Words<eng::SpriteTag> data = m_sprite_block.view.as_words();
		eng::u16* obj = data.data() + kBgChannels * kBgStride;
		for (eng::u8 i = 0; i < kObjects; ++i) {
			eng::s16 x = static_cast<eng::s16>(m_obj_x[i] + m_obj_dx[i]);
			if (x < 0) { x = 0; m_obj_dx[i] = static_cast<eng::s16>(-m_obj_dx[i]); }
			if (x > static_cast<eng::s16>(kObjMaxX)) {
				x = static_cast<eng::s16>(kObjMaxX);
				m_obj_dx[i] = static_cast<eng::s16>(-m_obj_dx[i]);
			}
			m_obj_x[i] = static_cast<eng::u16>(x);
			obj[static_cast<eng::u16>(i) * kObjStride] =
				static_cast<eng::u16>((kObjY << 8u) | ((m_obj_x[i] >> 1u) & 0xffu));
		}

		if (build_copper()) {
			backend.install_copper_list(m_copper_ptr);
		}
	}

	void render(eng::amiga::AmigaBackend& backend, eng::GameContext& context) {
		eng::debug::probe_when_ready(g_eng_run_status, context.frame.frame_index);
	}

private:
	/// Rellena las 6 estructuras DMA del fondo con un **motivo de ladrillos**: cada canal
	/// (columna de 16 px) forma una franja de ladrillo con junta horizontal cada 8 líneas y
	/// un escalonado alterno (ladrillo trabado). DAT = plano 0 (color 1), DATB = plano 1
	/// (color 2): las líneas de junta van a color 2, el cuerpo a color 1, y el escalonado
	/// mueve la junta vertical de lado a lado para que el patrón no parezca columnas planas.
	void build_background_data(eng::u16* base) {
		for (eng::u8 c = 0; c < kBgChannels; ++c) {
			eng::u16* s = base + static_cast<eng::u16>(c) * kBgStride;
			const eng::u16 hstart = static_cast<eng::u16>((static_cast<eng::u16>(c) * kColumnWidth) >> 1u);
			s[0] = static_cast<eng::u16>((kBandTop << 8u) | (hstart & 0xffu)); // POS
			s[1] = static_cast<eng::u16>(kBandBottom << 8u);                   // CTL (VSTOP)
			for (eng::u16 l = 0; l < kBandLines; ++l) {
				// Junta horizontal cada 8 líneas.
				const bool mortar_h = (l & 7u) == 0u;
				// Ladrillo trabado: el ladrillo impar (bloques de 8 líneas) va desfasado
				// media columna, con una junta vertical en el borde de 4 px.
				const bool row_odd = (l & 8u) != 0u;
				const eng::u16 shifted = row_odd
					? static_cast<eng::u16>((c + 1u) & 1u)
					: static_cast<eng::u16>(c & 1u);
				// DAT: cuerpo del ladrillo (todo a 1 salvo la junta de 4 px que se deja 0).
				eng::u16 dat = 0xffffu;
				if (shifted != 0u) {
					dat = 0x0fffu; // junta vertical de 4 px por un lado
				} else {
					dat = 0xfff0u; // junta vertical por el otro
				}
				// DATB: marca la junta horizontal (color 2) en las líneas de mortero.
				const eng::u16 datb = mortar_h ? 0xffffu : 0x0000u;
				s[2u + l * 2u + 0u] = dat;
				s[2u + l * 2u + 1u] = datb;
			}
			s[2u + kBandLines * 2u + 0u] = 0u; // terminador
			s[2u + kBandLines * 2u + 1u] = 0u;
		}
	}

	/// Rellena las estructuras DMA de los objetos con una **forma** (rombo/asterisco), no un
	/// bloque sólido: DAT (color 1) y DATB (color 2) dibujan un patrón 16x16 legible.
	void build_object_data(eng::u16* base) {
		// Mascara de un rombo de 16x16 (funcion de distancia de Manhattan).
		for (eng::u8 i = 0; i < kObjects; ++i) {
			eng::u16* s = base + static_cast<eng::u16>(i) * kObjStride;
			s[0] = static_cast<eng::u16>((kObjY << 8u) | ((m_obj_x[i] >> 1u) & 0xffu)); // POS
			s[1] = static_cast<eng::u16>(kObjVstop << 8u);                              // CTL
			for (eng::u16 l = 0; l < kObjH; ++l) {
				// Distancia al centro vertical; anchura del rombo por línea.
				const eng::u16 dy = (l < 8u) ? l : static_cast<eng::u16>(15u - l);
				const eng::u16 half = static_cast<eng::u16>((dy + 1u) * 2u); // 2..16 px por lado
				const eng::u16 center = 8u;
				eng::u16 dat = 0u;
				for (eng::u16 x = 0u; x < 16u; ++x) {
					const eng::u16 dx = (x < center) ? (center - x) : (x - center);
					if (dx < half) {
						dat |= static_cast<eng::u16>(0x8000u >> x);
					}
				}
				// El objeto 0 usa el color 1 del par (DAT -> COLOR17) y el 1 el color 2
				// (DATB -> COLOR18): los dos canales del par comparten COLORxx, asi que la
				// forma debe salir de un plano distinto para verse de color diferente.
				const bool use_b = (i == 1u);
				s[2u + l * 2u + 0u] = use_b ? 0x0000u : dat;
				s[2u + l * 2u + 1u] = use_b ? dat : 0x0000u;
			}
			s[2u + kObjH * 2u + 0u] = 0u; // terminador
			s[2u + kObjH * 2u + 1u] = 0u;
		}
	}

	bool build_copper() {
		eng::copper::SchedulerT<false> sched { m_copper_block };
		sched.emit_planes_display(0x2c81, 0x2cc1, 0x0038, 0x00d0, kBytesPerRow, 0x4200,
					  kPlanes, m_bitplane_block.mem_view_chip(), kPlaneBytes);
		// Reset de los 8 canales (puntero válido + VSTART/VSTOP=0) antes de SPREN.
		const eng::uintptr spr_base = reinterpret_cast<eng::uintptr>(m_sprite_block.view.data());
		for (eng::u8 c = 0; c < 8u; ++c) {
			sched.move(static_cast<eng::u16>(0x120u + c * 4u), static_cast<eng::u16>(spr_base >> 16));
			sched.move(static_cast<eng::u16>(0x122u + c * 4u), static_cast<eng::u16>(spr_base & 0xffffu));
			sched.move(static_cast<eng::u16>(0x142u + c * 8u), 0x0000u);
			sched.move(static_cast<eng::u16>(0x140u + c * 8u), 0x0000u);
		}
		sched.move(eng::copper::Register::DMACON,
			   static_cast<eng::u16>(eng::copper::DmaSetClear | eng::copper::DmaMaster |
						 eng::copper::DmaCopper | eng::copper::DmaBitplane |
						 eng::copper::DmaSprite));
		sched.emit_palette(kPalette.color);
		// **Objetos primero** (canales 0/1): su `SPRxPT`/POS/CTL deben fijarse ANTES de que
		// la lista entre en la banda del fondo (que avanza por líneas con WAITs). Si se
		// emitieran después, no se ejecutarían hasta pasada la banda y los objetos no
		// estarían armados cuando el haz llega a su VSTART (línea 120).
		eng::Words<eng::SpriteTag> data = m_sprite_block.view.as_words();
		const eng::uintptr obj_base =
			reinterpret_cast<eng::uintptr>(data.data() + kBgChannels * kBgStride);
		for (eng::u8 i = 0; i < kObjects; ++i) {
			const eng::u8 ch = m_obj_channel[i];
			const eng::uintptr addr = obj_base + static_cast<eng::uintptr>(i) * kObjStride * 2u;
			sched.move(static_cast<eng::u16>(0x120u + ch * 4u), static_cast<eng::u16>(addr >> 16));
			sched.move(static_cast<eng::u16>(0x122u + ch * 4u), static_cast<eng::u16>(addr & 0xffffu));
			// **Arma** el canal: el registro SPRxPOS/SPRxCTL debe ser válido (el DMA lee la
			// cabecera de la estructura al armar). Ver `winuae/sprite-dma.md`.
			sched.move(static_cast<eng::u16>(0x140u + ch * 8u),
				   static_cast<eng::u16>((kObjY << 8u) | ((m_obj_x[i] >> 1u) & 0xffu)));
			sched.move(static_cast<eng::u16>(0x142u + ch * 8u),
				   static_cast<eng::u16>(kObjVstop << 8u));
		}
		// Fondo Risky Woods: BPLCON2 + SPRxPT de 2..7 + reposiciones de SPRxPOS por línea.
		m_layer.emit_into(sched);
		sched.wait_line(0xf8);
		sched.end();

		m_copper_ok = sched.ok();
		m_copper_ptr = sched.data();
		return m_copper_ok;
	}

	bool m_copper_ok = false;
	const eng::u16* m_copper_ptr = nullptr;
	eng::Block<eng::PlaneTag> m_bitplane_block {};
	eng::Block<eng::CopperTag> m_copper_block {};
	eng::Block<eng::SpriteTag> m_sprite_block {};
	eng::graphics::SpriteChannelLedger m_ledger {};
	eng::effects::RiskyWoodsLayer m_layer {};
	eng::u16 m_obj_x[kObjects] { 40u, 240u };
	eng::s16 m_obj_dx[kObjects] { 2, -2 };
	eng::u8  m_obj_channel[kObjects] {};
	eng::u16 m_scroll = 0u; ///< desplazamiento del fondo (px low-res, 0..kPeriod-1)
};

} // namespace

int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	eng::debug::reset(g_eng_run_status);

	eng::amiga::AmigaBackend backend {};
	RiskyWoodsDemo game {};
	eng::Engine engine { backend, game };
	engine.run_frames_polling(0xffff);

	return 0;
}
