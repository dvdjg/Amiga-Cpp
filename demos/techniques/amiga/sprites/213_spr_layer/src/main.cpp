// Lanzar:
//   bash ./tools/build/build-demo.sh demos/techniques/amiga/sprites/213_spr_layer --debug
//   bash ./tools/run/run-demo.sh demos/techniques/amiga/sprites/213_spr_layer --keep-running

// ============================================================================
// Demo 213 — Free Form Sprite Layer (Jeroen Knoester, "SPR Layer", 2018)
// ============================================================================
//
// Recrea en el engine la capa de fondo de sprites *free form* de la referencia
// `spr_layer/Sprite_Layer`: los 8 canales de sprite se **rearman horizontalmente** dentro de
// cada linea (el Copper reescribe `SPRxPOS`+`SPRxDATB`+`SPRxDATA`, **sin** `SPRxCTL`) para
// cubrir toda la pantalla con una imagen **no repetitiva**, y al final de cada linea se
// reposicionan los canales a la izquierda (orden inverso) para el renglon siguiente. Se usa el
// tileset de fondo (Turrican II) de la propia referencia.
//
// Claves verificadas (docs/reference/amiga/techniques/sprite-horizontal-multiplex.md §46-70):
//   1) por posicion SOLO `SPRxPOS`+`SPRxDATB`+`SPRxDATA` (escribir `SPRxCTL` desactiva el
//      comparador; `SPRxDATA` arma el sprite en la nueva X);
//   2) reposicion de fin de linea en orden inverso (7..0);
//   3) cada canal (DMA y Copper) necesita una **estructura DMA valida** (cabecera POS+CTL +
//      terminador) o el DMA avanza por memoria y deja una columna fantasma.
//
// Scroll **fino de 1 px** como la referencia: el sprite solo se posiciona en pasos de 2 px
// (HSTART = X/2), asi que el pixel impar se logra con el **bit de paridad de `SPRxCTL`** (la
// referencia alterna `$0c02`/`$0c03`). Doble buffer de copperlist (`install_copper_list`) para
// parchear la lista inactiva y evitar el tearing (la referencia usa 4 listas).
//
// El playfield de 4 planos de la referencia se omite: la capa de sprites es el contenido.
// No se usa `effects::FreeFormSpriteLayer` (roto; ver demo 212).
// ============================================================================

#include <eng/api/api.hpp>
#include <eng/graphics/copper/scheduler.hpp>
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

// Tileset de fondo de la referencia (13 tiles de 16x16x2). Asset fuente en
// `assets/amiga/sprites/spr_layer/bg_tiles_spr.raw` (Turrican II; ver README de assets).
// `INCBIN` a scope global (define los simbolos `incbin_*_start/_end` sin mangle).
INCBIN(spr_bgtiles, "assets/amiga/sprites/spr_layer/bg_tiles_spr.raw");

namespace {

using eng::u16;
using eng::u32;
using eng::u8;
using eng::usize;

// --- Geometria ---------------------------------------------------------------------------------
constexpr u16 kBytesPerRow = 40;                 // 320 px / 8
constexpr u8  kPlanes = 1;                        // playfield transparente (fondo) de 1 plano
constexpr u32 kPlaneBytes = static_cast<u32>(kBytesPerRow) * 256u;

constexpr u16 kScreenW = 320;
constexpr u16 kLines = 224;                       // alto de la capa de sprites
constexpr u16 kTop = 16;                          // primera linea de la capa
// HALLAZGO (emulador, sin resolver): con `kTop=16` (< VSTART del display, $2c=44) los canales
// DMA NO se muestran (todo lo pinta el Copper); con `kTop=44` el DMA si dibuja pero la capa
// (224 lineas) no cubre el display (256) y queda un resto abajo. Falta cuadrar altura/linea.
constexpr u16 kColW = 16;                         // ancho de columna (1 sprite)
constexpr u16 kHpos0 = 112;                       // X de la 1.ª columna (px lo-res)
constexpr u16 kCols = 22;                         // columnas visibles (22*16 = 352 > 320 + margen)
constexpr u8  kChannels = 8;                      // canales DMA = 8 primeras columnas
constexpr u16 kCopperCols = kCols - kChannels;    // 14 columnas por Copper
constexpr u16 kArmHpos = 0x30;                    // WAIT del rearmado (hpos = px/2; tras el fetch DMA)

constexpr u16 kStride = static_cast<u16>(2u + kLines * 2u + 2u); // words por estructura DMA
// Palabras de Copper por linea: WAIT(2) + columnas Copper (POS+DATB+DATA=6) + reposicion (8*2).
constexpr u16 kLineWords = static_cast<u16>(2u + kCopperCols * 6u + kChannels * 2u);
constexpr u32 kSpriteWords = static_cast<u32>(kChannels) * kStride;
constexpr u32 kCuBytes = 48u * 1024u; // una copperlist ~45 KB (224 lineas x 102 words)

// Mundo: tilemap 32x14 de tiles 16x16 (de la referencia, Turrican II).
constexpr u16 kWorldCols = 32;
constexpr u16 kTileBytes = 64;                    // 16 lineas x 2 planos x 1 word

// Paleta de sprites (COLOR16-31): ($000,$22b,$444,$ddd) repetido en los 4 pares (COLOR16-19 por
// par de canales 0/1, etc.) — misma convencion que la referencia.
static constexpr eng::Palette32 kPalette {{
	0x000, 0x111, 0x222, 0x333, 0x444, 0x555, 0x666, 0x777,
	0x888, 0x999, 0xaaa, 0xbbb, 0xccc, 0xddd, 0xeee, 0xfff,
	0x000, 0x22b, 0x444, 0xddd, 0x000, 0x22b, 0x444, 0xddd,
	0x000, 0x22b, 0x444, 0xddd, 0x000, 0x22b, 0x444, 0xddd,
}};

// Tilemap de fondo (extraido de GFX/tilemap.asm, `bg_tile_map`).
static constexpr u16 kTileMap[32u * 14u] = {
	0x07u,0x00u,0x01u,0x09u,0x0au,0x02u,0x00u,0x00u,0x01u,0x02u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x03u,0x05u,0x04u,0x00u,0x06u,
	0x00u,0x03u,0x05u,0x05u,0x05u,0x05u,0x04u,0x03u,0x05u,0x05u,0x04u,0x01u,0x02u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x03u,0x05u,0x05u,0x05u,0x04u,0x00u,
	0x03u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x04u,0x01u,0x02u,0x00u,0x00u,0x00u,0x06u,0x07u,0x00u,0x00u,0x00u,0x00u,0x03u,0x05u,0x05u,0x05u,0x05u,0x05u,0x04u,
	0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x04u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x03u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,
	0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x04u,0x00u,0x00u,0x00u,0x00u,0x00u,0x03u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,
	0x08u,0x08u,0x08u,0x08u,0x08u,0x08u,0x08u,0x08u,0x08u,0x08u,0x08u,0x08u,0x08u,0x08u,0x08u,0x08u,0x08u,0x08u,0x0cu,0x00u,0x01u,0x02u,0x0bu,0x08u,0x08u,0x08u,0x08u,0x08u,0x08u,0x08u,0x08u,0x08u,
	0x00u,0x00u,0x01u,0x02u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x01u,0x02u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x03u,0x05u,0x05u,0x04u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,
	0x00u,0x0bu,0x08u,0x08u,0x0cu,0x00u,0x00u,0x00u,0x00u,0x0bu,0x08u,0x08u,0x0cu,0x01u,0x02u,0x01u,0x09u,0x0au,0x09u,0x05u,0x05u,0x05u,0x05u,0x04u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,
	0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,0x01u,0x02u,0x03u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x04u,0x00u,0x00u,0x06u,0x07u,0x00u,0x00u,0x00u,
	0x00u,0x06u,0x07u,0x00u,0x00u,0x00u,0x00u,0x03u,0x0au,0x09u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x04u,0x00u,0x00u,0x00u,0x00u,0x00u,0x00u,
	0x00u,0x00u,0x00u,0x00u,0x03u,0x0au,0x09u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x04u,0x00u,0x00u,0x00u,0x00u,0x00u,
	0x00u,0x00u,0x01u,0x09u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x04u,0x00u,0x00u,0x00u,0x00u,
	0x00u,0x03u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x05u,0x04u,0x00u,0x00u,0x00u,
	0x0bu,0x08u,0x08u,0x08u,0x08u,0x08u,0x08u,0x08u,0x08u,0x08u,0x08u,0x08u,0x08u,0x08u,0x08u,0x08u,0x08u,0x08u,0x08u,0x08u,0x08u,0x08u,0x08u,0x08u,0x08u,0x08u,0x08u,0x08u,0x08u,0x0cu,0x00u,0x00u,
};

/// Palabra big-endian del tileset.
[[nodiscard]] inline u16 be16(const u8* p) noexcept {
	return static_cast<u16>((static_cast<u16>(p[0]) << 8u) | p[1]);
}

/// POS del sprite: `(VSTART<<8)|(HSTART)` con `HSTART = X/2` (X en px lo-res; el bit impar va en
/// `SPRxCTL` bit 0).
[[nodiscard]] inline u16 spr_pos(u16 x) noexcept {
	return static_cast<u16>((static_cast<u16>(kTop) << 8u) | ((x >> 1u) & 0xffu));
}

struct SprLayerDemo {
	void init(eng::amiga::AmigaBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		// 384K chip (playfield 10K + mundo 28K + estructuras 7K + 2 copperlists 96K + margen).
		if (!backend.configure_memory({ 384u * 1024u, 8u * 1024u, 8u * 1024u })) {
			eng::debug::mark_failed(g_eng_run_status, 0x00021301u); return;
		}
		m_bitplane = backend.memory_manager().chip().reserve<eng::PlaneTag>(kPlaneBytes, 16);
		m_world_block = backend.memory_manager().chip().reserve<eng::PlaneTag>(
			static_cast<u32>(kWorldCols) * kLines * 2u * 2u, 16);
		// Doble buffer TAMBIEN de las estructuras DMA (la referencia usa `spr0`/`spr0b`): si no,
		// el DMA (parcheado cada frame) y el Copper (lista con 1 frame de retraso) quedan a 1 px
		// de desfase y aparece una discontinuidad en la frontera DMA/Copper.
		m_sprite[0] = backend.memory_manager().chip().reserve<eng::SpriteTag>(kSpriteWords * 2u, 16);
		m_sprite[1] = backend.memory_manager().chip().reserve<eng::SpriteTag>(kSpriteWords * 2u, 16);
		m_cu[0] = backend.memory_manager().chip().reserve<eng::CopperTag>(kCuBytes, 16);
		m_cu[1] = backend.memory_manager().chip().reserve<eng::CopperTag>(kCuBytes, 16);
		if (!m_bitplane.valid() || !m_world_block.valid() || !m_sprite[0].valid() ||
		    !m_sprite[1].valid() || !m_cu[0].valid() || !m_cu[1].valid()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00021302u); return;
		}
		for (u32 i = 0u; i < kPlaneBytes / 2u; ++i) m_bitplane.view.as_words().data()[i] = 0u; // playfield transparente
		m_world = m_world_block.view.as_words().data();
		build_world();
		build_sprites(m_sprite[0], 0u);
		build_sprites(m_sprite[1], 0u);
		if (!build_copper(m_cu[0], m_sprite[0], 0u, 0u) ||
		    !build_copper(m_cu[1], m_sprite[1], 0u, 0u)) {
			eng::debug::mark_failed(g_eng_run_status, 0x00021303u); return;
		}
		m_cu_window[0] = 0u; m_cu_window[1] = 0u; m_active = 0u;
		backend.takeover_display(m_cu[0].view.as_words().data());
		eng::debug::mark_ready(g_eng_run_status, 0x00021300u);
	}
	/// Scroll suave a pantalla completa (doble buffer): cada frame se parchea la copperlist
	/// **inactiva** (POS fino por Blitter + DATA si cambia la ventana) y se **publica** con
	/// `install_copper_list` (el Copper recarga en VBlank -> sin tearing). Ping-pong sobre el
	/// mundo (512 px).
	void update(eng::amiga::AmigaBackend& backend, eng::GameContext& c) {
		constexpr u16 kRange = static_cast<u16>(kWorldCols * kColW); // 512
		const u32 period = static_cast<u32>(2u * kRange);
		const u32 t = c.frame.frame_index % period;
		const u16 pos = static_cast<u16>(t < kRange ? t : period - t);
		const u16 s = static_cast<u16>(pos & (kColW - 1u));
		const u16 window = static_cast<u16>((pos >> 4u) % kWorldCols);
		const u8 inact = static_cast<u8>(1u - m_active);
		if (m_cu_window[inact] != window) {
			patch_data(m_cu[inact], m_sprite[inact], window);
			m_cu_window[inact] = window;
		}
		patch_pos(backend, m_cu[inact], m_sprite[inact], s);
		backend.install_copper_list(m_cu[inact].view.as_words().data());
		m_active = inact;
		eng::debug::mark_frame(g_eng_run_status, c.frame.frame_index);
	}
	void render(eng::amiga::AmigaBackend&, eng::GameContext& c) {
		eng::debug::probe_when_ready(g_eng_run_status, c.frame.frame_index);
	}

private:
	using CopperBlock = eng::Block<eng::CopperTag>;

	/// Genera el MUNDO (32 columnas x 224 lineas) como palabras (DATB, DATA) por linea.
	void build_world() {
		for (u16 wc = 0u; wc < kWorldCols; ++wc) {
			for (u16 l = 0u; l < kLines; ++l) {
				const u16 row = static_cast<u16>(l / 16u);
				const u16 r = static_cast<u16>(l % 16u);
				const u16 tile = kTileMap[row * kWorldCols + wc];
				const u8* t = reinterpret_cast<const u8*>(spr_bgtiles) +
					      static_cast<u32>(tile) * kTileBytes + static_cast<u32>(r) * 4u;
				m_world[(static_cast<usize>(wc) * kLines + l) * 2u + 0u] = be16(t);     // DATB
				m_world[(static_cast<usize>(wc) * kLines + l) * 2u + 1u] = be16(t + 2u); // DATA
			}
		}
	}

	/// Estructuras DMA de los 8 canales (cabecera POS+CTL + DATA/DATB por linea + terminador).
	/// Sin multiplicaciones en el bucle (punteros incrementales; AGENTS §1.15).
	void build_sprites(eng::Block<eng::SpriteTag>& sprite, u16 window) {
		u16* base = sprite.view.as_words().data();
		for (u8 c = 0u; c < kChannels; ++c) {
			const u16 wc = static_cast<u16>((window + c) % kWorldCols);
			const u16* q = m_world + static_cast<usize>(wc) * kLines * 2u;
			u16* s = base + static_cast<u32>(c) * kStride;
			s[0] = spr_pos(static_cast<u16>(kHpos0 + c * kColW));   // POS cabecera (parcheable)
			s[1] = static_cast<u16>(static_cast<u16>(kTop + kLines) << 8u); // CTL: VSTOP
			u16* d = s + 2u;
			for (u16 l = 0u; l < kLines; ++l) {
				d[0] = q[1]; // DATA
				d[1] = q[0]; // DATB
				d += 2u; q += 2u;
			}
			d[0] = 0u; // terminador
			d[1] = 0u;
		}
	}

	/// Copperlist: display + punteros de sprite + capa de sprites por linea (WAIT + 14 columnas
	/// Copper + reposicion de fin de linea) + cierre. Registra `m_pos_base`.
	bool build_copper(CopperBlock& block, eng::Block<eng::SpriteTag>& sprite, u16 window, u16 s) {
		eng::copper::SchedulerT<false> sched { block };
		sched.emit_planes_display(0x2c81, 0x2cc1, 0x0038, 0x00d0, kBytesPerRow, 0x1200, kPlanes,
					  m_bitplane.mem_view_chip(), kPlaneBytes);
		const eng::uintptr sbase = reinterpret_cast<eng::uintptr>(sprite.view.data());
		for (u8 c = 0u; c < kChannels; ++c) {
			const eng::uintptr addr = sbase + static_cast<eng::uintptr>(c) * kStride * 2u;
			sched.move(static_cast<u16>(0x120u + c * 4u), static_cast<u16>(addr >> 16u));
			sched.move(static_cast<u16>(0x122u + c * 4u), static_cast<u16>(addr & 0xffffu));
		}
		sched.move(eng::copper::Register::BPLCON2, 0x0000u); // prioridad: sprites detras del playfield
		sched.move(eng::copper::Register::DMACON,
			   static_cast<u16>(eng::copper::DmaSetClear | eng::copper::DmaMaster |
					     eng::copper::DmaCopper | eng::copper::DmaBitplane |
					     eng::copper::DmaSprite));
		sched.emit_palette(kPalette.color, 16u, 16u);
		// Indice (words) de la **palabra de valor** del primer `SPRxPOS` de la 1.ª linea Copper:
		// cabecera + WAIT (2 words) + palabra de registro del POS (1 word).
		m_pos_base = static_cast<u16>(sched.words_used() + 3u);
		for (u16 l = 0u; l < kLines; ++l) {
			sched.wait_position_safe(static_cast<u16>(kTop + l), kArmHpos);
			for (u16 j = kChannels; j < kCols; ++j) {
				const u8 ch = static_cast<u8>(j % kChannels);
				const u16 wc = static_cast<u16>((window + j) % kWorldCols);
				const usize t = (static_cast<usize>(wc) * kLines + l) * 2u;
				sched.move(static_cast<u16>(0x140u + ch * 8u),
					   spr_pos(static_cast<u16>(kHpos0 + j * kColW - s)));
				sched.move(static_cast<u16>(0x146u + ch * 8u), m_world[t + 0u]); // DATB
				sched.move(static_cast<u16>(0x144u + ch * 8u), m_world[t + 1u]); // DATA (arma)
			}
			for (u8 c = kChannels; c-- > 0u; ) { // reposicion a la izquierda, orden inverso
				sched.move(static_cast<u16>(0x140u + c * 8u),
					   spr_pos(static_cast<u16>(kHpos0 + c * kColW - s)));
			}
		}
		sched.wait_line(0xf8u);
		sched.end();
		return sched.ok();
	}

	/// Parchea la X de las columnas Copper (14 fills estridados por Blitter) y los 8 POS de
	/// cabecera DMA (CPU) con el scroll fino `s`. El bit 0 de `SPRxCTL` (paridad) da el 1 px que
	/// el POS (pasos de 2 px) no puede; afecta a todas las columnas del canal.
	void patch_pos(eng::amiga::AmigaBackend& backend, CopperBlock& block,
		       eng::Block<eng::SpriteTag>& sprite, u16 s) {
		eng::Words<eng::CopperTag> w = block.view.as_words();
		for (u16 j = kChannels; j < kCols; ++j) {
			const u16 x = static_cast<u16>(kHpos0 + j * kColW - s);
			backend.blitter_fill_words_strided(
				eng::graphics::blit_ptr(w.subspan(m_pos_base + (j - kChannels) * 6u)), spr_pos(x),
				kLines, kLineWords, j == (kCols - 1u));
		}
		// Pixel impar de X: `SPRxCTL` bit0 (`../WinUAE-DBG/drawing.cpp:2706`:
		// `sprxp = (pos&0xff)*2 + (ctl&1)`). Va en el CTL de las ESTRUCTURAS DMA, que es el CTL
		// del canal: asi lo reciben igual las columnas DMA (POS de estructura) y las Copper
		// (POS de copperlist), y `HSTART = (X)>>1` (el POS solo tiene pasos de 2 px).
		const u16 ctl = static_cast<u16>((static_cast<u16>(kTop + kLines) << 8u) | (s & 1u));
		eng::Words<eng::SpriteTag> sp = sprite.view.as_words();
		for (u8 c = 0u; c < kChannels; ++c) {
			sp[static_cast<u32>(c) * kStride + 0u] =
				spr_pos(static_cast<u16>(kHpos0 + c * kColW - s));
			sp[static_cast<u32>(c) * kStride + 1u] = ctl; // VSTOP + paridad (fine 1 px)
		}
	}

	/// Escribe la DATA (`DATB`/`DATA`) de las 14 columnas Copper para la ventana `window` y
	/// regenera las estructuras DMA. Solo al cruzar columna (cada 16 px). Sin multiplicaciones
	/// en el bucle (punteros incrementales; AGENTS §1.15).
	void patch_data(CopperBlock& block, eng::Block<eng::SpriteTag>& sprite, u16 window) {
		u16* w = block.view.as_words().data();
		const u16* src[kCopperCols];
		for (u16 jj = 0u; jj < kCopperCols; ++jj) {
			const u16 wc = static_cast<u16>((window + kChannels + jj) % kWorldCols);
			src[jj] = m_world + static_cast<usize>(wc) * kLines * 2u;
		}
		u16* dst = w + m_pos_base; // POS de la 1.ª columna; DATB en +2, DATA en +4
		for (u16 l = 0u; l < kLines; ++l) {
			for (u16 jj = 0u; jj < kCopperCols; ++jj) {
				const u16 off = static_cast<u16>(jj * 6u);
				dst[off + 2u] = src[jj][l * 2u + 0u]; // DATB
				dst[off + 4u] = src[jj][l * 2u + 1u]; // DATA
			}
			dst += kLineWords;
		}
		build_sprites(sprite, window);
	}

	u16 m_pos_base = 0u;                 // indice de la palabra de valor del 1.er SPRxPOS Copper
	u8  m_active = 0u;                   // copperlist mostrada
	u16 m_cu_window[2] { 0xffffu, 0xffffu }; // ventana que tiene cada copperlist
	u16* m_world = nullptr;
	eng::Block<eng::PlaneTag> m_bitplane {};
	eng::Block<eng::PlaneTag> m_world_block {};
	eng::Block<eng::SpriteTag> m_sprite[2] {};
	CopperBlock m_cu[2] {};
};

} // namespace

int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	eng::debug::reset(g_eng_run_status);

	eng::amiga::AmigaBackend backend {};
	SprLayerDemo game {};
	eng::Engine engine { backend, game };
	engine.run_frames_polling(0xffff);

	return 0;
}
