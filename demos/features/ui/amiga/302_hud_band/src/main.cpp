// Demo 302 - hud_band
// ----------------------------------------------------------------------------
// Tutorial (H1 de ROADMAP_HUD.md): **HUD de juego en franja propia**. El modelo
// `eng::hud` (contador, medidor, vidas) declara una region `Band` con su presupuesto;
// el presentador `HudStrip` materializa las operaciones sobre un `CanvasPlayfield`
// (Chip) con `Surface`, y el `App` compone el display con `RasterLayout`: campo de
// juego a 4 planos + franja de 2 planos con paleta propia en la linea 208
// (`ModeSwitchZone`). El juego no escribe registros ni conoce el chipset.
//
//   bash ./tools/build/build-demo.sh demos/features/ui/amiga/302_hud_band --debug
//   bash ./tools/run/run-demo.sh demos/features/ui/amiga/302_hud_band --keep-running

#include <eng/api/api.hpp> // incluye eng::hud (fachada estable desde H1)
#include <eng/platform/amiga/entry.hpp>
#include <eng/scene/display.hpp>

namespace {

using eng::s16;
using eng::s32;
using eng::u16;
using eng::u32;
using eng::u8;

constexpr u16 kWidth = 320u;
constexpr u16 kHeight = 256u;

// --- Franja del HUD: lineas 208..255 (48 lineas), 2 planos, paleta propia ---------
// `Band::top` es linea de RASTER (el `WAIT` del Copper compara VPOS): la linea de display
// 208 empieza en la raster `DIWSTRT_y + 208` (0x2c = 44; ver demo 115 y AHRM cap. 3).
constexpr u16 kDiwstrtY = 0x2cu;
constexpr u16 kBandTop = 208u;
constexpr u16 kBandTopRaster = static_cast<u16>(kDiwstrtY + kBandTop);
constexpr u16 kBandHeight = 48u;
constexpr u8 kBandPlanes = 2u;
constexpr u16 kBandBytesPerRow = kWidth / 8u; // 40 B por fila y plano (fetch estandar)
constexpr u32 kBandBytes = static_cast<u32>(kBandBytesPerRow) * kBandPlanes * kBandHeight;

// Indices logicos del tema (paleta de la franja).
constexpr u8 kHudBg = 0u;
constexpr u8 kHudFg = 1u;
constexpr u8 kHudAccent = 2u;
constexpr u8 kHudWarn = 3u;

// Paleta del campo de juego (4 planos, 16 colores) y de la franja (2 planos, 4 colores).
// Los colores de la franja (blanco/amarillo/rojo) NO se repiten en la de juego: el gate
// visual los usa para localizar la banda.
constexpr eng::Palette32 kGamePalette {{
	0x000, 0x112, 0x224, 0x336, 0x448, 0x55a, 0x66c, 0x77e,
	0x135, 0x246, 0x357, 0x468, 0x579, 0x68a, 0x79b, 0x8ac,
}};
constexpr eng::Palette32 kHudPalette {{
	0x000, 0xfff, 0xff0, 0xf00,
}};

constexpr eng::debug::Version kVersion {1u, 0u};

// Actividad del "juego" (el HUD es lo que se demuestra): un actor que rebota.
// El actor se dibuja con el **sistema de objetos del engine** (`World::add_actor` con
// `BackgroundPolicy::SaveUnder`): el Blitter guarda/restaura el fondo que tapa, de modo que
// los bloques estaticos no se danan (el borrado con relleno negro los dejaba marcados) y no
// hay ventana de "actor ausente" entre borrado y pintado (el motor encadena
// restaurar->guardar->dibujar en el plan, ejecutado tras el VBlank).
constexpr u16 kActorSize = 16u;
constexpr u16 kActorY = 150u;
constexpr u16 kActorTravel = kWidth - 2u * kActorSize;
constexpr u8 kActorPlanes = 4u; // planos del campo de juego (color 15 = los cuatro)
// Hoja **par** de cookie-cut (`BobMaskPack::InterleavedPair`): por cada fila fisica (fila de
// arte x plano), `[imagen k][guarda k][mascara k][guarda k]` con `k = 1` palabra (arte de
// 16 px); 4 palabras = 8 bytes por fila. La ventana del Bob (`bob.width`) incluye la guarda:
// 32 px. El dibujo opaco anterior (2 palabras con la guarda a cero, `D = A`) pintaba la
// guarda como **banda negra** de 16 px a la derecha del actor: invisible sobre fondo negro,
// visible al cruzar los bloques de color (defecto reportado). El cookie-cut `$CA`
// (`D = (A & B) | (~A & C)`) preserva el fondo fuera del objeto por construccion.
constexpr u16 kActorSheetRowBytes = 8u; // 4 palabras (imagen, guarda, mascara, guarda)
constexpr u32 kActorSheetBytes =
	static_cast<u32>(kActorSheetRowBytes) * kActorSize * kActorPlanes;
// Save-under: por plano, `kActorSaveWords` palabras de guardado x `kActorSize` filas.
constexpr u16 kActorSaveWords = 2u; // la caja de la hoja par procesa width/16 = 2 palabras
constexpr u32 kActorSaveBytes = static_cast<u32>(kActorSaveWords) * kActorSize * kActorPlanes * 2u;

// Geometria del medidor de energia: marco {72,16,128,8}; relleno interior de 6 filas.
constexpr s16 kMeterX = 72;
constexpr s16 kMeterY = 16;
constexpr u16 kMeterW = 128u;
constexpr u16 kMeterInnerMax = 124u;

// Contador de puntos: celda alineada a 16 px (ruta `draw_text_words` del presentador).
constexpr s16 kScoreX = 64;

// Posiciones de los 4 pips de vidas (x por pip, paso 10): tabla, sin multiplicar por frame.
constexpr u16 kPipX[4] = {264u, 274u, 284u, 294u};

/// Juego mínimo: fondo estático + actor que rebota (arriba) y HUD de acción (abajo).
struct HudBandDemo {
	void init(auto& app) {
		// (1) Memoria de la franja: bloque Chip tipado; el lienzo lo liga `HudStrip`.
		auto& mm = app.memory_manager();
		m_band_planes = mm.chip().template reserve<eng::PlaneTag>(kBandBytes, 16u);
		if (!m_band_planes.valid()) {
			eng::amiga::mark_failed(eng::debug::BootFail::Game);
			return;
		}
		eng::hud::HudStripConfig strip_cfg {};
		strip_cfg.width = kWidth;
		strip_cfg.height = kBandHeight;
		strip_cfg.planes = kBandPlanes;
		strip_cfg.background = kHudBg;
		if (!m_strip.bind(m_band_planes, strip_cfg)) {
			eng::amiga::mark_failed(eng::debug::BootFail::Game);
			return;
		}
		m_strip.clear();

		// (2) Contrato de region (H0) y modelo: contador de puntos, energia y vidas.
		eng::hud::HudRegionSpec region {};
		region.placement = eng::hud::HudPlacement::Band;
		region.band = eng::hud::HudBand {kBandTop, kBandHeight, kBandPlanes, true};
		region.budget.max_draw_ops = 32u;
		region.budget.max_copper_moves_per_line = 8u;
		if (!m_hud.add_region(region).valid()) {
			eng::amiga::mark_failed(eng::debug::BootFail::Game);
			return;
		}
		m_score = m_hud.add_counter(10u, 6u, true); // 6 digitos, ceros a la izquierda
		m_energy = m_hud.add_meter(eng::hud::MeterStyle::Linear);
		m_lives = m_hud.add_meter(eng::hud::MeterStyle::Pips);

		// (3) Composicion: campo de juego (escena del App) + franja con paleta propia.
		// `band_from_view` refleja la superficie que el App ya compuso para el juego.
		eng::scene::Band game_band = eng::scene::band_from_view(
			app.device().scene().playfield().hardware_view(), 0u);
		eng::scene::Band hud_band = eng::scene::band_of_planes(
			m_band_planes, kBandPlanes, kBandBytesPerRow, kBandTopRaster);
		hud_band.palette = kHudPalette.words();
		hud_band.palette_colors = 4u;
		eng::scene::RasterLayout layout {};
		(void)layout.add(game_band);
		(void)layout.add(hud_band);
		layout[0u].palette = kGamePalette.words();
		layout[0u].palette_colors = 16u;
		if (!app.present_layout(layout)) {
			eng::amiga::mark_failed(eng::debug::BootFail::Display);
			return;
		}

		// (4) Fondo estatico del juego y parte estatica del HUD (etiquetas, marcos y fondo
		// del medidor). Lo dinamico se repinta por deltas cuando el valor visible cambia.
		eng::Screen screen = app.screen();
		screen.clear(0u);
		(void)screen.fill(eng::Box {0, 180, kWidth, 28u}, 1u);
		(void)screen.fill(eng::Box {40, 150, 24u, 24u}, 3u);
		(void)screen.fill(eng::Box {120, 168, 40u, 12u}, 2u);
		(void)screen.fill(eng::Box {240, 140, 32u, 16u}, 6u);
		(void)screen.fill(eng::Box {180, 60, 48u, 8u}, 5u);
		(void)screen.fill(eng::Box {60, 90, 8u, 60u}, 4u);
		draw_static_hud();
		draw_version_prefix();
		draw_version_number(0u);
		draw_fps(0u);
		m_fps_win.reset(app.frame(), eng::debug::prof_clock());

		// (5) Actor por el **sistema de objetos**: hoja Chip + save-under por buffer. El
		// Blitter restaura el fondo que el actor tapa; el motor encadena los jobs en el plan.
		if (!init_actor(app)) {
			eng::amiga::mark_failed(eng::debug::BootFail::Game);
			return;
		}

		m_ready = true;
		eng::amiga::mark_ready();
	}

	void update(auto& app) {
		if (!m_ready) {
			return;
		}
		const u32 f = app.frame();
		// El juego es la fuente de verdad: `set_*` solo marca *dirty* si el valor visible
		// cambia (contador/medidor se repintan cuando toca, no por frame). Los valores se
		// actualizan en fases distintas (2/20 frames) para repartir los picos del HUD y no
		// salir de 1 campo (un frame que se pasa a 2 campos deja al haz ver el Blitter a
		// medias: parpadeo).
		// Puntos: cada 20 frames (2,5 Hz).
		(void)m_hud.set_counter(m_score, static_cast<s32>(f / 20u));
		// Energia: diente de sierra 60..0 en 120 frames; el ancho del relleno = valor*2
		// (medidor interior de 124 px, maximo 60 -> 120), sin multiplicar por frame.
		if ((f & 1u) == 0u) {
			const s32 phase = static_cast<s32>(f % 120u);
			const s32 energy = phase < 60 ? 60 - phase : phase - 60;
			(void)m_hud.set_meter(m_energy, energy, 60);
		}
		// Vidas: 4 pips, se pierde uno cada 50 frames y se recuperan en el ciclo.
		const s32 lives = 4 - static_cast<s32>((f / 50u) % 5u);
		(void)m_hud.set_meter(m_lives, lives < 0 ? 0 : lives, 4);

		// Actor: el **sistema de objetos** del engine mueve el BOB (restaurar fondo ->
		// guardar fondo nuevo -> dibujar, encolado en el plan y ejecutado en `present`).
		// El recorrido (ida y vuelta) se calcula con desplazamientos, sin `*` ni `/`.
		const u32 half = kActorTravel >> 1u;
		const u32 px = f % kActorTravel;
		const s16 x = static_cast<s16>(px < half ? kActorSize + (px << 1u)
							 : kActorSize + ((kActorTravel - px) << 1u));
		if (x != m_actor_x) {
			if (auto a = app.world().actor(m_actor)) {
				a->desc.x = x;
				a->desc.y = kActorY;
			}
			m_actor_x = x;
		}
	}

	void render(auto& app) {
		if (!m_ready) {
			return;
		}
		// **Orden por banda (carrera con el haz)**: la capa viva del juego (arriba) se emite y
		// ejecuta PRIMERO, cuando el haz aun esta en la parte alta del campo; los blits del
		// actor terminan antes de que el haz alcance sus lineas (y=150 a ~0,58 de campo). El
		// HUD (franja inferior, y>=208) se pinta despues por CPU: el haz tarda ~0,81 de campo
		// en llegar. Con el orden inverso (HUD primero), los blits del actor caian a ~0,6 de
		// campo, justo cuando el haz pasaba por y=150: carrera visible (parpadeo).
		(void)app.draw_world();
		app.present();
		if (m_hud.any_dirty()) {
			build_dynamic_ops();
			(void)m_strip.apply(m_hud.ops());
			m_hud.clear_ops();
			m_hud.clear_dirty();
		}
		// Franja de depuracion obligatoria (AGENTS §0): prefijo fijo (una vez, en `init`) y
		// numero de frames por palabras, cada 2 segundos (longitud constante: sin clear).
		if (app.frame() / 100u != m_hud_second) {
			m_hud_second = app.frame() / 100u;
			draw_version_number(app.frame());
		}
		// fps: la ventana de 1 s se comprueba **cada frame** (~15 ciclos: una resta y una
		// comparacion; `HudFpsWindow` acota a 50 frames por ventana, asi que comprobarla
		// cada 100 frames daria la mitad) y el numero se repinta solo al cerrar la ventana.
		eng::u32 fps10 = 0u;
		if (m_fps_win.update(app.frame(), eng::debug::prof_clock(), fps10)) {
			draw_fps(fps10);
		}
	}

private:
	/// Da de alta el actor del juego por el **sistema de objetos** del engine: hoja planar en
	/// Chip (cuadrado solido de color 15; por fila, palabra de objeto + guarda del *barrel
	/// shifter*) y save-under por buffer de display (`BackgroundPolicy::SaveUnder`). El motor
	/// encadena restaurar→guardar→dibujar en el plan y lo ejecuta en `present`.
	[[nodiscard]] bool init_actor(auto& app) {
		auto& chip = app.device().memory_manager().chip();
		m_actor_sheet = chip.template reserve<eng::BobTag>(kActorSheetBytes, 16u);
		m_actor_save = chip.template reserve<eng::BobTag>(kActorSaveBytes, 16u);
		if (!m_actor_sheet.valid() || !m_actor_save.valid()) {
			return false;
		}
		// Contenido de la hoja par por la vista tipada del bloque (sin `view.data()`):
		// por cada fila de arte y plano, [imagen=0xFFFF][guarda=0][mascara=0xFFFF][guarda=0].
		eng::Words<eng::BobTag> sheet = m_actor_sheet.view.as_words();
		for (eng::u8 p = 0u; p < kActorPlanes; ++p) {
			for (eng::u16 r = 0u; r < kActorSize; ++r) {
				const eng::u32 base = (static_cast<eng::u32>(r) * kActorPlanes + p) * 4u;
				sheet[base] = 0xffffu;      // imagen (16 px, MSB = pixel 0)
				sheet[base + 1u] = 0x0000u; // guarda del barrel shifter
				sheet[base + 2u] = 0xffffu; // mascara de cookie-cut (1 = objeto)
				sheet[base + 3u] = 0x0000u; // guarda de la mascara
			}
		}
		eng::graphics::Bob bob {};
		bob.sheet = m_actor_sheet.mem_view();
		bob.width = static_cast<u16>(2u * kActorSize); // ventana = arte + guarda (hoja par)
		bob.height = kActorSize;
		bob.planes = kActorPlanes;
		bob.layout = eng::graphics::BobLayout::Interleaved;
		bob.sheet_row_bytes = kActorSheetRowBytes;
		bob.draw = eng::graphics::BobDraw::CookieCut;
		bob.mask_pack = eng::graphics::BobMaskPack::InterleavedPair;
		bob.frame_count = 1u;
		m_actor_sprite = eng::graphics::Sprite {bob, kActorSheetBytes, 0u};
		app.world().reset_actors(0u, 60000u, 0u);
		eng::scene::ActorDesc d = eng::scene::actor_desc_from_sprite(
			m_actor_sprite, static_cast<s16>(m_actor_x), kActorY, 10u, 0u,
			eng::scene::TransparencyMode::Mask1Bit, eng::scene::BackgroundPolicy::SaveUnder);
		d.save[0u] = m_actor_save.mem_view();
		d.save_words_per_row = kActorSaveWords;
		d.save_height = kActorSize;
		m_actor = app.world().add_actor(d);
		if (!m_actor.valid()) {
			return false;
		}
		// **El Bob del actor se fija explicito** (patron de la 121): `add_actor` reconstruye
		// el Bob desde el `Visual` (`bob_from_visual`) y ese camino **no puede expresar la
		// hoja par**: dejaria `SeparatePlane` sin mascara y el cookie-cut interleaved se
		// rechaza en silencio (el actor no se dibuja). Se copia el Bob configurado (hoja
		// par con la mascara dentro de la hoja).
		if (auto a = app.world().actor(m_actor)) {
			a->bob = bob;
		}
		return true;
	}

	/// Prefijo fijo de la linea de depuracion (AGENTS §0): `HUD BAND vX.Y.BBBB F` en x=0 con el
	/// build a 4 digitos (longitud constante) y la etiqueta ` FPS ` en x=208. El numero de
	/// frames (`draw_version_number`) va en x=160 y el de fps (`draw_fps`) en x=256; los tres
	/// bloques estan alineados a 16 px (ruta `draw_text_words`). Se pinta una vez.
	void draw_version_prefix() {
		char line[32] {};
		eng::u8 n = 0u;
		for (const char* p = "HUD BAND v"; *p != '\0' && n < sizeof(line) - 1u; ++p) {
			line[n++] = *p;
		}
		line[n++] = static_cast<char>('0' + (kVersion.major % 10u));
		line[n++] = '.';
		line[n++] = static_cast<char>('0' + (kVersion.minor % 10u));
		line[n++] = '.';
		// Build con 4 digitos (relleno de ceros): longitud constante.
		for (eng::u8 d = 0u; d < 4u; ++d) {
			u32 div = 1000u;
			for (eng::u8 k = 0u; k < d; ++k) {
				div /= 10u;
			}
			line[n++] = static_cast<char>('0' + ((kVersion.build / div) % 10u));
		}
		for (const char* p = " F"; *p != '\0' && n < sizeof(line) - 1u; ++p) {
			line[n++] = *p;
		}
		auto surf = m_strip.surface();
		(void)surf.draw_text_words(0, 40, line, kHudFg);
		// Etiqueta de fps en x=208 (16-alineado; el numero va en x=256, `draw_fps`).
		(void)surf.draw_text_words(208, 40, " FPS  ", kHudFg);
	}

	/// Numero de frames de la linea de depuracion: un espacio + 5 digitos con relleno de
	/// ceros en x=160 (16-alineado, tras `HUD BAND vX.Y.BBBB F`). Longitud constante:
	/// sobrescribe las mismas celdas, sin borrar la fila. Cuesta 3 palabras x 8 filas x
	/// 2 planos (~1,7k ciclos), una vez por segundo.
	void draw_version_number(u32 frame) {
		char digits[8] {};
		digits[0] = ' ';
		for (eng::u8 d = 0u; d < 5u; ++d) {
			u32 div = 10000u;
			for (eng::u8 k = 0u; k < d; ++k) {
				div /= 10u;
			}
			digits[1u + d] = static_cast<char>('0' + ((frame / div) % 10u));
		}
		auto surf = m_strip.surface();
		(void)surf.draw_text_words(160, 40, digits, kHudFg);
	}

	/// fps de la demo (ventana de 1 s del reloj de ciclos, x10 para tener un decimal) en
	/// x=256: 2 digitos + '.' + 1 digito ("49.4"). Etiqueta " FPS" fija en `init` (x=208).
	/// Se repinta cada ventana cerrada, sobre las mismas celdas (longitud constante).
	void draw_fps(u32 fps10) {
		char digits[7] {};
		digits[0] = static_cast<char>('0' + (fps10 / 100u));      // decenas de fps (0..5)
		digits[1] = static_cast<char>('0' + ((fps10 / 10u) % 10u)); // unidades
		digits[2] = '.';
		digits[3] = static_cast<char>('0' + (fps10 % 10u));       // decima
		digits[4] = ' ';
		digits[5] = ' ';
		auto surf = m_strip.surface();
		(void)surf.draw_text_words(256, 40, digits, kHudFg);
	}

	/// Parte estatica del HUD: etiquetas, marco de la franja y marco del medidor (una vez).
	void draw_static_hud() {
		push_text(8, 4, "SCORE", kHudFg);
		push_text(8, 16, "ENERGY", kHudFg);
		push_text(216, 4, "LIVES", kHudFg);
		eng::hud::HudDrawOp frame {};
		frame.kind = eng::hud::HudOpKind::Frame;
		frame.box = eng::Box {0, 0, kWidth, kBandHeight};
		frame.color = kHudFg;
		(void)m_hud.push_op(frame);
		eng::hud::HudDrawOp meter_frame {};
		meter_frame.kind = eng::hud::HudOpKind::Frame;
		meter_frame.box = eng::Box {kMeterX, kMeterY, kMeterW, 8u};
		meter_frame.color = kHudFg;
		(void)m_hud.push_op(meter_frame);
		(void)m_strip.apply(m_hud.ops());
		m_hud.clear_ops();
		m_hud.clear_dirty();
	}

	/// Operaciones dinamicas **por deltas**: solo se emite lo que cambio desde el ultimo
	/// repintado (barra que crece/encoge, pips que cambian de estado y texto del contador).
	/// El presentador pinta operaciones sin fondo: el delta usa el color de la franja para
	/// liberar el tramo que la barra deja atras.
	void build_dynamic_ops() {
		m_hud.clear_ops();

		// Contador: solo si el texto cambio (se compara con la copia del ultimo repintado).
		const eng::Span<const char> text = m_hud.counter_text(m_score);
		bool text_changed = text.size() != m_score_len;
		for (eng::usize i = 0u; !text_changed && i < text.size(); ++i) {
			text_changed = text[i] != m_score_text[i];
		}
		if (text_changed) {
			// `draw_text_words` escribe la palabra completa (patron y fondo) por fila y
			// plano: sobrescribe las mismas celdas y no necesita borrado previo.
			eng::hud::HudDrawOp digits {};
			digits.kind = eng::hud::HudOpKind::Digits;
			// x alineada a 16 px: el presentador usa `draw_text_words` (2 glifos por word).
			digits.box = eng::Box {kScoreX, 4, 48u, 8u};
			digits.color = kHudAccent;
			// Texto **por valor** en el op: `counter_text` apunta al buffer interno del
			// contador y se copia al emitir (el op es autocontenido).
			(void)digits.set_text(text);
			(void)m_hud.push_op(digits);
			m_score_len = static_cast<u8>(text.size());
			for (eng::usize i = 0u; i < text.size() && i < sizeof(m_score_text); ++i) {
				m_score_text[i] = text[i];
			}
		}

		// Energia: relleno interior; delta entre el ancho anterior y el nuevo. Si el color
		// cambia (cruce del umbral de aviso) se repinta la barra completa para que no
		// queden dos tonos mezclados.
		const eng::hud::Meter& energy = m_hud.meter(m_energy);
		const u16 want = energy.value > 0 ? static_cast<u16>(energy.value << 1) : 0u;
		const u16 width = want < kMeterInnerMax ? want : kMeterInnerMax;
		const u8 color = energy.value <= 15 ? kHudWarn : kHudAccent;
		if (color != m_energy_color) {
			push_clear(static_cast<s16>(kMeterX + 1), 17, kMeterInnerMax, 6u);
			if (width > 0u) {
				push_fill(static_cast<s16>(kMeterX + 1), 17, width, 6u, color);
			}
			m_energy_color = color;
			m_energy_w = width;
		} else if (width != m_energy_w) {
			const u16 lo = width < m_energy_w ? width : m_energy_w;
			const u16 hi = width < m_energy_w ? m_energy_w : width;
			push_fill(static_cast<s16>(kMeterX + 1 + static_cast<s16>(lo)), 17,
				  static_cast<u16>(hi - lo), 6u, width > m_energy_w ? color : kHudBg);
			m_energy_w = width;
		}

		// Vidas: un pip cambia de color cuando se pierde o se recupera.
		const eng::hud::Meter& lives = m_hud.meter(m_lives);
		for (u16 i = 0u; i < 4u; ++i) {
			const u8 color = (static_cast<s32>(i) < lives.value) ? kHudAccent : kHudWarn;
			if (color != m_pip_color[i]) {
				push_fill(static_cast<s16>(kPipX[i]), 16, 8u, 8u, color);
				m_pip_color[i] = color;
			}
		}
	}

	/// Relleno de una zona con un color (delta de la barra, pip o fondo de los digitos).
	void push_fill(s16 x, s16 y, u16 w, u16 h, u8 color) {
		eng::hud::HudDrawOp op {};
		op.kind = eng::hud::HudOpKind::Fill;
		op.box = eng::Box {x, y, w, h};
		op.color = color;
		(void)m_hud.push_op(op);
	}

	/// Borrado de una zona con el color de fondo de la franja.
	void push_clear(s16 x, s16 y, u16 w, u16 h) { push_fill(x, y, w, h, kHudBg); }

	/// Encoda una etiqueta estatica como operacion de texto (copia por valor; el literal de la
	/// demo cabe de sobra en `kHudTextMax`).
	void push_text(s16 x, s16 y, const char* text, u8 color) {
		eng::usize n = 0u;
		while (text[n] != '\0' && n < eng::hud::kHudTextMax) {
			++n;
		}
		eng::hud::HudDrawOp op {};
		op.kind = eng::hud::HudOpKind::Text;
		op.box = eng::Box {x, y, static_cast<u16>(n * 8u), 8u};
		op.color = color;
		(void)op.set_text(eng::Span<const char> {text, n});
		(void)m_hud.push_op(op);
	}

	eng::hud::Hud<> m_hud {};
	eng::hud::HudStrip m_strip {};
	eng::Block<eng::PlaneTag, eng::MemoryKind::Chip> m_band_planes {};
	eng::hud::CounterHandle m_score {};
	eng::hud::MeterHandle m_energy {};
	eng::hud::MeterHandle m_lives {};
	// Actor del juego (sistema de objetos): hoja + save-under + sprite + id.
	eng::Block<eng::BobTag, eng::MemoryKind::Chip> m_actor_sheet {};
	eng::Block<eng::BobTag, eng::MemoryKind::Chip> m_actor_save {};
	eng::graphics::Sprite m_actor_sprite {};
	eng::scene::ActorId m_actor {};
	// Estado del ultimo repintado (para emitir solo deltas).
	char m_score_text[8] {};
	u8 m_score_len = 0u;
	u16 m_energy_w = 0u;
	u8 m_energy_color = 0xffu;
	u8 m_pip_color[4] = {0xffu, 0xffu, 0xffu, 0xffu};
	u16 m_actor_x = kActorSize;
	u32 m_hud_second = ~0u;
	eng::debug::HudFpsWindow m_fps_win {}; ///< ventana de fps de la franja de depuracion
	bool m_ready = false;
};

// El estado del juego (`Hud<>` son ~7 KB con las capacidades por defecto) vive en **BSS**
// (convención del repo para el estado del juego): `run_demo` ya deja `AmigaBackend`,
// `DemoHost` y `App` —decenas de KB— en la pila del lanzador; un `main` con el estado del
// juego en local ata el demo a la pila que le dé el lanzador y no escala con mundos grandes.
HudBandDemo g_game {};

} // namespace

int main() {
	eng::GameDisplay display {};
	display.width = kWidth;
	display.height = kHeight;
	display.color_depth = 4u;
	// Interleaved: la escena expone su `CanvasPlayfield` y `band_from_view` lo compone.
	display.layout = eng::graphics::PlaneLayout::Interleaved;
	display.palette = kGamePalette;
	return eng::amiga::run_demo(g_game, display);
}
