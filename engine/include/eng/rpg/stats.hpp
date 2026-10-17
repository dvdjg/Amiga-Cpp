#pragma once

/// \file stats.hpp
/// **Atributos derivados** (`eng::rpg`): el único sitio donde se combinan base + equipo +
/// estado para producir los valores que leen las reglas (ataque, defensa, vida máxima,
/// velocidad…). Ningún sistema calcula "nivel × 2" por su cuenta y el combate **solo lee**
/// `derived` — así el balance se audita en un sitio.
///
/// El pipeline tiene **fases fijas y ordenadas** (como el de daño que lo consumirá):
/// 1. valor base (rasgos, nivel, uso…): `StatSheet::base`;
/// 2. suma de las adiciones de todos los `StatMod` aplicables (equipo, buffs, zona);
/// 3. porcentaje acumulado de cada `StatMod` (`add_pct`, recortado a ±100 por atributo);
/// 4. recorte final a `s16` (aritmética saturante documentada, no envolvente).
///
/// La aritmética usa productos/cocientes ensanchados nativos del 68000 (`muls.w`… vía
/// `eng::math::mul_wide`/`eng::util::div32`) — nada de `float` ni de libcalls de 32 bits.
///
/// Verificación: HOST-456. NO VERIFICADA por demo (vertical slice en
/// `docs/guides/roadmap/ROADMAP_RPG.md` M6).

#include <eng/core/math/arith.hpp>
#include <eng/core/types/span.hpp>
#include <eng/core/types/types.hpp>
#include <eng/core/util/array.hpp>
#include <eng/core/util/intmath.hpp>
#include <eng/rpg/types.hpp>

namespace eng::rpg {

/// Modificador de atributo. Las tablas de contenido encadenan filas: el rango
/// `mod_first`/`mod_count` de un `ItemDef` apunta a las filas del objeto y las recetas/buffs
/// aportan las suyas. Una fila con `stat == no_stat` se ignora (plantillas huecas).
struct StatMod {
	StatId stat = no_stat; ///< atributo afectado (índice en el `StatSheet`)
	eng::s16 add = 0;      ///< suma plana aplicada antes de los porcentajes
	eng::s8 add_pct = 0;   ///< porcentaje adicional aplicado a (base + adiciones), [-100, +100]
};

/// Hoja de atributos: valores base + caché derivada. `derived` solo es válido tras
/// `recompute_stats` (que sube `derived_valid`); al cambiar base, equipo o buffs se vuelve a
/// llamar (el equipo lo hace vía `gather_equipment_mods` + `recompute_stats`).
template <eng::u8 MaxStats>
struct StatSheet {
	static_assert(MaxStats >= 1u && MaxStats <= 0xfeu,
		      "StatSheet: hasta 254 atributos (no_stat está reservado)");

	eng::util::Array<eng::s16, MaxStats> base {};    ///< valores base por atributo (nivel, rasgos)
	eng::util::Array<eng::s16, MaxStats> derived {}; ///< caché derivada (válida si `derived_valid`)
	bool derived_valid = false;                      ///< false = hay que recalcular antes de leer
};

/// Recalcula `sheet.derived` desde `sheet.base` y los modificadores `mods`, con las fases
/// fijas del archivo (suma → porcentaje acumulado → recorte). Mods con `stat` fuera de
/// rango se ignoran; los porcentajes se recortan a ±100 y el resultado final a `s16`.
template <eng::u8 MaxStats>
void recompute_stats(StatSheet<MaxStats>& sheet, eng::Span<const StatMod> mods) noexcept {
	eng::s32 add_acc[MaxStats] {}; // adiciones acumuladas (s32: las pilas de mods no desbordan)
	eng::s16 pct_acc[MaxStats] {}; // porcentajes acumulados, recortados a ±100 por atributo
	for (eng::usize i = 0; i < mods.size(); ++i) {
		const StatMod& mod = mods[i];
		if (mod.stat >= MaxStats) {
			continue; // fila de otro dominio o plantilla hueca
		}
		add_acc[mod.stat] += mod.add;
		eng::s16 pct = pct_acc[mod.stat] + mod.add_pct;
		if (pct > 100) {
			pct = 100;
		} else if (pct < -100) {
			pct = -100;
		}
		pct_acc[mod.stat] = pct;
	}
	for (eng::u8 s = 0; s < MaxStats; ++s) {
		eng::s32 value = sheet.base[s] + add_acc[s];
		if (value > 32767) {
			value = 32767; // saturación explícita: stats fuera de s16 son contenido roto
		} else if (value < -32768) {
			value = -32768;
		}
		const eng::s16 factor = 100 + pct_acc[s]; // 0..200
		// Producto s16×s16 exacto en s32 (muls.w): el cast elige la instanciación `R = s16`
		// de `mul_wide` (el valor ya está recortado al rango de s16).
		const eng::s32 scaled = eng::math::mul_wide(static_cast<eng::s16>(value), factor);
		// División por 100 con divu.w sobre la magnitud (signo aparte, sin libcalls).
		const eng::s32 magnitude = scaled < 0 ? -scaled : scaled;
		const eng::u32 divided = eng::util::div32(static_cast<eng::u32>(magnitude), 100u);
		const eng::s32 quotient = static_cast<eng::s32>(divided); // cociente ≤ 65534: cabe
		const eng::s32 signed_quotient = scaled < 0 ? -quotient : quotient;
		const eng::s32 clamped =
		    signed_quotient > 32767 ? 32767 : (signed_quotient < -32768 ? -32768 : signed_quotient);
		sheet.derived[s] = clamped; // ya recortado al rango de s16
	}
	sheet.derived_valid = true;
}

/// Valor base de un atributo; 0 si el id está fuera de rango (mismo contrato que `FactStore`).
template <eng::u8 MaxStats>
[[nodiscard]] constexpr eng::s16 base_stat(const StatSheet<MaxStats>& sheet, StatId stat) noexcept {
	return stat < MaxStats ? sheet.base[stat] : 0;
}

/// Valor derivado de un atributo (precondición: `derived_valid`; en debug un uso antes de
/// recalcular detiene la CPU con `ENG_ASSERT`). Devuelve 0 si el id está fuera de rango.
template <eng::u8 MaxStats>
[[nodiscard]] constexpr eng::s16 derived_stat(const StatSheet<MaxStats>& sheet, StatId stat) noexcept {
	ENG_ASSERT(sheet.derived_valid);
	return stat < MaxStats ? sheet.derived[stat] : 0;
}

} // namespace eng::rpg
