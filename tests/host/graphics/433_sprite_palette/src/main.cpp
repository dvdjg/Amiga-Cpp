// ============================================================================
// Test HOST-433: arbitraje de paleta de Sprites HW (`eng/graphics/sprite_palette.hpp`) - F7.
// ============================================================================
//
// Respalda el planificador puro de F7: conflicto simultáneo (mismo par/registros con paletas
// distintas en líneas solapadas) → degrada el de menor `z`; reuso vertical en franjas
// disjuntas → conmutación por Copper (con su `CopperIntent` PaletteLine); paletas idénticas
// (mismo puntero) o registros disjuntos → sin conflicto.
//
//   CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/graphics/433_sprite_palette

#include <cstdio>

#include <eng/graphics/sprite_palette.hpp>

namespace {

int g_fail = 0;
void check(bool ok, const char* m) {
	if (!ok) {
		std::printf("[FAIL] %s\n", m);
		++g_fail;
	}
}

using eng::u16;
using eng::u8;
using eng::graphics::CopperIntentKind;
using eng::graphics::plan_sprite_palettes;
using eng::graphics::SpritePaletteDecision;
using eng::graphics::SpritePaletteNeed;
using eng::graphics::SpritePaletteUse;

alignas(4) eng::u16 g_pal_a[4] = {0x111u, 0x222u, 0x333u, 0x444u};
alignas(4) eng::u16 g_pal_b[4] = {0x555u, 0x666u, 0x777u, 0x888u};

SpritePaletteUse use(u8 channel, u8 first, u8 count, u16 top, u16 bottom, const u16* colors,
		     u8 z) {
	return SpritePaletteUse {channel, SpritePaletteNeed {first, count, top, bottom}, colors, z};
}

} // namespace

int main() {
	std::printf("== HOST-433 sprite_palette ==\n");

	// A) Conflicto simultáneo: mismo par (17..19) en líneas solapadas con paletas distintas
	//    -> degrada el de menor z.
	{
		const SpritePaletteUse uses[2] = {use(0u, 17u, 3u, 100u, 120u, g_pal_a, 10u),
						  use(1u, 17u, 3u, 100u, 120u, g_pal_b, 20u)};
		SpritePaletteDecision out[2] = {};
		eng::graphics::CopperIntent copper[2] = {};
		const eng::u16 n = plan_sprite_palettes(
			eng::Span<const SpritePaletteUse> {uses, 2u},
			eng::Span<SpritePaletteDecision> {out, 2u},
			eng::Span<eng::graphics::CopperIntent> {copper, 2u});
		check(n == 0u, "conflicto: sin conmutaciones");
		check(out[0] == SpritePaletteDecision::Degrade, "conflicto: z bajo degrada");
		check(out[1] == SpritePaletteDecision::Ok, "conflicto: z alto se queda");
	}

	// B) Registros disjuntos (pares distintos) -> los dos OK.
	{
		const SpritePaletteUse uses[2] = {use(0u, 16u, 4u, 100u, 120u, g_pal_a, 10u),
						  use(2u, 20u, 4u, 100u, 120u, g_pal_b, 20u)};
		SpritePaletteDecision out[2] = {};
		const eng::u16 n = plan_sprite_palettes(
			eng::Span<const SpritePaletteUse> {uses, 2u},
			eng::Span<SpritePaletteDecision> {out, 2u}, {});
		check(n == 0u && out[0] == SpritePaletteDecision::Ok &&
			      out[1] == SpritePaletteDecision::Ok,
		      "pares disjuntos: sin conflicto");
	}

	// C) Reuso vertical: mismo canal/registros en franjas disjuntas y paletas distintas
	//    -> conmutación por Copper en la línea de entrada del segundo.
	{
		const SpritePaletteUse uses[2] = {use(3u, 17u, 3u, 60u, 80u, g_pal_a, 10u),
						  use(3u, 17u, 3u, 100u, 120u, g_pal_b, 10u)};
		SpritePaletteDecision out[2] = {};
		eng::graphics::CopperIntent copper[2] = {};
		const eng::u16 n = plan_sprite_palettes(
			eng::Span<const SpritePaletteUse> {uses, 2u},
			eng::Span<SpritePaletteDecision> {out, 2u},
			eng::Span<eng::graphics::CopperIntent> {copper, 2u});
		check(n == 1u && out[0] == SpritePaletteDecision::Ok &&
			      out[1] == SpritePaletteDecision::CopperSwitch,
		      "reuso vertical: conmuta el segundo");
		check(copper[0].kind == CopperIntentKind::PaletteLine && copper[0].top == 100u &&
			      copper[0].first == 17u && copper[0].count == 3u &&
			      copper[0].colors.data() == g_pal_b,
		      "intencion de conmutacion (linea/registros/paleta)");
	}

	// D) Misma paleta (mismo puntero) en franjas disjuntas -> sin conmutación.
	{
		const SpritePaletteUse uses[2] = {use(3u, 17u, 3u, 60u, 80u, g_pal_a, 10u),
						  use(3u, 17u, 3u, 100u, 120u, g_pal_a, 10u)};
		SpritePaletteDecision out[2] = {};
		const eng::u16 n = plan_sprite_palettes(
			eng::Span<const SpritePaletteUse> {uses, 2u},
			eng::Span<SpritePaletteDecision> {out, 2u}, {});
		check(n == 0u && out[1] == SpritePaletteDecision::Ok, "misma paleta: sin trabajo");
	}

	// E) Sin requisito de paleta (count 0) -> Ok y sin tocar nada.
	{
		const SpritePaletteUse uses[1] = {use(0u, 0u, 0u, 0u, 0u, nullptr, 0u)};
		SpritePaletteDecision out[1] = {SpritePaletteDecision::Degrade};
		const eng::u16 n = plan_sprite_palettes(
			eng::Span<const SpritePaletteUse> {uses, 1u},
			eng::Span<SpritePaletteDecision> {out, 1u}, {});
		check(n == 0u && out[0] == SpritePaletteDecision::Ok, "sin necesidad: Ok");
	}

	// F) Empate de z -> degrada el índice mayor; sin buffer de Copper la decisión se
	//    mantiene pero no se emite la intención.
	{
		const SpritePaletteUse uses[2] = {use(0u, 17u, 3u, 100u, 120u, g_pal_a, 5u),
						  use(1u, 17u, 3u, 100u, 120u, g_pal_b, 5u)};
		SpritePaletteDecision out[2] = {};
		const eng::u16 n = plan_sprite_palettes(
			eng::Span<const SpritePaletteUse> {uses, 2u},
			eng::Span<SpritePaletteDecision> {out, 2u}, {});
		check(n == 0u && out[0] == SpritePaletteDecision::Ok &&
			      out[1] == SpritePaletteDecision::Degrade,
		      "empate de z: degrada el indice mayor");

		const SpritePaletteUse vert[2] = {use(3u, 17u, 3u, 60u, 80u, g_pal_a, 1u),
						  use(3u, 17u, 3u, 100u, 120u, g_pal_b, 1u)};
		SpritePaletteDecision out2[2] = {};
		const eng::u16 n2 = plan_sprite_palettes(
			eng::Span<const SpritePaletteUse> {vert, 2u},
			eng::Span<SpritePaletteDecision> {out2, 2u}, {});
		check(n2 == 0u && out2[1] == SpritePaletteDecision::CopperSwitch,
		      "sin buffer: decision CopperSwitch sin emision");
	}

	if (g_fail != 0) {
		std::printf("%d fallo(s)\n", g_fail);
		return 1;
	}
	std::printf("OK: arbitraje de paleta de sprites validado.\n");
	return 0;
}
