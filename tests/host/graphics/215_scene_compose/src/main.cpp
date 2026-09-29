// Test host de `eng::graphics::composition::compose` (prototipo de composición de escenas):
// una escena planar se construye uniendo etapas (display + paleta + una etapa propia con
// un PatchHandle), sin una clase por driver.
//
// Ejecución:
//   bash tools/run-host-tests.sh tests/host/graphics/215_scene_compose

#include <cstdio>

#include <eng/graphics/composition/compose.hpp>

using namespace eng;

namespace {

alignas(16) u8 g_chip[512 * 1024];

MemorySystem make_memory() {
	MemorySystem mem;
	mem.chip = eng::ChipArena {g_chip, sizeof(g_chip), MemoryKind::Chip};
	return mem;
}

int failures = 0;
void check(bool ok, const char* msg) {
	if (!ok) {
		std::printf("  [FAIL] %s\n", msg);
		++failures;
	}
}

// Intenciones de compilacion para fijar la huella estatica de `intents`.
constexpr u16 kPal4[4] = {0x000u, 0x123u, 0x456u, 0x789u};
constexpr graphics::CopperIntent kIntentLine {
	graphics::CopperIntentKind::PaletteLine, 100u, 100u, 0u,
	eng::PaletteWords {kPal4, 4u}, 0u, 4u};
constexpr graphics::CopperIntent kIntentOver {
	graphics::CopperIntentKind::PaletteLine, 300u, 300u, 0u,
	eng::PaletteWords {kPal4, 1u}, 0u, 1u};
constexpr graphics::CopperIntent kIntentSplit {
	graphics::CopperIntentKind::BitplaneSplit, 100u, 100u, 0u, {}, 0u, 0u};

} // namespace

int main() {
	MemorySystem mem = make_memory();
	u16 pal[4] = {0x000u, 0x123u, 0x456u, 0x789u};

	graphics::composition::Scene s;
	copper::PatchHandle sky {};
	const bool composed = graphics::composition::compose(
		s, mem, graphics::composition::planar(320, 256, 4), graphics::composition::ocs_a500,
		graphics::composition::display(0x2c81, 0x2cc1, 0x0038, 0x00d0, 0x4200),
		graphics::composition::palette(eng::PaletteWords {pal, 4}, 0, 4),
		// Etapa propia: emite un MOVE parcheable (p. ej. el color de fondo por frame).
		[&](graphics::composition::Scene& sc) {
			sky = sc.scheduler().patchable(copper::Register::COLOR00, 0x0111);
		});

	check(composed, "compose() construye la escena");
	check(s.ok(), "la escena queda ok");
	check(s.bitplanes().data() != nullptr, "hay bitplanes");
	check(s.scheduler().words_used() > 0u, "la copperlist tiene palabras");

	check(sky.valid(), "la etapa emitio un PatchHandle valido");
	sky.set(0x0abcu);
	const u16* words = s.scheduler().data();
	check(words[sky.index + 1u] == 0x0abcu, "el PatchHandle parchea el color por frame");

	// Ciclo de vida: una tarea (FunctionRef) de frame ligada a la escena. El lambda debe
	// tener nombre (la referencia no lo copia).
	int frames = 0;
	auto frame_fn = [&]() { ++frames; };
	s.on_frame(frame_fn);
	s.tick();
	s.tick();
	check(frames == 2, "la tarea de frame corre una vez por tick");

	// Escena con etapas HAM/EHB: repeticion de filas (cuadruplicado) + zonas de paleta.
	graphics::composition::Scene s2;
	graphics::composition::SceneResources r2 = graphics::composition::planar(320, 32, 4);
	r2.rows = 8; // 8 filas logicas x 4 = 32 lineas de display
	const graphics::composition::PaletteZone zones[2] = {
		graphics::composition::PaletteZone {0, eng::PaletteWords {pal, 4}, 0, 4},
		graphics::composition::PaletteZone {16, eng::PaletteWords {pal, 4}, 0, 4},
	};
	const bool ok2 = graphics::composition::compose(
		s2, mem, r2,
		graphics::composition::ocs_a500,
		graphics::composition::display(0x2c81, 0x2cc1, 0x0038, 0x00d0, 0x7a00),
		graphics::composition::palette_zones(eng::Span<const graphics::composition::PaletteZone> {zones, 2}),
		graphics::composition::row_repeat(4, 0x2c, 0x0022));
	check(ok2 && s2.ok(), "escena con row_repeat + zonas de paleta compone");
	check(s2.scheduler().words_used() > 0u, "la copperlist con row_repeat no esta vacia");

	// Layout interleaved: expone surface() para dibujar con primitivas.
	graphics::composition::Scene s3;
	graphics::composition::SceneResources r3 = graphics::composition::planar(64, 64, 4);
	r3.layout = graphics::composition::SceneLayout::Interleaved;
	const bool ok3 = graphics::composition::compose(s3, mem, r3, graphics::composition::ocs_a500,
						  graphics::composition::display(0x2c81, 0x2cc1, 0x0038, 0x00d0, 0x4200));
	check(ok3 && s3.ok(), "escena interleaved compone");
	check(s3.playfield().bitplanes().data() != nullptr, "el playfield tiene bitplanes");
	field::Surface surf = s3.surface();
	const s16 xs[3] = {8, 40, 8};
	const s16 ys[3] = {8, 8, 40};
	check(surf.fill_polygon(xs, ys, 3, 3), "surface() pinta un poligono");

	// Filas logicas + punteros inversos (caso fire-rgb):
	// `rows` reduce el bitmap y `row_repeat` cuadruplica las filas en pantalla.
	graphics::composition::Scene s4;
	graphics::composition::SceneResources r4 = graphics::composition::planar(320, 256, 4);
	r4.rows = 8;
	const bool ok4 = graphics::composition::compose(
		s4, mem, r4,
		graphics::composition::ocs_a500,
		graphics::composition::display(0x2c81, 0x2cc1, 0x0038, 0x00d0, 0x7a00),
		graphics::composition::reverse_ptrs(),
		graphics::composition::row_repeat(4, 0x2c, 0x0022));
	check(ok4 && s4.ok(), "escena con reverse_ptrs + row_repeat compone");

	// Etapa de intents de Copper (capa dinamica ordenada por scanline por el Plan).
	graphics::composition::Scene s5;
	const graphics::CopperIntent it {
		graphics::CopperIntentKind::PaletteLine, 100, 100, 0,
		eng::PaletteWords {pal, 4}, 0, 4};
	const bool ok5 = graphics::composition::compose(
		s5, mem, graphics::composition::planar(320, 256, 4), graphics::composition::ocs_a500,
		graphics::composition::display(graphics::composition::kPal320x256, graphics::composition::kBplcon0_4Planes),
		graphics::composition::intents(eng::Span<const graphics::CopperIntent> {&it, 1}));
	check(ok5 && s5.ok(), "escena con intents de Copper compone");
	check(s5.plan().intent_count() == 1u, "la intencion se registra en el Plan");

	// Zonas de paleta PARCHEABLES: se emiten y devuelven su binding para reescribir colores.
	graphics::composition::Scene s6;
	graphics::composition::ZoneBinding bindings[2] {};
	const graphics::composition::PaletteZone zones2[2] = {
		graphics::composition::PaletteZone {16, eng::PaletteWords {pal, 4}, 0, 4},
		graphics::composition::PaletteZone {32, eng::PaletteWords {pal, 4}, 0, 4},
	};
	const bool ok6 = graphics::composition::compose(
		s6, mem, graphics::composition::planar(320, 256, 4), graphics::composition::ocs_a500,
		graphics::composition::display(graphics::composition::kPal320x256, graphics::composition::kBplcon0_4Planes),
		graphics::composition::palette_zones(
			eng::Span<const graphics::composition::PaletteZone> {zones2, 2},
			eng::Span<graphics::composition::ZoneBinding> {bindings, 2}));
	check(ok6 && s6.ok(), "escena con zonas de paleta parcheables compone");
	check(bindings[1].line == 32u && bindings[1].count == 4u, "los bindings de zona son correctos");
	const copper::PatchHandle zone_h = graphics::composition::zone_color(s6.scheduler(), bindings[1], 2);
	zone_h.set(0x0aaau);
	const u16* zwords = s6.scheduler().data();
	check(zwords[zone_h.index + 1u] == 0x0aaau, "un color de zona se parchea por frame");

	// Base comun: una zona parcheable de CUALQUIER registro (aqui offsets de scanline).
	graphics::composition::Scene s7;
	graphics::composition::PatchZone mod_zone {};
	const graphics::composition::PatchSlot mod_slots[2] = {
		{copper::Register::BPL1MOD, 0x0040},
		{copper::Register::BPL2MOD, 0x0040},
	};
	const bool ok7 = graphics::composition::compose(
		s7, mem, graphics::composition::planar(320, 256, 4), graphics::composition::ocs_a500,
		graphics::composition::display(graphics::composition::kPal320x256, graphics::composition::kBplcon0_4Planes),
		graphics::composition::patchable_zone(
			40, eng::Span<const graphics::composition::PatchSlot> {mod_slots, 2}, &mod_zone));
	check(ok7 && s7.ok(), "zona parcheable generica (offsets de scanline) compone");
	const copper::PatchHandle mod_h = mod_zone.handle(s7.scheduler(), 0);
	mod_h.set(0x0050u);
	const u16* mw = s7.scheduler().data();
	check(mw[mod_h.index + 1u] == 0x0050u, "un offset BPL1MOD se parchea por frame");

	// Doble buffer de display: los BPLxPT se parchean al buffer trasero en commit().
	graphics::composition::Scene s8;
	graphics::composition::SceneResources r8 = graphics::composition::planar(320, 256, 4);
	r8.buffers = 2;
	const bool ok8 = graphics::composition::compose(
		s8, mem, r8,
		graphics::composition::ocs_a500,
		graphics::composition::display(graphics::composition::kPal320x256, graphics::composition::kBplcon0_4Planes));
	check(ok8 && s8.ok(), "escena con 2 buffers de display compone");
	check(s8.buffer_count() == 2u, "hay 2 buffers de display");
	check(s8.buffer(0).data() != s8.buffer(1).data(), "los dos buffers son distintos");
	s8.commit();
	check(s8.back().data() == s8.buffer(0).data(), "commit avanza al buffer trasero");

	// Preset EHB: 6 planos y display con BPLCON0 EHB. `kPal320x256` debe coincidir con la
	// geometria DIW/DDF que usan los drivers EHB/HAM (0x2c81/0x2cc1/0x38/0xd0).
	check(graphics::composition::kPal320x256.diwstrt == 0x2c81u &&
		      graphics::composition::kPal320x256.diwstop == 0x2cc1u &&
		      graphics::composition::kPal320x256.ddfstrt == 0x0038u &&
		      graphics::composition::kPal320x256.ddfstop == 0x00d0u,
	      "kPal320x256 es la geometria DIW/DDF de 320x256");
	graphics::composition::Scene s9;
	const bool ok9 = graphics::composition::compose(
		s9, mem, graphics::composition::planar(320, 256, 6), graphics::composition::ocs_a500,
		graphics::composition::display(graphics::composition::kPal320x256, graphics::composition::kBplcon0_Ehb));
	check(ok9 && s9.ok(), "preset ehb compone");
	check(s9.planes() == 6u, "el preset ehb usa 6 planos");
	bool found_ehb = false;
	for (u16 i = 0; i < s9.scheduler().words_used(); ++i) {
		if (s9.scheduler().data()[i] == graphics::composition::kBplcon0_Ehb) {
			found_ehb = true;
		}
	}
	check(found_ehb, "el display emite BPLCON0 = 0x6200 (EHB)");
	bool found_ham = false;
	for (u16 i = 0; i < s4.scheduler().words_used(); ++i) {
		if (s4.scheduler().data()[i] == graphics::composition::kBplcon0_Ham6) {
			found_ham = true;
		}
	}
	check(found_ham, "el display emite BPLCON0 = 0x7a00 (HAM6)");

	// --- Validacion de configuraciones contra el perfil de la maquina ---------------
	// Estatica (compile-time): configs conocidas en compilacion.
	static_assert(graphics::composition::valid_scene(graphics::composition::planar(320, 256, 6),
						   graphics::composition::ocs_a500));
	static_assert(graphics::composition::valid_scene(graphics::composition::planar(288, 256, 4),
						   graphics::composition::ocs_a500));
	// 336 cabe en el fetch hw (21 palabras <= 25) pero no en los 368 px visibles... en
	// realidad 336 < 368, si es visible; 384 supera 368 y no es valido en OCS.
	static_assert(graphics::composition::valid_scene(graphics::composition::planar(336, 256, 4),
						   graphics::composition::ocs_a500));
	static_assert(!graphics::composition::valid_scene(graphics::composition::planar(384, 256, 4),
						    graphics::composition::ocs_a500));
	// 300 no es valido: no es multiplo de 16. 7 planos tampoco en OCS; en AGA si.
	static_assert(!graphics::composition::valid_scene(graphics::composition::planar(300, 256, 4),
						    graphics::composition::ocs_a500));
	static_assert(!graphics::composition::valid_scene(graphics::composition::planar(320, 256, 7),
						    graphics::composition::ocs_a500));
	static_assert(graphics::composition::valid_scene(graphics::composition::planar(320, 256, 7),
						   graphics::composition::aga_a1200));

	// HAM/EHB/DPF: los planos del modo se validan aparte.
	{
		constexpr graphics::composition::SceneResources ham = [] { auto r = graphics::composition::planar(320, 256, 6); r.mode = graphics::composition::SceneMode::Ham; return r; }();

		static_assert(graphics::composition::valid_scene(ham, graphics::composition::ocs_a500));
		constexpr graphics::composition::SceneResources dpf = [] { auto r = graphics::composition::planar(320, 256, 4); r.mode = graphics::composition::SceneMode::DualPlayfield; return r; }();

		// DPF en OCS admite 3+3: 4 planos por PF no es valido.
		static_assert(!graphics::composition::valid_scene(dpf, graphics::composition::ocs_a500));
	}

	// Dinamica (runtime): el rechazo queda en `config_error()`.
	{
		graphics::composition::Scene bad;
		graphics::composition::SceneResources wide = graphics::composition::planar(384, 256, 4);
		const bool okw = graphics::composition::compose(
			bad, mem, wide, graphics::composition::ocs_a500,
			graphics::composition::display(graphics::composition::kPal320x256,
						 graphics::composition::kBplcon0_4Planes));
		check(!okw, "compose rechaza width=384 en OCS (>368 visibles)");
		check(!bad.config_error().ok(), "config_error marca el rechazo");

		graphics::composition::Scene rec;
		const bool okr = graphics::composition::compose(
			rec, mem, graphics::composition::planar(320, 256, 4), graphics::composition::ocs_a500,
			graphics::composition::display(graphics::composition::kPal320x256,
						 graphics::composition::kBplcon0_4Planes));
		check(okr && rec.config_error().ok(), "compose acepta una config valida");

		const graphics::composition::ConfigError e =
			graphics::composition::validate(graphics::composition::planar(320, 256, 7),
						  graphics::composition::ocs_a500);
		check(e.code == 5u, "validate: 7 planos en OCS = codigo 5 (planos)");
	}

	// --- Huella estatica de las etapas de forma conocida ---------------------------
	// Las funciones `*_words` permiten `static_assert` sobre el presupuesto; aqui se fijan
	// sus valores y se comprueba que coinciden con la emision real (`Scene::words()`).
	static_assert(graphics::composition::display_words(graphics::composition::planar(320, 256, 4)) == 36u);
	static_assert(graphics::composition::display_words(graphics::composition::planar(320, 256, 6)) == 44u);
	static_assert(graphics::composition::palette_words(0u, 4u, 4u) == 8u);
	static_assert(graphics::composition::palette_words(1u, 15u, 16u) == 30u);
	static_assert(graphics::composition::palette_zone_words(0u, 4u, 4u) == 10u);
	static_assert(graphics::composition::reverse_ptrs_words(graphics::composition::planar(320, 256, 4)) == 16u);
	static_assert(graphics::composition::patchable_zone_words(2u) == 6u);
	// Intents: PaletteLine = WAIT(2) + 4 colores x 2; con top>255 suma el par de overflow.
	static_assert(graphics::composition::intent_words(kIntentLine) == 10u);
	static_assert(graphics::composition::intents_words(&kIntentLine, 1u) == 10u);
	static_assert(graphics::composition::intent_words(kIntentOver) == 4u);
	static_assert(graphics::composition::intents_words(&kIntentOver, 1u) == 6u);
	// BitplaneSplit via `intents` no lo materializa el Plan (sin layout): 0 palabras.
	static_assert(graphics::composition::intent_words(kIntentSplit) == 0u);
	{
		// s1: display(36) + palette(8) + patch(2) + fin de lista(2).
		const u16 expected_s1 =
			static_cast<u16>(graphics::composition::display_words(s.resources()) +
					 graphics::composition::palette_words(0u, 4u, 4u) + 2u + 2u);
		check(s.words() == expected_s1,
		      "s1: display+palette+patch coinciden con la huella estatica");
		// s2: display(36) + 2 zonas(10 cada una) + row_repeat(256) + fin(2).
		const u16 expected_s2 = static_cast<u16>(
			graphics::composition::display_words(r2) +
			2u * graphics::composition::palette_zone_words(0u, 4u, 4u) +
			graphics::composition::row_repeat_words(8u, 4u, 0x2cu) + 2u);
		check(s2.words() == expected_s2,
		      "s2: display+zonas+row_repeat coinciden con la huella estatica");
		// s4: display(36) + reverse_ptrs(16) + row_repeat(256) + fin(2).
		const u16 expected_s4 = static_cast<u16>(
			graphics::composition::display_words(r4) +
			graphics::composition::reverse_ptrs_words(r4) +
			graphics::composition::row_repeat_words(8u, 4u, 0x2cu) + 2u);
		check(s4.words() == expected_s4,
		      "s4: display+reverse_ptrs+row_repeat coinciden con la huella estatica");
		// s5: display(36) + 1 intent PaletteLine(10) + fin(2).
		const u16 expected_s5 = static_cast<u16>(
			graphics::composition::display_words(s5.resources()) +
			graphics::composition::intents_words(&it, 1u) + 2u);
		check(s5.words() == expected_s5,
		      "s5: display+intents coinciden con la huella estatica");
		// s7: display(36) + patchable_zone(6) + fin(2).
		const u16 expected_s7 = static_cast<u16>(
			graphics::composition::display_words(s7.resources()) +
			graphics::composition::patchable_zone_words(2u) + 2u);
		check(s7.words() == expected_s7,
		      "s7: display+patchable_zone coinciden con la huella estatica");
	}

	if (failures == 0) {
		std::printf("OK: scene::compose (etapas display/paleta/zonas/row_repeat + PatchHandle + ciclo de vida).\n");
		return 0;
	}
	std::printf("FAIL: %d comprobacion(es) fallaron\n", failures);
	return 1;
}
