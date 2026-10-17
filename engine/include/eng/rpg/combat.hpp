#pragma once

/// \file combat.hpp
/// **Conflicto** (`eng::rpg`, hito M3): vitalidad, pipeline de daño por fases fijas y
/// resistencias por **etiqueta** de daño. Diseño: `docs/engine/architecture/RPG_LAYER.md` §5.3.
///
/// El combate **no entrega loot ni toca quests**: resuelve el ataque (fases ordenadas y
/// estables, aritmética entera) y publica el resultado (daño aplicado, muerte); quien
/// escucha (loot, narrativa, HUD) reacciona. El estilo (tiempo real, turnos, táctico) es
/// **flujo de input y tiempo**, no otro núcleo: la firma que usan todos es `resolve_damage`.
///
/// Fases del pipeline (orden fijo, doc. §5.3):
/// 1. payload base (arma/skill) + mods del atacante;
/// 2. mods del defensor;
/// 3. resistencias por etiqueta del defensor;
/// 4. recorte del factor combinado a [-100, +100] (0× a 2× daño);
/// 5. un ataque con payload > 0 hace **al menos 1** salvo inmunidad total (factor 0).
///
/// Verificación: HOST-462. NO VERIFICADA por demo (el slice 400 ejercita el `hurt_actor`
/// contra enemigos; la marca se levanta al cerrar su campaña).

#include <eng/core/types/span.hpp>
#include <eng/core/types/types.hpp>
#include <eng/core/util/intmath.hpp>
#include <eng/rpg/types.hpp>

namespace eng::rpg {

/// **Etiquetas de daño** (máscara de 16 capacidades): el mismo lenguaje que `item_tags`.
/// El juego etiqueta sus skills/armas con estas bases (o define las suyas en los bits
/// libres) y el pipeline cruza contra las resistencias del defensor.
namespace damage_tags {
inline constexpr eng::u16 physical = 1u << 0u; ///< daño físico genérico
inline constexpr eng::u16 slash = 1u << 1u;    ///< filo (espada, hacha)
inline constexpr eng::u16 blunt = 1u << 2u;    ///< contundente (maza, puño)
inline constexpr eng::u16 pierce = 1u << 3u;   ///< perforante (lanza, flecha)
inline constexpr eng::u16 fire = 1u << 4u;     ///< fuego (afijo, hechizo)
inline constexpr eng::u16 cold = 1u << 5u;     ///< frío
inline constexpr eng::u16 poison = 1u << 6u;   ///< veneno
inline constexpr eng::u16 magic = 1u << 7u;    ///< mágico puro
} // namespace damage_tags

/// Vitalidad de un actor de combate. `hp == 0` = derrotado (la muerte la convierte el
/// juego en hecho/evento; el kernel no posee la muerte).
struct Vitality {
	eng::u16 hp = 20u;     ///< puntos de vida actuales (0 = derrotado)
	eng::u16 hp_max = 20u; ///< máximo (para barras de HUD y curas)
	[[nodiscard]] constexpr bool alive() const noexcept { return hp != 0u; }
};

/// Cura `amount` saturado a `hp_max`. Devuelve lo curado realmente.
[[nodiscard]] constexpr eng::u16 heal(Vitality& v, eng::u16 amount) noexcept {
	const eng::u16 room = static_cast<eng::u16>(v.hp_max - v.hp);
	const eng::u16 done = amount < room ? amount : room;
	v.hp = static_cast<eng::u16>(v.hp + done);
	return done;
}

/// Aplica `dealt` a la vitalidad (saturado en 0) y devuelve el daño realmente quitado.
[[nodiscard]] constexpr eng::u16 apply_damage(Vitality& v, eng::u16 dealt) noexcept {
	const eng::u16 done = dealt < v.hp ? dealt : v.hp;
	v.hp = static_cast<eng::u16>(v.hp - done);
	return done;
}

/// Modificador de combate por etiquetas: se suma (por fase) cuando el ataque toca alguna
/// de sus `tags`. `pct` en [-100, +100].
struct CombatMod {
	eng::u16 tags = 0u; ///< etiquetas del ataque a las que aplica
	eng::s8 pct = 0;    ///< porcentaje sumado a la fase (positivo = más daño)
};

/// Resistencia del defensor por etiqueta: `pct` positivo reduce el daño (100 = inmune),
/// negativo lo aumenta (vulnerabilidad).
struct Resistance {
	eng::u16 tags = 0u; ///< etiquetas resistidas
	eng::s8 pct = 0;    ///< reducción porcentual [−100, +100]
};

/// Entrada del pipeline: payload base (arma/habilidad) y etiquetas del ataque.
struct DamageInput {
	eng::u16 base = 0u; ///< daño base (sin mods)
	eng::u16 tags = 0u; ///< etiquetas (damage_tags del contenido + las del arma)
};

/// Tablas del pipeline (contenido del juego, típicamente en ROM).
struct CombatTables {
	eng::Span<const CombatMod> attacker_mods {};   ///< mods del atacante (fase 1)
	eng::Span<const CombatMod> defender_mods {};   ///< mods del defensor (fase 2)
	eng::Span<const Resistance> resistances {};    ///< resistencias del defensor (fase 3)
};

/// Suma (por fase) los `pct` de las filas cuyas etiquetas toquen el ataque, recortando
/// cada fase a [-100, +100]. Función pura: se usa en el pipeline y en los tests.
template <class Row>
[[nodiscard]] constexpr eng::s16 sum_mods(eng::Span<const Row> rows, eng::u16 tags) noexcept {
	eng::s16 acc = 0;
	for (eng::usize i = 0; i < rows.size(); ++i) {
		if ((rows[i].tags & tags) != 0u) {
			acc = static_cast<eng::s16>(acc + rows[i].pct);
		}
	}
	if (acc > 100) {
		return 100;
	}
	if (acc < -100) {
		return -100;
	}
	return acc;
}

/// **Resuelve el daño de un ataque** con las fases fijas del archivo. Devuelve el daño
/// final (puede ser 0 por inmunidad total). Sin multiplicaciones ni divisiones de 32 bits
/// que no sean nativas (`div32` de `eng::util`): apto para el 68000.
[[nodiscard]] inline eng::u16 resolve_damage(const DamageInput& in,
					     const CombatTables& tables) noexcept {
	if (in.base == 0u) {
		return 0u;
	}
	// Fases 1-3: mods del atacante + del defensor − resistencias (cada fase recortada).
	const eng::s16 attacker = sum_mods(tables.attacker_mods, in.tags);
	const eng::s16 defender = sum_mods(tables.defender_mods, in.tags);
	const eng::s16 resisted = sum_mods(tables.resistances, in.tags);
	eng::s16 factor = static_cast<eng::s16>(100 + attacker + defender - resisted);
	if (factor > 200) {
		factor = 200; // fase 4: 0×..2×
	}
	if (factor <= 0) {
		return 0u; // inmunidad total: ni el mínimo de 1 aplica
	}
	// Daño = base × factor / 100 con aritmética exacta (producto u32 y `div32` nativo por
	// mitades de divu.w, sin `__udivsi3` en 68000) y saturación a u16.
	const eng::u32 scaled = static_cast<eng::u32>(in.base) * static_cast<eng::u32>(factor);
	const eng::u32 dealt = eng::util::div32(scaled + 50u, 100u); // redondeo al más cercano
	eng::u16 out = dealt > 0xffffu ? 0xffffu : static_cast<eng::u16>(dealt);
	if (out == 0u) {
		out = 1u; // fase 5: un golpe con payload positivo hace al menos 1
	}
	return out;
}

} // namespace eng::rpg
