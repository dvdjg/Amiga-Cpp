// Lanzar:
//   Depurar   : bash ./tools/build/build-demo.sh demos/techniques/amiga/os/213_bartman_abyss --debug   && bash ./tools/run/run-demo.sh demos/techniques/amiga/os/213_bartman_abyss --keep-running
//   Optimizada: bash ./tools/build/build-demo.sh demos/techniques/amiga/os/213_bartman_abyss --release && bash ./tools/run/run-demo.sh demos/techniques/amiga/os/213_bartman_abyss --keep-running

// Demo 213 - "Bartman Abyss": port de la demo clásica de Bartman/vscode-amiga-debug al engine,
// escrita con la **fachada de juego** (`eng::App`/`Screen`, `Scene` + `app.audio()`). No hay
// `main`, `SysBase`, punteros crudos, registros ni `BlitJob`: el juego describe QUÉ quiere y el
// engine decide CÓMO. Ver `docs/engine/architecture/GAME_API_TWO_LEVELS.md`.
//
// ---------------------------------------------------------------------------------------------
// QUÉ HACE LA ORIGINAL (`BartmanBasic/main.c`), PASO A PASO
// ---------------------------------------------------------------------------------------------
// La original es un programa **sin SO** (toma el sistema con `TakeSystem`) que programa el chipset
// a mano. Su flujo, con el registro de hardware entre paréntesis:
//
//  1) Arranque y toma del sistema. `main()` abre `graphics.library`/`dos.library` solo para poder
//     **restaurar** el display del SO al salir. `TakeSystem()` hace `Forbid`/`Disable`, guarda
//     `INTENA`/`DMACON`/`ADKCON` y el `View` activo, `LoadView(0)`, `OwnBlitter()`, apaga TODAS las
//     IRQ (`INTENA=$7fff`) y el DMA (`DMACON=$7fff`), pone los 32 colores a negro y localiza el
//     **VBR** (vector base, 68010+) para poder instalar su propia IRQ de nivel 3/4.
//  2) Precálculo en `warpmode(1)`: `p61Init(module)` inicializa el replayer P61 (ThePlayer 6.1a).
//  3) **Copperlist** en Chip RAM (`copper1`, `AllocMem(MEMF_CHIP)`): se compone a mano palabra a
//     palabra. `screenScanDefault` escribe `DDFSTRT/DDFSTOP` (ventana de fetch, $38..) y
//     `DIWSTRT/DIWSTOP` (ventana visible). Luego: `BPLCON0` (5 planos + COLOR), un MOVE parcheable
//     de `BPLCON1` (fine-scroll), `BPLCON2` con `1<<6` (sprites detrás del playfield), `BPL1MOD`/
//     `BPL2MOD` = 4*40 (salto entre filas de un plano en layout **interleaved**: 5 planos × 40 B =
//     200 B/fila; el módulo que ve Agnus es `fila_física − fila_fetch = 200 − 40 = 160`), los 5
//     `BPLxPT` (`copSetPlanes`: `image + p*40`), la paleta completa (`copSetColor` ×32) y un
//     `COPJMP2` a `copper2`. La segunda copperlist `copper2` es un pequeño gradiente de COLOR00 en
//     las líneas $41..$4F.
//  4) **DMA del display**: `COP1LC=copper1`, `COP2LC=copper2`, arranque con `COPJMP1=$7fff` y
//     `DMACON = SETCLR|MASTER|RASTER|COPPER|BLITTER` (activa Copper + bitplanes + Blitter). El
//     truco de apagar el Blitter antes de `COPJMP1` evita un bug de `COPJMP`.
//  5) **IRQ de VBlank** (`interruptHandler`, nivel 3): rearma `INTREQ` (dos veces, bug A4000),
//     **reescribe el MOVE de `BPLCON1`** con `sinus15[frame]` en AMBOS nibbles (`sin|sin<<4`) →
//     "menea" todo el playfield (±15 px), y llama `p61Music()` (avanza la música una vez por
//     frame); `frameCounter++`.
//  6) **Bucle principal** (`while(!MouseLeft())`), sincronizado a la línea raster 0x10:
//      - **Clear**: blit `D-only` (`BLTCON0 = A_TO_D|DEST`) que borra la banda de juego: `BLTDPT =
//        image + 40·200·5` (fila 200 del plano 0 en interleaved), `BLTDMOD=0` y `BLTSIZE = (56·5)<<6
//        | 20` (320 px × 280 filas físicas = 56 filas × 5 planos).
//      - **Blit de BOB**: por cada BOB (el `main.c` literal usa `for(i=0;i<1;i++)`: **un** BOB; la
//        demo del repo original dibuja un enjambre de 16), blit cookie-cut `$CA`
//        (`BLTCON0 = $CA|SRCA|SRCB|SRCC|DEST|(x&15)<<12`, `BLTCON1 = (x&15)<<12`) con
//        `BLTAPT=src` (imagen), `BLTBPT=src+40*1`...) — nótese que el layout real del par es
//        `[imagen][máscara]` y la corrección de x se aplica a AMBOS canales —, `BLTxMOD = 4`,
//        `BLTDMOD = (320−32)/8`, `BLTSIZE=(16·5)<<6 | 2` (16 filas × 5 planos, 1 blit por BOB en
//        interleaved). El frame del BOB cicla `i%6`.
//      - `debug_clear()` (vacía el overlay del depurador de WinUAE).
//  7) Salida: `p61End()`, `FreeSystem()` (restaura copperlist del SO, IRQ y DMA) y cierra
//     librerías.
//
// ---------------------------------------------------------------------------------------------
// CÓMO LO EXPRESA ESTA DEMO (misma máquina, API de intenciones del engine)
// ---------------------------------------------------------------------------------------------
// Internamente hace **casi lo mismo** que la original, pero el juego no nombra registros ni
// punteros: describe *qué* quiere y el engine lo traduce al chipset. Correspondencia:
//
//   original                                    | 213 (fachada del engine)
//   --------------------------------------------|-------------------------------------------------
//   TakeSystem/FreeSystem (apagar DMA/IRQ…)     | `App::run` + `takeover()` (SYSTEM_TAKEOVER del engine)
//   copperlist a mano (BPLCON0/MOD/DIW/DDF/PT)  | `composition::compose(scene, …, planar(320,256,5))`
//   `copSetPlanes` (BPLxPT = image + p*40)      | `Screen::bitmap(bg, Box{0,0,320,256})`
//   paleta por `copSetColor` ×32                | argumento `PaletteWords` de `compose` (etapa paleta)
//   `copper2` (gradiente COLOR00, $41..$4F)     | etapa `intents` con `CopperIntent::PaletteLine`
//   MOVE parcheable de BPLCON1 + IRQ que lo      | `App::set_fine_scroll(px)` (patch del slot de
//     reescribe con `sinus15`                      BPLCON1, aplicado en el VBlank del engine)
//   IRQ VBlank que llama `p61Music()`            | `app.audio().play_music()` + `App::on_vblank`
//   clear D-only (`BLTDPT/BLTDMOD/BLTSIZE`)     | `Screen::clear_box(Box)`
//   blit cookie-cut `$CA` por BOB               | `Screen::sprite(Sprite, x, y, frame)`
//   `debug_register_bitmap/palette/copperlist`  | `app.debug().register_bitmap/palette/…`
//   `debug_clear`                               | `app.debug().clear()` (mismo overlay)
//   `while(!MouseLeft())` (sondeo de hardware)  | bucle `App` + `app.port()` (mini-SO, mensajes)
//
// La única diferencia de fondo: en la 213 la **colocación** de registros (`BPLxPT`, módulos,
// minterm, `ASH`/`BSH`, tamaño del blit) la calcula el motor, no el juego. Por eso aquí no
// aparecen `custom->…`, `AllocMem`, `WaitBlit` ni `BlitJob`.
//
// Nota sobre el nº de BOBs: el `main.c` literal (`for(i=0;i<1;i++)`) dibuja **un** BOB; la demo
// de referencia del repo dibuja un **enjambre de 16** movidos por senos. Esta port reproduce el
// enjambre (el efecto visual de la demo), que es lo que se observa al ejecutarla.
//
// Reproduce la demo original:
//   - Escena 320x256 de 5 planos con la imagen "abyss" sobre fondo claro.
//   - 16 BOB enmascarados (cookie-cut) movidos por senos; el juego solo pinta sprites.
//   - **Fine-scroll** del playfield (`BPLCON1`, tabla `sinus15`) que "menea" el logo.
//   - Música P61 por `app.audio()`.
//   - Overlay/registro de recursos del depurador de WinUAE (`app.debug()`).

#include <eng/api/api.hpp>
#include <eng/api/assets.hpp>
#include <eng/debug/mem_probe.hpp>
#include <eng/debug/prof.hpp>
#include <eng/debug/run_status.hpp>
#include <eng/platform/amiga/entry.hpp>
#include <eng/platform/amiga/memory_profile.hpp>

#include "support/gcc8_c_support.h"

// --- Assets incrustados: manifiesto (fuente única de blobs/rutas; el juego no usa `INCBIN`) ---
// El manifiesto declara los blobs y expone accesores (`abyss::img_data()`/`img_size()`…); el
// código de juego no contiene rutas de assets ni el helper `INCBIN`.
#include "assets.manifest.hpp"

namespace {

/// Códigos que el runner interpreta para localizar fallos de init y el estado Ready.
constexpr eng::u32 kRunDetailBase = 0x00021300u;
constexpr eng::u32 kRunDetailReady = kRunDetailBase;
constexpr eng::u32 kRunDetailMemorySetupFailed = kRunDetailBase + 1u;
constexpr eng::u32 kRunDetailSceneSetupFailed = kRunDetailBase + 2u;
constexpr eng::u32 kRunDetailBitmapAssetFailed = kRunDetailBase + 3u;
constexpr eng::u32 kRunDetailBitmapCopyFailed = kRunDetailBase + 4u;
constexpr eng::u32 kRunDetailSpriteOrMusicAssetFailed = kRunDetailBase + 5u;

/// Secciones medidas con `eng::debug::g_eng_prof` (ver `tools/debug/profile.mjs` y §1.5 de
/// `AGENTS.md`: medir, no inferir). El `present` distingue sync/async: con `K_ASYNC_PRESENT=1`
/// solo debe contener el lanzado del primer job (~decenas de ciclos), no las esperas.
constexpr eng::u8 kProfLoop = 0u;
constexpr eng::u8 kProfRender = 1u;
constexpr eng::u8 kProfPresent = 2u;
constexpr eng::u8 kProfOverlay = 3u;
constexpr eng::u8 kProfCount = 4u;

/// Id del aviso de fin de ristra (modo async): `Screen::notify(kChainTicket)` tras los 16 BOBs
/// y el clear. La IRQ de fin de blit lo publica como `MsgType::IntentDone`; el `update` lo
/// cuenta. Diagnóstico del encadenado transparente (`BLITTER_INTENT_QUEUE.md` §6).
constexpr eng::u16 kChainTicket = 0x00b1u;
volatile eng::u32 g_chain_done_count = 0u;

constexpr eng::u16 kWidth = 320u;
constexpr eng::u16 kHeight = 256u;
constexpr eng::u8 kPlanes = 5u;
constexpr eng::u32 kBytesPerWord = sizeof(eng::u16);
constexpr eng::u16 kPaletteColorCount = eng::kPaletteEntries;
constexpr eng::u8 kSpriteFrameCount = 6u;
constexpr eng::u16 kBobCount = 16u;
/// A/B de diagnóstico del camino por BOB (`-DK_BOB_COUNT=n`): por defecto, todos los BOBs.
#ifndef K_BOB_COUNT
#define K_BOB_COUNT kBobCount
#endif
constexpr eng::u16 kBobSpacing = 16u;
constexpr eng::u16 kGameBandTop = 200u;
constexpr eng::u16 kGameBandHeight = kHeight - kGameBandTop;
constexpr eng::u16 kHorizontalWaveModulo = 51u;constexpr eng::u8 kVerticalWaveMask = 63u;
constexpr eng::u8 kWaveFrequency = 2u;
constexpr eng::u8 kWaveAmplitudeScale = 2u;
constexpr eng::u8 kWaveVerticalScale = 2u;

constexpr eng::u16 kBobW = 32u;
constexpr eng::u16 kBobH = 16u;
constexpr eng::u32 kBobFrameStride = kBobH * kPlanes * kBytesPerWord *
					    (kBobW / (kBytesPerWord * 8u)) * kBytesPerWord; // 640 B

// Ondas generadas en compilación (`eng::ct_array`), aproximación entera de seno (Bhaskara I),
// como la referencia de `Amiga-Cpp-Optimizations`: reparto horizontal y seno vertical del
// enjambre de 16 BOBs. (El `main.c` original usa `sinus40`/`sinus32`; aquí se generan.)
constexpr eng::u8 kWaveY[kVerticalWaveMask + 1u] {
	20,22,24,26,28,30,31,33,34,36,37,38,39,39,40,40,
	40,40,39,39,38,37,36,35,34,32,30,29,27,25,23,21,
	19,17,15,13,11,10,8,6,5,4,3,2,1,1,0,0,
	0,0,1,1,2,3,4,6,7,9,10,12,14,16,18,20};
constexpr eng::u8 kWaveX[kHorizontalWaveModulo] {
	16,18,20,22,24,25,27,28,30,30,31,32,32,32,32,31,
	30,30,28,27,25,24,22,20,18,16,14,12,10,8,7,5,
	4,2,2,1,0,0,0,0,1,2,2,4,5,7,8,10,12,14,16};

// Tabla `sinus15` de `BartmanBasic/main.c` (amplitud 0..15): alimenta el **fine-scroll** del
// playfield por `BPLCON1`, que "menea" toda la imagen del fondo (logo abyss incluido, sobre el
// que van los BOBs).
constexpr eng::u8 kFineScroll[64] {
	8,8,9,10,10,11,12,12,
	13,13,14,14,14,15,15,15,
	15,15,15,15,14,14,14,13,
	13,12,12,11,10,10,9,8,
	8,7,6,5,5,4,3,3,
	2,2,1,1,1,0,0,0,
	0,0,0,0,1,1,1,2,
	2,3,3,4,5,5,6,7};

// Gradiente de la **copper2** del original: COLOR00 = `0x0NNN` en las líneas $41..$4F (N =
// línea − $40). La original lo emitía con una segunda copperlist (`copper2`) + `COPJMP2`; aquí
// el plan del `compose` lo expresa con una intencion `PaletteLine` por línea (WAIT + MOVE
// COLOR00), el mismo vocabulario que usan las demos 085/086; la etapa `intents` las aporta.
constexpr eng::u8 kGradientLine0 = 0x41u; ///< primera línea (`copper2` del original)
constexpr eng::u8 kGradientLineCount = 15u; ///< 15 líneas: $41..$4F
constexpr eng::u16 kGradientWords[16] {
	0x0000, 0x0111, 0x0222, 0x0333, 0x0444, 0x0555, 0x0666, 0x0777,
	0x0888, 0x0999, 0x0aaa, 0x0bbb, 0x0ccc, 0x0ddd, 0x0eee, 0x0fff};

/// Tabla de intenciones `PaletteLine` de la copper2 (patrón `SkyIntents` de la 086):
/// una intención por línea, COLOR00 con el color de la tabla. Como la original acaba en
/// `0x0fff` (= el COLOR00 de la paleta base) y **no restaura** COLOR00, tras la línea $4F
/// el fondo queda blanco: el gradiente solo es visible en las líneas $41..$4F.
struct GradientIntents {
	eng::graphics::CopperIntent v[kGradientLineCount] {};
	constexpr GradientIntents() {
		for (eng::u8 i = 0u; i < kGradientLineCount; ++i) {
			eng::graphics::CopperIntent it {};
			it.kind = eng::graphics::CopperIntentKind::PaletteLine;
			it.top = static_cast<eng::u16>(kGradientLine0 + i);
			it.bottom = it.top;
			it.first = 0u; // COLOR00
			it.count = 1u;
			it.colors = eng::PaletteWords {&kGradientWords[i + 1u], 1u};
			v[i] = it;
		}
	}
};
constexpr GradientIntents kColorGradientIntents {};

struct AbyssDemo {
	void init(auto& app) {
		eng::debug::mark_init_started(g_eng_run_status);
		ENG_PROF_INIT(kProfCount);

		// La **escena y el display** los compone `App::start()` (lo llama `main`) desde el
		// `GameDisplay` declarativo: el juego no nombra `SceneResources`, `ocs_a500`, `compose`
		// ni `BPLCON0`. Aquí solo se comprueba que el contexto de dibujo quedó listo.
		if (!app.screen().valid()) {
			eng::debug::mark_failed(g_eng_run_status, kRunDetailSceneSetupFailed);
			return;
		}

		// --- Assets de juego por nombre (el engine copia a Chip y resuelve el dominio) ------
		// ORIGINAL: `image`/`bob`/`module` viven en `.MEMF_CHIP` (`#embed`), porque el Blitter y
		// Paula solo ven Chip RAM. AQUÍ: el motor copia los blobs de `.rodata` a bloques Chip.
		m_assets.bind(app.device().memory_manager());
		const bool bitmap_added = m_assets.add_bitmap("abyss", abyss::img_data(),
					       abyss::img_size(), kWidth, kHeight, kPlanes,
					       eng::graphics::PlaneLayout::Interleaved);
		if (!bitmap_added) {
			eng::debug::mark_failed(g_eng_run_status, kRunDetailBitmapAssetFailed);
			return;
		}
		const auto background = m_assets.bitmap("abyss");
		// ORIGINAL: `image` es ya el bitmap de la escena (no hay copia). AQUÍ: se copia el asset
		// al framebuffer una vez, con un blit interleaved de los 5 planos.
		const bool background_queued = app.screen().bitmap(background, eng::Box {0, 0, kWidth, kHeight});
		if (!background_queued) {
			eng::debug::mark_failed(g_eng_run_status, kRunDetailBitmapCopyFailed);
			return;
		}
		app.present();
		// Refresca la **sonda de memoria** (`g_mem_probe`) con el estado de los bancos antes de
		// cargar sprite/música: si un `add` falla, el host lee causa, tamaño pedido y huecos sin
		// recompilar (`tools/debug/mem-probe.mjs`). Es diagnóstico permanente, no un parche.
		eng::debug::probe_memory_banks(app.device().memory_manager());
		// El `bob.bpl` original ya trae el layout que consume el motor: por cada fila de cada
		// plano, `[imagen `w/16` palabras][máscara `w/16` palabras]` (ver
		// `docs/reference/amiga/techniques/interleaved-bob-single-blit.md`). No se reempaqueta:
		// el engine lo dibuja con **un** blit cookie-cut `$CA` por BOB. Es el mismo `bob.bpl` de
		// la original: `BLTAPT`/`BLTBPT` y `AMOD`/`BMOD`/`DMOD` los deriva el encoder del motor.
		const bool sprite_added = m_assets.add<eng::SpriteTag>("bob", abyss::bob_data(),
						  abyss::bob_size());
		const bool music_added = m_assets.add<eng::MusicTag>("mod", abyss::mod_data(),
						 abyss::mod_size());
		if (!sprite_added || !music_added) {
			// El fallo de reserva queda en `g_mem_probe` (lo registra `res::load`); el `detail`
			// resume qué banco y por qué, para el `runstatus` del canal lateral.
			const auto& bank = app.device().memory_manager().chip();
			const auto snap = bank.snapshot();
			eng::debug::mark_mem_failed(g_eng_run_status, kRunDetailBase + 5u,
						    static_cast<eng::u32>(bank.status()), snap.remaining,
						    snap.used);
			return;
		}

		// --- El objeto como asset de juego (geometría declarada; el sheet lo pone `Assets`) --
		// ORIGINAL: la geometría del blit estaba implícita en los registros (`BLTAMOD=4`,
		// `BLTxMOD`, `BLTSIZE=(16*5)<<6|2`). AQUÍ: se declara una vez (32×16, 5 planos,
		// interleaved, cookie-cut `$CA` con la máscara intercalada por pares). El motor traduce.
		eng::graphics::Bob desc {};
		desc.width = kBobW;
		desc.height = kBobH;
		desc.planes = kPlanes;
		desc.frame_count = kSpriteFrameCount;
		desc.frame_stride = kBobFrameStride;
		desc.layout = eng::graphics::BobLayout::Interleaved;
		desc.draw = eng::graphics::BobDraw::CookieCut;
		desc.mask_pack = eng::graphics::BobMaskPack::InterleavedPair;
		m_sprite = m_assets.sprite("bob", desc);

		// --- Música por la fachada de audio ------------------------------------------------
		// ORIGINAL: el replayer P61 se inicializa (`p61Init`) y avanza en la IRQ de VBlank
		// (`p61Music`), ambas por llamadas `jsr` a `player`. AQUÍ: el engine resuelve formato y
		// buffer y **conduce** la música en su VBlank; el juego solo la arranca por nombre.
		(void)app.audio().play_music(m_assets.music("mod"));

		// ORIGINAL: `TakeSystem()` apaga DMA/IRQ y toma el display; `FreeSystem()` lo restaura.
		// AQUÍ: `App::start()` (en `main`) ya instaló la copperlist del camino planar y tomó el
		// display (SYSTEM_TAKEOVER); el engine gestiona el ciclo de vida y el runner cierra.
		// --- Recursos del depurador gráfico de WinUAE --------------------------------------
		// Registra el bitmap abyss, la hoja del BOB, la paleta y la copperlist como recursos
		// nombrados (la original lo hace con `debug_register_*`): así el gfx debugger los
		// muestra. Accedemos a los datos por la escena/asset de dominio, sin punteros crudos.
		if constexpr (requires { app.debug(); }) {
			auto& d = app.debug();
			const auto bg = m_assets.bitmap("abyss");
			if (bg.valid()) {
				d.register_bitmap(bg.planes.data(), "abyss.bpl", kWidth, kHeight, kPlanes,
						  /*interleaved=*/true, /*masked=*/false);
			}
			const auto sheet = m_assets.bytes("bob");
			if (!sheet.empty()) {
				d.register_bitmap(sheet.data(), "bob.bpl", kBobW, kBobH * kSpriteFrameCount,
						  kPlanes, /*interleaved=*/true, /*masked=*/true);
			}
			d.register_palette(abyss::pal_words(), "abyss.pal",
					   kPaletteColorCount);
		}
		// --- Vía async del `present()` (IRQ de blit, `BLITTER_INTENT_QUEUE.md` §6.1) ---------
		// A/B de rendimiento: con `K_ASYNC_PRESENT=1` el `present()` lanza la cadena de blits
		// (16 BOBs + clear + copia de fondo) y la IRQ de fin de blit encadena el resto, con la
		// CPU libre; con 0, el `present()` síncrono clásico (esperas activas). Misma imagen.
#ifndef K_ASYNC_PRESENT
#define K_ASYNC_PRESENT 0
#endif
		if constexpr (K_ASYNC_PRESENT != 0) {
			if constexpr (requires { app.set_async_present(true); }) {
				app.set_async_present(true);
			}
		}
		m_ready = true;
	}

	void update(auto& app) {

		// **Cola de mensajes del mini-SO** (`ENG_APP_MAIN` la deja lista): el latido de VBlank
		// del `App` publica `MsgType::VBlank` en `app.port()` y el juego lo drena aquí. Sin
		// drenar, el puerto se llena y descarta; un juego real consume además la entrada.
		eng::os::Msg m;
		while (app.port().pop(m)) {
			if (m.type == eng::os::MsgType::VBlank) {
				++m_vblank_msgs;
			} else if (m.type == eng::os::MsgType::IntentDone) {
				// Aviso de la cadena async (`Screen::notify`): el `ticket` identifica qué punto
				// de la ristra ha terminado. Aquí solo se cuenta para verificar el mecanismo.
				if (m.payload.user.a == kChainTicket) {
					++m_chain_done;
					g_chain_done_count = g_chain_done_count + 1u;
				}
			}
		}
		eng::debug::mark_frame(g_eng_run_status, app.frame());

	}

	void render(auto& app) {
		auto s = app.screen();
		const eng::u32 frame = app.frame();
		ENG_PROF_BEGIN(kProfRender);
		// **Fine-scroll** del playfield (tabla `sinus15`): desplaza TODO el fondo —imagen
		// "abyss" incluida— con `BPLCON1`, sin tocar los BOBs del plan del frame. Se aplica en
		// el VBlank (lo hace el engine) para no partir scanlines.
		// ORIGINAL: el handler de la IRQ de VBlank escribía el MOVE parcheable de `BPLCON1`
		// (`*scroll = sin | (sin<<4)`). AQUÍ: `set_fine_scroll` pide el valor y el engine lo
		// aplica en su propio latido de VBlank.
		app.set_fine_scroll(kFineScroll[frame & 63u]);
		// Limpia la banda de juego. ORIGINAL: blit D-only (`BLTCON0=A_TO_D|DEST`, `BLTDPT=image+
		// 40*200*5`, `BLTDMOD=0`, `BLTSIZE=(56*5)<<6|20`). AQUÍ: `clear_box` encola el mismo
		// blit D-only interleaved (filas 200..255 de los 5 planos) en el plan del frame.
#ifndef K_STREAM_BOBS
#define K_STREAM_BOBS 1
#endif
#if K_STREAM_BOBS
		// **Streaming (coste cero)**: se emiten los blits en el momento, sin `FramePlan`. El juego
		// describe objetos y el motor escribe el Blitter registro a registro (el bucle del original).
		(void)s.clear_now(eng::Box {0, kGameBandTop, kWidth, kGameBandHeight});
		auto run = s.stamp(m_sprite);
#else
		s.clear_box(eng::Box {0, kGameBandTop, kWidth, kGameBandHeight});
#endif
		// --- 16 BOBs, EXACTAMENTE como el original -----------------------------------------
		// Original: `for (i = 0; i < 16; i++) { x = i*16 + sinus32[(frameCounter + i) % 51]*2;
		//   y = sinus40[((frameCounter + i)*2) & 63] / 2; src = bob + stride*(i % 6); ... }`
		// Reparto horizontal `i*16`, seno horizontal `sinus32` y vertical `sinus40`, y frame de
		// la hoja `i % 6`. Fase horizontal en módulo 51 que empieza en `frame % 51` y avanza por
		// BOB con resta condicional (sin `%` en el bucle, regla de coste ~cero).
		eng::u32 hphase = frame % kHorizontalWaveModulo;
		// Frame de la hoja con **contador que envuelve**, no `i % 6`: el `%` sobre `u16` compila
		// a `__modsi3` (división por software) y se ejecutaba una vez por BOB en el bucle
		// caliente (16 divisiones/frame). El original avanza el frame de la hoja con un índice
		// propio; aquí un `if (++fi == 6) fi = 0` es `addq`/`beq`, sin libcall.
		eng::u8 fi = 0u;
		for (eng::u16 i = 0u; i < K_BOB_COUNT; ++i) {
			const eng::s16 x = static_cast<eng::s16>(
				static_cast<eng::u32>(i) * kBobSpacing +
				static_cast<eng::u32>(kWaveX[hphase]) * kWaveAmplitudeScale);
			const eng::s16 y = static_cast<eng::s16>(
				static_cast<eng::u32>(
					kWaveY[((frame + i) * kWaveFrequency) & kVerticalWaveMask]) /
					kWaveVerticalScale);
			// El original coloca el BOB en la fila `200 + y` (`image + 40*5*(200+y)`).
			const eng::s16 by = static_cast<eng::s16>(y + static_cast<eng::s16>(kGameBandTop));
#if K_STREAM_BOBS
			(void)run.at(x, by, fi);
#else
			s.sprite(m_sprite, x, by, fi);
#endif
			if (++hphase >= kHorizontalWaveModulo) {
				hphase = 0u;
			}
			if (++fi >= kSpriteFrameCount) {
				fi = 0u;
			}
		}
#if K_STREAM_BOBS
		(void)run.done();
#endif
		ENG_PROF_END(kProfRender);
		ENG_PROF_BEGIN(kProfPresent);
		// **Aviso de fin de ristra** (solo async): pide que la IRQ publique `IntentDone` con
		// `kChainTicket` cuando terminen TODOS los trabajos encolados (clear + 16 BOBs). Es la
		// petición de aviso intermedia que describe `BLITTER_INTENT_QUEUE.md` §6: un Id por
		// barrera, sin sondeo. En modo síncrono `notify` no tiene efecto (nadie lo lee).
		if constexpr (K_ASYNC_PRESENT != 0) {
			(void)app.screen().notify(kChainTicket);
		}
		app.present();
		ENG_PROF_END(kProfPresent);
		// --- Overlay de depuración de WinUAE -----------------------------------------------
		// Igual que el original: un rectángulo relleno, un rectángulo de borde y un texto, todos
		// desplazándose con `f = frameCounter & 255` (coordenadas PAL ×2). El motor lo expone con
		// `app.debug()` (mismo overlay que `debug_rect`/`debug_filled_rect`/`debug_text`).

		if constexpr (requires { app.debug(); }) {
			ENG_PROF_BEGIN(kProfOverlay);
			auto& d = app.debug();
			d.clear();
			const eng::s16 f = static_cast<eng::s16>(frame & 255u);
			d.filled_rect(static_cast<eng::s16>(f + 100), 400,
				      static_cast<eng::s16>(f + 400), 440, 0x0000ff00u);
			d.rect(static_cast<eng::s16>(f + 90), 380,
			       static_cast<eng::s16>(f + 400), 440, 0x000000ffu);
			d.text(static_cast<eng::s16>(f + 130), 418, "This is a WinUAE debug overlay",
			       0x00ff00ffu);
			ENG_PROF_END(kProfOverlay);
		}

		if (m_ready) {
			eng::debug::mark_ready(g_eng_run_status, kRunDetailReady);
		}
		eng::debug::probe_when_ready(g_eng_run_status, app.frame());
		ENG_PROF_FRAME();
	}

	eng::graphics::Sprite m_sprite {};
	eng::Assets m_assets {};
	eng::u32 m_vblank_msgs = 0u; ///< mensajes `VBlank` drenados del puerto del mini-SO
	eng::u32 m_chain_done = 0u;  ///< avisos `IntentDone` de la cadena async (kChainTicket)
	bool m_ready = false;
};

} // namespace

// **Composition root**: la app no usa `ENG_APP_MAIN` porque elige el perfil de memoria y declara
// el display; el juego (`AbyssDemo`) solo ve la fachada.
struct ExecBase* SysBase = nullptr;

extern "C" {
__attribute__((used)) volatile eng::debug::RunStatus g_eng_run_status {
	eng::debug::run_status_magic, eng::debug::run_status_version,
	static_cast<eng::u16>(eng::debug::RunState::Cold), 0, 0,
};
}

int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	eng::debug::reset(g_eng_run_status);

	eng::amiga::AmigaBackend backend {};
	// **Memoria automática**: el engine elige el perfil (A500/A1200) desde el inventario de
	// hardware; el juego no llama `configure_memory` ni elige tamaños.
	if (!backend.configure_game_memory()) {
		eng::debug::mark_failed(g_eng_run_status, kRunDetailMemorySetupFailed);
		return 0;
	}

	// **Display declarativo**: geometría (320×256×5, interleaved), paleta y efectos de copper por
	// línea (el gradiente de COLOR00). `App::start()` compone escena + copperlist sin que el juego
	// nombre `SceneResources`/`ocs_a500`/`BPLCON0`.
	eng::GameDisplay display {};
	display.width = kWidth;
	display.height = kHeight;
	display.color_depth = kPlanes;
	display.layout = eng::graphics::PlaneLayout::Interleaved;
	{
		const eng::u16* w = abyss::pal_words();
		for (eng::u8 i = 0u; i < eng::kPaletteEntries; ++i) {
			display.palette.color[i] = w[i];
		}
	}
	display.intents = eng::Span<const eng::graphics::CopperIntent> {kColorGradientIntents.v,
									kGradientLineCount};

	AbyssDemo game {};
	eng::App app {backend, game, backend.memory_manager()};
	if (!app.set_display(display) || !app.start()) {
		eng::debug::mark_failed(g_eng_run_status, kRunDetailSceneSetupFailed);
		return 0;
	}
	app.run(0xffffu);
	return 0;
}
