#pragma once

/// \file affix.hpp
/// **Afijos y mejora** (`eng::rpg`, hito M2): el progreso del objeto concreto. Un afijo es
/// una capacidad permanente ("de fuego", "afilada") que aporta modificadores de atributo;
/// la mejora (`reinforce`) sube el nivel de la pieza y **escala** sus modificadores.
///
/// Modelo de datos: `ItemInstance.affixes` es una **máscara de bits** (bit i = afijo i del
/// catálogo del juego) y `ItemInstance.upgrade` un nivel. El catálogo de afijos
/// (`AffixDef`) declara qué objetos lo aceptan (por etiquetas) y su rango en la tabla de
/// `StatMod` de afijos; la expansión a modificadores la hace `gather_item_mods`, que el
/// equipo usa para alimentar `recompute_stats` (`stats.hpp`). Diseño: `RPG_LAYER.md` §5.2.
///
/// La escala de mejora es **aditiva en porcentaje** (`add_pct += upgrade × step`): evita una
/// división por modificador y deja el recorte de porcentajes en el pipeline de stats, con
/// aritmética entera exacta.
///
/// Verificación: HOST-461. NO VERIFICADA por demo (vertical slice en el roadmap, M6).

#include <eng/core/types/span.hpp>
#include <eng/core/types/types.hpp>
#include <eng/rpg/items.hpp>
#include <eng/rpg/stats.hpp>

namespace eng::rpg {

/// Número máximo de afijos distintos (bits de `ItemInstance::affixes`).
inline constexpr eng::u8 kMaxAffixes = 16u;

/// Definición de afijo (fila del catálogo del juego, típicamente en ROM).
struct AffixDef {
	eng::u16 allowed_tags = 0u; ///< etiquetas del objeto que acepta el afijo (0 = cualquiera)
	eng::u8 mod_first = 0u;     ///< primer índice en la tabla de `StatMod` de afijos
	eng::u8 mod_count = 0u;     ///< nº de `StatMod` del afijo
};

/// Aplica el afijo `bit` (0..15) a la instancia si la definición de su tipo lo acepta y no
/// estaba ya aplicado. Marca la instancia (no aplica el efecto: eso lo hace la recolección
/// de mods). `false` sin efecto si el bit no cabe, la etiqueta no cuadra o ya estaba.
[[nodiscard]] inline bool apply_affix(ItemInstance& inst, const ItemDef& def, eng::u8 bit,
				      const AffixDef& affix) noexcept {
	if (bit >= kMaxAffixes) {
		return false;
	}
	const eng::u16 mask = 1u << bit;
	if ((inst.affixes & mask) != 0u) {
		return false; // ya estaba
	}
	if (!has_tags(def.tags, affix.allowed_tags)) {
		return false; // el tipo no acepta este afijo (p. ej. "de fuego" solo en armas de filo)
	}
	inst.affixes = inst.affixes | mask;
	return true;
}

/// Tablas de modificadores del juego (rangos de `ItemDef` y de `AffixDef`) más la escala de
/// mejora. Es **dato de contenido**: el engine solo la recorre.
struct ModSources {
	eng::Span<const StatMod> def_mods {};   ///< mods base de los `ItemDef` (mod_first/mod_count)
	eng::Span<const AffixDef> affixes {};   ///< catálogo de afijos (bit i del instance)
	eng::Span<const StatMod> affix_mods {}; ///< mods de afijos (rango del `AffixDef`)
	eng::u8 upgrade_step_pct = 0u;          ///< % añadido a `add_pct` por nivel de mejora (0 = sin escala)
};

/// Copia a `out` los modificadores de una instancia: el rango base de su `ItemDef` y, por
/// cada afijo marcado, el rango de su `AffixDef`; con `upgrade_step_pct > 0` cada copia suma
/// `upgrade × step` a su `add_pct` (recortado a ±100, como el pipeline de stats). Devuelve
/// cuántos escribió; si `out` se llena escribe los que caben (el conteo lo delata).
[[nodiscard]] inline eng::u16 gather_item_mods(const ItemInstance& inst, const ItemDef& def,
					       const ModSources& sources,
					       eng::Span<StatMod> out) noexcept {
	eng::u16 written = 0u;
	auto copy = [&](const StatMod& mod) {
		if (written >= out.size()) {
			return;
		}
		StatMod scaled = mod;
		if (sources.upgrade_step_pct != 0u && inst.upgrade != 0u) {
			eng::s16 pct = mod.add_pct + inst.upgrade * sources.upgrade_step_pct;
			if (pct > 100) {
				pct = 100;
			} else if (pct < -100) {
				pct = -100;
			}
			scaled.add_pct = pct;
		}
		out[written] = scaled;
		++written;
	};
	for (eng::u8 k = 0u; k < def.mod_count; ++k) {
		const eng::usize index = def.mod_first + k;
		if (index >= sources.def_mods.size()) {
			break; // rango de contenido mal formado: corta, no lee fuera
		}
		copy(sources.def_mods[index]);
	}
	for (eng::u8 bit = 0u; bit < kMaxAffixes; ++bit) {
		const eng::u16 mask = 1u << bit;
		if ((inst.affixes & mask) == 0u || bit >= sources.affixes.size()) {
			continue;
		}
		const AffixDef& affix = sources.affixes[bit];
		for (eng::u8 k = 0u; k < affix.mod_count; ++k) {
			const eng::usize index = affix.mod_first + k;
			if (index >= sources.affix_mods.size()) {
				break;
			}
			copy(sources.affix_mods[index]);
		}
	}
	return written;
}

/// Sube el nivel de mejora de una instancia hasta `max_level` (el coste material lo pone la
/// regla de crafteo del juego). `false` si ya estaba al máximo.
[[nodiscard]] inline bool reinforce(ItemInstance& inst, eng::u8 levels = 1u,
				    eng::u8 max_level = 10u) noexcept {
	if (inst.upgrade >= max_level) {
		return false;
	}
	const eng::u16 next = inst.upgrade + levels;
	inst.upgrade = next > max_level ? max_level : next;
	return true;
}

} // namespace eng::rpg
