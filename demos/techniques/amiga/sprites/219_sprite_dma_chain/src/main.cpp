// Lanzar:
//   Depurar   : bash ./tools/build/build-demo.sh demos/techniques/amiga/sprites/219_sprite_dma_chain --debug   && bash ./tools/run/run-demo.sh demos/techniques/amiga/sprites/219_sprite_dma_chain --keep-running
//   Optimizada: bash ./tools/build/build-demo.sh demos/techniques/amiga/sprites/219_sprite_dma_chain --release && bash ./tools/run/run-demo.sh demos/techniques/amiga/sprites/219_sprite_dma_chain --keep-running
//   A/B       : EXTRA_DEFINES=-DSPRITE219_STRATEGY=0 (rearme)  |  =1 (cadena por frame)  |  =2 (persistente, defecto)

// ============================================================================
// Demo 219: emisión DMA encadenada por canal — una ristra de 20 balas = 1 canal.
// ============================================================================
//
// F3 de `docs/guides/roadmap/ROADMAP_SPRITES_COMPLETO.md` §3.2. El chipset tiene 8
// canales de sprite; una **ristra vertical** de objetos disjuntos en Y se sirve con UNO
// solo gracias al multiplexado vertical. Esta demo compara las formas de emitirla:
//
//   0) **Rearme por Copper** (camino actual): `emit_placements_into` arma el canal en la
//      línea temprana y **rearma** (WAIT + POS/CTL/PT) en cada franja → 1 armado + 19
//      rearmes por frame.
//   1) **Cadena DMA reconstruida**: `build_sprite_dma_chain` escribe por frame
//      `[POS,CTL,DATA…]×20 + 0,0` en Chip RAM y el canal se arma **una vez** (POS/CTL de
//      la primera franja + PT a su DATA, `arm_dma_chain`); el walk del DMA encadena los
//      bloques siguientes (cabecera + datos).
//   2) **Cadena persistente** (defecto): la cadena se construye UNA vez (imágenes y
//      ranuras estables) y por frame solo se **parchean** las POS/CTL (4 bytes por bala)
//      con `patch_sprite_chain_block`.
//
// Qué se ve: 20 balas en columna (canal 3), x fija y la columna “respira” en Y (pistón
// suave, todas las balas siempre visibles y en el mismo orden: el caso que la cadena
// persistente exige). El **cuadrado superior izquierdo** codifica la estrategia activa
// (COLOR 1 = rearme, 2 = cadena, 3 = persistente) para que las capturas de validación
// sean inequívocas. Fondo de 4 planos (bandas + estrellas) y COLOR20/21/22/23 para el
// par de canales 2/3 (`COLOR16 + 4·par`; AHRM cap. 4).
//
// Mecanismo y citas: `engine/include/eng/graphics/sprite_chain.hpp` (cabecera del
// módulo), `docs/reference/emulators/winuae/sprite-dma.md` (estructura DMA con cabecera
// y columna fantasma) y `../WinUAE-DBG/custom.cpp:4078-4090/12012-12023` (el DMA procesa
// la cabecera y el puntero avanza con el walk). HOST-434 fija el formato de la cadena
// (golden de words) y el armado; esta demo valida la emisión en hardware emulado.

#include <eng/api/api.hpp>
#include <eng/os/os.hpp>
#include <eng/graphics/copper/scheduler.hpp>
#include <eng/graphics/sprite_chain.hpp>
#include <eng/graphics/sprite_manager.hpp>
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
// Telemetría de coste por frame (la lee el host por el canal lateral, patrón 201/202):
// `copper_words` = words de la copperlist publicada (el A/B de emisión se lee aquí).
__attribute__((used)) volatile eng::debug::FrameTelemetry g_eng_frame_telemetry {
	0xffffffffu, 0, 0, 0, 0, { 0, 0, 0 }
};
}

#ifndef SPRITE219_STRATEGY
#define SPRITE219_STRATEGY 2
#endif

namespace {

// Display 320x256, 4 planos. DIW canónico 0x2c81 (como 214).
constexpr eng::u16 kBytesPerRow = 40;
constexpr eng::u8  kPlanes = 4;
constexpr eng::u32 kPlaneBytes = static_cast<eng::u32>(kBytesPerRow) * 256u;
constexpr eng::u32 kBitplaneBytes = kPlaneBytes * kPlanes;

// La ristra: 20 balas por UN solo canal (el chipset tiene 8). Separación vertical > 1
// línea (el gap del reuso) y altura 8: cada bloque = POS+CTL + 8×2 words de DATA.
constexpr eng::u8  kChannel = 3u;              // canal par 2/3 -> COLOR20-23
constexpr eng::u8  kBullets = 20u;
constexpr eng::u16 kBulletH = 8u;
constexpr eng::u16 kSpacing = 10u;             // px entre balas (>= kSpriteVerticalGapLines)
constexpr eng::u16 kBulletWords = static_cast<eng::u16>(kBulletH * 2u); // DAT/DATB por línea
constexpr eng::u16 kBlockWords = static_cast<eng::u16>(2u + kBulletWords);
// Cadena: bloques + terminador + runway de ceros (el walk ocioso sigue leyendo words;
// el buffer se pone a cero al reservar y el builder no toca lo que sigue al terminador).
constexpr eng::u16 kChainRunway = 32u;
constexpr eng::u16 kChainWords = static_cast<eng::u16>(kBullets * kBlockWords + 2u + kChainRunway);

// Estrategia (A/B por EXTRA_DEFINES): 0 = rearme Copper, 1 = cadena por frame, 2 = persistente.
constexpr eng::u8 kStrategy = static_cast<eng::u8>(SPRITE219_STRATEGY);
static_assert(SPRITE219_STRATEGY >= 0 && SPRITE219_STRATEGY <= 2, "estrategia 0..2");
static_assert(kBullets >= 2u, "la cadena necesita >= 2 bloques");

// Pistón: la columna se mueve 40..65 en Y, 1 px/frame, rebote en los extremos. La base
// mínima queda por encima del armado (kArmLine): la primera franja se arma por registro
// con su VSTART ya fijado (una VSTART por debajo del armado no se puede servir).
constexpr eng::u16 kBaseTop = 40u;
constexpr eng::u16 kBaseBottom = static_cast<eng::u16>(kBaseTop + 25u); // 65: cola en 255
constexpr eng::u16 kBulletX = 152u;            // X fija (px pares: HSTART directo)

constexpr eng::u16 kArmLine = 32u;             // armado temprano compartido (patrón 214/428)

// Fondo: bandas 0-7 + estrellas (COLOR08-14) y blanco 15. Par de canales 2/3 en
// COLOR20-23 (20 = transparente del par; 21/22/23 = colores de la bala).
constexpr eng::Palette32 kPalette {{
	0x012, 0x023, 0x034, 0x045, 0x256, 0x467, 0x578, 0x68a,
	0x111, 0x222, 0x333, 0x444, 0x555, 0x666, 0x777, 0xfff,
	0x000, 0x000, 0x000, 0x000,
	0x000, 0x3cf, 0x8ef, 0xfff, // COLOR20-23: par 2/3 (20 transparente)
	0x000, 0x000, 0x000, 0x000,
	0x000, 0x000, 0x000, 0x000,
}};

// --- Arte de la bala (2 planos, 16x8): cápsula con núcleo brillante -----------
struct BulletArt {
	eng::u16 dat[kBulletH] {};
	eng::u16 datb[kBulletH] {};
};

/// Cápsula: `v = 1` contorno, `v = 2` cuerpo, `v = 3` núcleo central (bit 0 -> DAT,
/// bit 1 -> DATB; bit 15 = píxel 0).
constexpr BulletArt make_bullet_art() {
	BulletArt a {};
	for (eng::u16 y = 0u; y < kBulletH; ++y) {
		for (eng::u16 x = 0u; x < 16u; ++x) {
			const eng::u16 adx = static_cast<eng::u16>(x < 8u ? (7u - x) : (x - 8u));
			eng::u16 v = 0u;
			if (adx <= 1u) {
				v = 3u; // núcleo
			} else if (adx <= 3u) {
				v = (y == 0u || y == (kBulletH - 1u)) ? 1u : 2u; // cuerpo con puntas
			} else if (adx == 4u && y != 0u && y != (kBulletH - 1u)) {
				v = 1u; // borde
			}
			if ((v & 1u) != 0u) a.dat[y] = static_cast<eng::u16>(a.dat[y] | (0x8000u >> x));
			if ((v & 2u) != 0u) a.datb[y] = static_cast<eng::u16>(a.datb[y] | (0x8000u >> x));
		}
	}
	return a;
}
constexpr BulletArt kBullet = make_bullet_art();

struct DmaChainDemo {
	using HwSpritePlacement = eng::graphics::HwSpritePlacement;
	using SpriteDmaChain = eng::graphics::SpriteDmaChain;

	void init(eng::amiga::AmigaBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		if (!backend.configure_memory({ 128u * 1024u, 8u * 1024u, 4u * 1024u })) {
			eng::debug::mark_failed(g_eng_run_status, 0x00021901u);
			return;
		}
		m_bitplane_block = backend.memory_manager().chip().reserve<eng::PlaneTag>(kBitplaneBytes, 16);
		m_copper_block = backend.memory_manager().chip().reserve<eng::CopperTag>(2048u, 16);
		// DATA de la bala + DOS storages de cadena (doble buffer del modo 1) + margen.
		const eng::u32 sprite_bytes =
			static_cast<eng::u32>(kBulletWords + 2u * kChainWords + 32u) * 2u;
		m_sprite_block = backend.memory_manager().chip().reserve<eng::SpriteTag>(sprite_bytes, 16);
		if (!m_bitplane_block.valid() || !m_copper_block.valid() || !m_sprite_block.valid()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00021902u);
			return;
		}

		eng::u16* const words = m_sprite_block.view.as_words().data();
		// Imagen de la bala (sin cabecera: la cadena la cabecera y el rearme apunta a la DATA).
		for (eng::u16 y = 0u; y < kBulletH; ++y) {
			words[y * 2u + 0u] = kBullet.dat[y];
			words[y * 2u + 1u] = kBullet.datb[y];
		}
		// Storages de cadena a CERO (runway para el walk tras el terminador).
		for (eng::u32 i = 0u; i < 2u * kChainWords; ++i) {
			words[kBulletWords + i] = 0u;
		}
		m_chain[0].storage = chain_view(0u);
		m_chain[1].storage = chain_view(1u);
		m_img = eng::ChipView<eng::SpriteTag> {
			eng::Address<eng::MemoryKind::Chip>::from_storage(words), kBulletWords * 2u};

		fill_placements(); // posiciones iniciales (base = kBaseTop)
		if (kStrategy == 2u) {
			// Cadena persistente: se construye UNA vez; sin imágenes ni alturas que cambien.
			m_active = 0u;
			if (!eng::graphics::build_sprite_dma_chain(
				    eng::Span<const HwSpritePlacement> {m_ps, kBullets}, m_chain[0])) {
				eng::debug::mark_failed(g_eng_run_status, 0x00021903u);
				return;
			}
		}

		build_background(m_bitplane_block.view.as_words().data());
		if (!build_copper()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00021904u);
			return;
		}
		backend.takeover_display(m_copper_view);
		eng::debug::mark_ready(g_eng_run_status, 0x00021900u);
	}

	void update(eng::amiga::AmigaBackend& backend, eng::GameContext& context) {
		// Pistón de la columna (sin `*`//`%`): 1 px/frame con rebote en los extremos.
		if (m_piston_down) {
			m_base = static_cast<eng::u16>(m_base + 1u);
			if (m_base >= kBaseBottom) m_piston_down = false;
		} else {
			m_base = static_cast<eng::u16>(m_base - 1u);
			if (m_base <= kBaseTop) m_piston_down = true;
		}
		fill_placements();

		eng::copper::SchedulerT<false> sched { m_copper_block };
		build_display(sched, m_bitplane_block);

		if (kStrategy == 0u) {
			// Rearme por Copper: camino actual (armado + rearmes por franja).
			eng::graphics::SpriteManager::emit_placements_into(
				sched, eng::Span<const HwSpritePlacement> {m_ps, kBullets}, kArmLine);
		} else {
			const eng::usize active = (kStrategy == 1u) ? m_active : 0u;
			if (kStrategy == 1u) {
				// Cadena reconstruida: se escribe el buffer INACTIVO y se alterna (doble
				// buffer: la cadena mostrada no se toca mientras el walk la recorre).
				m_active ^= 1u;
				if (!eng::graphics::build_sprite_dma_chain(
					    eng::Span<const HwSpritePlacement> {m_ps, kBullets}, m_chain[m_active])) {
					// No cabe (no debería): degrada a rearme ese frame, sin mezclar.
					eng::graphics::SpriteManager::emit_placements_into(
						sched, eng::Span<const HwSpritePlacement> {m_ps, kBullets},
						kArmLine);
					sched.end();
					publish_telemetry(context, sched);
					install(backend, sched);
					return;
				}
			} else {
				// Persistente: solo POS/CTL de cada bloque (4 bytes/bala; la DATA no se toca).
				for (eng::u8 i = 0u; i < kBullets; ++i) {
					(void)eng::graphics::patch_sprite_chain_block(
						m_chain[0], i, kBulletX, m_ps[i].vstart, kBulletH, false);
				}
			}
			m_chains[kChannel] = m_chain[active];
			eng::graphics::SpriteManager::emit_placements_into(
				sched, eng::Span<const HwSpritePlacement> {m_ps, kBullets}, kArmLine, {},
				eng::Span<const SpriteDmaChain> {m_chains, 8u});
		}
		sched.end();
		publish_telemetry(context, sched);
		install(backend, sched);
	}

	void render(eng::amiga::AmigaBackend& backend, eng::GameContext& context) {
		eng::debug::probe_when_ready(g_eng_run_status, context.frame.frame_index);
	}

private:
	[[nodiscard]] eng::ChipView<eng::SpriteTag> chain_view(eng::u8 buffer) const {
		const eng::usize off = (static_cast<eng::usize>(kBulletWords) +
					static_cast<eng::usize>(buffer) * kChainWords) * 2u;
		return m_sprite_block.mem_view().subview(off, static_cast<eng::usize>(kChainWords) * 2u);
	}

	/// Posiciones del frame: `y_i = base + i·spacing` (orden por Y SIEMPRE ascendente y
	/// estable entre frames: la condición de la cadena persistente).
	void fill_placements() {
		for (eng::u8 i = 0u; i < kBullets; ++i) {
			HwSpritePlacement& p = m_ps[i];
			p.channel = kChannel;
			p.priority = 0u;
			p.hpos = kBulletX;
			p.vstart = static_cast<eng::u16>(m_base + i * kSpacing);
			p.height = kBulletH;
			p.width_words = 1u;
			p.attach = false;
			p.data = m_img;
		}
	}

	void install(eng::amiga::AmigaBackend& backend, const eng::copper::SchedulerT<false>& sched) {
		if (sched.ok()) {
			m_copper_view = sched.chip_view();
			backend.install_copper_list(m_copper_view);
		}
	}

	/// Publica la telemetría de coste del frame (canal lateral): `copper_words` es el
	/// tamaño real de la copperlist emitida; el A/B de estrategias se lee aquí.
	static void publish_telemetry(eng::GameContext& context,
				      const eng::copper::SchedulerT<false>& sched) {
		auto& tel = g_eng_frame_telemetry;
		tel.frame = context.frame.frame_index;
		tel.copper_words = sched.words_used();
	}

	/// Cabecera del display (geom + DMACON + paleta) común a los tres modos. Lo único que
	/// cambia entre modos es la emisión de sprites (abajo).
	static void build_display(eng::copper::SchedulerT<false>& sched, const eng::Block<eng::PlaneTag>& bitplane) {
		sched.emit_planes_display(0x2c81, 0x2cc1, 0x0038, 0x00d0, kBytesPerRow, 0x4200,
					  kPlanes, bitplane.mem_view_chip(), kPlaneBytes);
		sched.move(eng::copper::Register::DMACON,
			   static_cast<eng::u16>(eng::copper::DmaSetClear | eng::copper::DmaMaster |
						 eng::copper::DmaCopper | eng::copper::DmaBitplane |
						 eng::copper::DmaSprite));
		sched.emit_palette(kPalette.color);
	}

	bool build_copper() {
		eng::copper::SchedulerT<false> sched { m_copper_block };
		build_display(sched, m_bitplane_block);
		// Estado inicial del canal: armado por registro de la primera franja (patrón
		// validado `arm_object`) con PT a la DATA del primer bloque de la cadena. En modo
		// persistente la cadena ya está construida; en 0/1 su storage está a cero y el
		// bloque "fantasma" no pinta (DATA transparente).
		eng::graphics::SpriteManager::arm_dma_chain(sched, kChannel, m_chain[0].storage, kBulletX,
							    kBaseTop, kBulletH, false);
		sched.wait_line(0xf8);
		sched.end();
		m_copper_ok = sched.ok();
		m_copper_view = sched.chip_view();
		return m_copper_ok;
	}

	/// Fondo: bandas + estrellas (patrón de la 214) y marcador de estrategia en la esquina
	/// superior izquierda (COLOR = kStrategy + 1) para identificar el modo en las capturas.
	void build_background(eng::u16* words) {
		constexpr eng::u16 kWordsPerRow = static_cast<eng::u16>(kBytesPerRow / 2u); // 20
		constexpr eng::u16 kPlaneWords = static_cast<eng::u16>(kPlaneBytes / 2u);   // 5120
		eng::u32 rng = 0x2545f491u;
		for (eng::u16 y = 0u; y < 256u; ++y) {
			const eng::u16 band = static_cast<eng::u16>((y >> 5u) & 7u);
			for (eng::u16 wx = 0u; wx < kWordsPerRow; ++wx) {
				rng ^= rng << 13u;
				rng ^= rng >> 17u;
				rng ^= rng << 5u;
				const eng::u16 stars =
					((rng & 0x1fu) == 0u) ? static_cast<eng::u16>(1u << ((rng >> 8u) & 15u)) : 0u;
				if (y < 4u && wx < 4u) {
					// Marcador de estrategia: color kStrategy+1 (1..3) a plano completo y
					// sin estrellas, para que las capturas del A/B sean inequívocas.
					const eng::u16 v = static_cast<eng::u16>(kStrategy + 1u);
					for (eng::u8 p = 0u; p < kPlanes; ++p) {
						words[static_cast<eng::u32>(p) * kPlaneWords +
						      static_cast<eng::u32>(y) * kWordsPerRow + wx] =
							((v & (1u << p)) != 0u) ? 0xffffu : 0x0000u;
					}
					continue;
				}
				for (eng::u8 p = 0u; p < kPlanes; ++p) {
					const eng::u16 fill = ((band >> p) & 1u) != 0u ? 0xffffu : 0x0000u;
					words[static_cast<eng::u32>(p) * kPlaneWords +
					      static_cast<eng::u32>(y) * kWordsPerRow + wx] =
						static_cast<eng::u16>(fill | stars);
				}
			}
		}
	}

	bool m_copper_ok = false;
	eng::ChipView<eng::CopperTag> m_copper_view {};
	eng::ChipView<eng::SpriteTag> m_img {};
	eng::Block<eng::PlaneTag> m_bitplane_block {};
	eng::Block<eng::CopperTag> m_copper_block {};
	eng::Block<eng::SpriteTag, eng::MemoryKind::Chip> m_sprite_block {};
	HwSpritePlacement m_ps[kBullets] {};
	SpriteDmaChain m_chain[2] {};
	SpriteDmaChain m_chains[8] {}; // indexado por canal (solo kChannel se usa)
	eng::u8 m_active = 0u;         // buffer de cadena activo (modo 1)
	eng::u16 m_base = kBaseTop;    // Y de la primera bala (pistón)
	bool m_piston_down = true;
};

} // namespace

int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	eng::debug::reset(g_eng_run_status);

	eng::amiga::AmigaBackend backend {};
	DmaChainDemo game {};
	eng::Engine engine { backend, game };
	(void)eng::os::init(engine, 0u);
	engine.run_frames(0xffff);

	return 0;
}
