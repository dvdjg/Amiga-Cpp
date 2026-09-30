// Lanzar:
//   Poll : bash ./tools/build/build-demo.sh demos/techniques/amiga/blitter/212_blitter_feeder --debug
//   IRQ  : EXTRA_DEFINES="-DK_FEEDER_MODE=1" bash ./tools/build/build-demo.sh demos/techniques/amiga/blitter/212_blitter_feeder --debug
//   Run  : bash ./tools/run/run-demo.sh demos/techniques/amiga/blitter/212_blitter_feeder --warp
//
// Variantes (`EXTRA_DEFINES`):
//   K_FEEDER_MODE 0 | 1   0 = poll (CPU espera cada blit); 1 = feeder por IRQ de blit (fire-and-forget).

// ============================================================================
// Demo 212 - blitter_feeder
// ----------------------------------------------------------------------------
// "Fire-and-forget" frente a poll: el mismo trabajo (copiar por Blitter las 20
// columnas de 16 px de un bitmap interleaved) se lanza de dos maneras:
//
//   - POLL (K_FEEDER_MODE=0): la CPU programa cada columna con `blitter_submit(job, wait=true)`
//     y ESPERA a que termine (`BBUSY`). La CPU esta ocupada todo el trabajo.
//   - IRQ  (K_FEEDER_MODE=1): se instala un **feeder** de fin de blit (`set_blit_service`);
//     la CPU solo arranca la PRIMERA columna (`wait=false`) y vuelve. La IRQ de blit
//     (nivel 3, bit BLIT) programa la siguiente y asi hasta agotar la cadena. La CPU
//     queda libre durante el trabajo.
//
// Se mide con el reloj de ciclos del periferico (`prof_clock`) el tramo de CPU que
// consume el trabajo de columnas por frame, y se publica su media en
// `g_eng_run_status.detail`. Empate de trabajo y resultado: en las dos variantes el
// destino queda igual (`dst == src`), solo cambia **quien** programa los blits.
//
// Idea de Blitter-por-intencion: `docs/engine/architecture/BLITTER_INTENT_QUEUE.md` §6.
// La copia de columna (borde de scroll) con `BlitJob` `CopyRect` esta descrita en
// `docs/reference/amiga/techniques/blitter-memcpy.md` ("Concurrencia").
// ============================================================================

#include <eng/api/api.hpp>
#include <eng/debug/prof.hpp>
#include <eng/platform/amiga/backend.hpp>

#include <proto/exec.h>
#include <exec/execbase.h>

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

namespace gfx = eng::graphics::composition;

using eng::s16;
using eng::u16;
using eng::u32;
using eng::u8;

#ifndef K_FEEDER_MODE
#define K_FEEDER_MODE 0
#endif
static_assert(K_FEEDER_MODE == 0 || K_FEEDER_MODE == 1, "K_FEEDER_MODE debe ser 0 o 1");

constexpr u16 kWidth = 320u;
constexpr u16 kHeight = 256u;
constexpr u16 kRowBytes = kWidth / 8u; // 40
constexpr u8 kPlanes = 3u;
constexpr u16 kColPx = 16u;          // ancho de columna
constexpr u16 kWords = kColPx / 16u; // 1 word
constexpr u16 kColumns = kWidth / kColPx; // 20
// Copia interleaved de UNA pasada: `height` = filas de pixel * planos = 768 (<= 1023).
constexpr u16 kBlockHeight = kHeight * kPlanes;
constexpr u32 kBitmapBytes = static_cast<u32>(kRowBytes) * kPlanes * kHeight; // 30720

constexpr eng::Palette32 kPalette {{
	0x000, 0xf00, 0x0f0, 0xff0, 0x00f, 0xf0f, 0x0ff, 0xfff,
}};

/// Un trabajo `CopyRect` que copia UNA columna de 16 px a lo alto de un bitmap
/// **interleaved** (una sola BLTSIZE cubre los 3 planos). `src_dir`/`dst` son las bases
/// interleaved; `col` es el índice de columna. Modulo = bytes de plane-line menos la
/// palabra copiada (`BLTCMOD`/`BLTDMOD`), de modo que el Blitter camina 40 B por planeline.
eng::graphics::BlitJob make_column_job(const u8* src, u8* dst, u16 col) {
	eng::graphics::BlitJob j {};
	j.kind = eng::graphics::BlitJobKind::CopyRect;
	j.source = eng::graphics::BlitPtr::from_storage(
		reinterpret_cast<const u16*>(src + static_cast<u32>(col) * (kColPx / 8u)));
	j.destination = eng::graphics::BlitPtr::from_storage(
		reinterpret_cast<const u16*>(dst + static_cast<u32>(col) * (kColPx / 8u)));
	j.words_per_row = kWords;
	j.height = kBlockHeight;
	j.source_modulo_bytes = static_cast<s16>(kRowBytes - kWords * 2u);      // 38
	j.destination_modulo_bytes = static_cast<s16>(kRowBytes - kWords * 2u); // 38
	j.bitplane_count = 1u; // interleaved: una pasada cubre los planos
	j.source_shift = 0u;
	j.interleaved = true;
	return j;
}

/// Rellena el origen interleaved con una imagen estatica: una banda vertical por columna,
/// con un color (indice) que cicla. Es estatica a proposito: la copia de columnas es
/// **idempotente**, de modo que el resultado es el mismo con poll y con IRQ (sin *tearing*
/// visible) y las capturas deben salir identicas.
void fill_source(u8* bmp) {
	for (u16 col = 0u; col < kColumns; ++col) {
		const u8 color = static_cast<u8>(1u + (col & 7u));
		const u32 off = static_cast<u32>(col) * (kColPx / 8u);
		for (u8 p = 0u; p < kPlanes; ++p) {
			const u16 value = (color & (1u << p)) != 0u ? 0xffffu : 0x0000u;
			for (u16 y = 0u; y < kHeight; ++y) {
				auto* w = reinterpret_cast<u16*>(bmp +
								 static_cast<u32>(y) * (kRowBytes * kPlanes) +
								 static_cast<u32>(p) * kRowBytes + off);
				*w = value;
			}
		}
	}
}

struct FeederDemo {
	void init(eng::amiga::AmigaBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		m_backend = &backend;
		if (!backend.configure_memory({80u * 1024u, 8u * 1024u, 8u * 1024u})) {
			eng::debug::mark_failed(g_eng_run_status, 0x00021201u);
			return;
		}

		gfx::SceneResources res = gfx::planar(kWidth, kHeight, kPlanes);
		res.layout = gfx::SceneLayout::Interleaved; // la copia de columna en UNA pasada
		if (!gfx::compose(m_scene, backend.memory_manager(), res, gfx::ocs_a500,
				  gfx::display(res),
				  gfx::palette(eng::PaletteWords {kPalette.color, 8u}, 0u, 8u))) {
			eng::debug::mark_failed(g_eng_run_status, 0x00021202u);
			return;
		}
		m_src = backend.memory_manager().chip().reserve<eng::PlaneTag>(kBitmapBytes, 16u);
		if (!m_src.valid()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00021203u);
			return;
		}
		u8* dst = m_scene.bitplanes().data();
		for (u32 i = 0u; i < kBitmapBytes; ++i) {
			m_src.view.data()[i] = 0u;
		}
		fill_source(m_src.view.data());

		for (u16 c = 0u; c < kColumns; ++c) {
			m_jobs[c] = make_column_job(m_src.view.data(), dst, c);
		}

		m_scene.takeover(backend);
#if K_FEEDER_MODE == 1
		// Feeder: la IRQ de fin de blit programa la columna siguiente. Se instala UNA vez y
		// DESPUES del takeover: `takeover_display` apaga todos los IRQ (`INTENA`), asi que
		// armarlos antes los perderia.
		if (!backend.set_blit_service(&FeederDemo::on_blit, *this)) {
			eng::debug::mark_failed(g_eng_run_status, 0x00021204u);
			return;
		}
#else
		// Poll: el mismo trabajo, sin IRQ.
		for (u16 c = 0u; c < kColumns; ++c) {
			if (!backend.blitter_submit(m_jobs[c], true)) {
				eng::debug::mark_failed(g_eng_run_status, 0x00021205u);
				return;
			}
		}
#endif
		m_ready = true;
	}

	void update(eng::amiga::AmigaBackend& backend, eng::GameContext& context) {
		eng::debug::mark_frame(g_eng_run_status, context.frame.frame_index);
		if (!m_ready) {
			return;
		}
		// Mide el tramo de CPU que cuesta el trabajo de columnas (con su sincronizacion).
		const u32 c0 = eng::debug::prof_clock();

#if K_FEEDER_MODE == 1
		// Fire-and-forget: si la cadena del frame anterior sigue viva, se espera a que TERMINE
		// (la avanza la IRQ). `wait_blitter()` solo cubre el blit en curso; hay que repetir hasta
		// que la ISR marque `m_busy == false` (si no, una cadena nueva pisaria a la anterior).
		while (m_busy) {
			(void)backend.wait_blitter();
		}
#endif
#if K_FEEDER_MODE == 1
		m_next = 1u;
		m_busy = true;
		if (!backend.blitter_submit(m_jobs[0], false)) {
			m_busy = false;
			eng::debug::mark_failed(g_eng_run_status, 0x00021206u);
			return;
		}
		accumulate(backend);
#else
		for (u16 c = 0u; c < kColumns; ++c) {
			(void)backend.blitter_submit(m_jobs[c], true);
			accumulate(backend);
		}
#endif
		const u32 c1 = eng::debug::prof_clock();
		m_cycles += c1 - c0;
		++m_frames;

		if (context.frame.frame_index >= 5u) {
			// `detail` empaquetado (ver README):
			//   bit 31      : `verified` (la cadena dejo `dst == src`)
			//   bits 24..30 : `blitter_common_hits` / frame (control del estado comun)
			//   bits 18..23 : `blitter_starts` / frame
			//   bits 0..17  : ciclos de CPU / frame del tramo de columnas (tope 262143)
			const u32 cyc = m_cycles / m_frames;
			const u32 st = m_starts / m_frames;
			const u32 hi = m_hits / m_frames;
			eng::debug::mark_ready(g_eng_run_status,
					       (m_verified ? 0x80000000u : 0u) | ((hi & 0x7fu) << 24u) |
						       ((st & 0x3fu) << 18u) | (cyc & 0x3ffffu));
		}
	}

	void render(eng::amiga::AmigaBackend& backend, eng::GameContext& context) {
		// Verifica UNA vez (fuera del tramo medido) que la copia de columnas dejo el destino
		// igual al origen; en modo IRQ espera a que la cadena termine (Blitter libre).
		if (m_ready && !m_checked && !backend.blitter_busy()) {
			const u8* s = m_src.view.data();
			const u8* d = m_scene.bitplanes().data();
			m_verified = true;
			for (u32 i = 0u; i < kBitmapBytes; ++i) {
				if (s[i] != d[i]) {
					m_verified = false;
					break;
				}
			}
			m_checked = true;
		}
		eng::debug::probe_when_ready(g_eng_run_status, context.frame.frame_index);
	}

	/// Feeder: se llama en cada fin de blit. Programa la columna siguiente hasta agotar
	/// la cadena. No espera (el Blitter esta libre: la IRQ se genera al terminar).
	static void on_blit(FeederDemo& self, u16) {
		if (self.m_next >= kColumns) {
			self.m_busy = false;
			return;
		}
		if (!self.m_backend->blitter_submit(self.m_jobs[self.m_next], false)) {
			self.m_busy = false;
			return;
		}
		self.accumulate(*self.m_backend);
		++self.m_next;
	}

private:
	/// Acumula los contadores del diagnostico del backend tras programar UN blit (cada
	/// `blitter_submit` los reinicia y solo cuenta su propio job). `blitter_starts` debe
	/// dar 1 por columna; `blitter_common_hits` debe acercarse a kColumns (todas las
	/// columnas comparten estado comun: mismo `CopyRect` interleaved, mismas modulos).
	void accumulate(eng::amiga::AmigaBackend& backend) {
		m_starts += backend.blitter_starts();
		m_hits += backend.blitter_common_hits();
	}

	eng::amiga::AmigaBackend* m_backend = nullptr;
	bool m_ready = false;
	/// `m_busy`/`m_starts`/`m_hits` los comparte la **ISR**: `volatile` para que el compilador no
	/// los cachee en registro entre iteraciones del bucle de sincronizacion.
	volatile bool m_busy = false;
	bool m_checked = false;
	bool m_verified = false;
	u16 m_next = 0u;
	u32 m_cycles = 0u;
	volatile u32 m_starts = 0u;
	volatile u32 m_hits = 0u;
	u32 m_frames = 0u;
	gfx::Scene m_scene {};
	eng::Block<eng::PlaneTag> m_src {};
	eng::graphics::BlitJob m_jobs[kColumns] {};
};

} // namespace

int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	eng::debug::reset(g_eng_run_status);

	eng::amiga::AmigaBackend backend {};
	FeederDemo game {};
	eng::Engine engine {backend, game};
	engine.run_frames_polling(0xffffu);

	return 0;
}
