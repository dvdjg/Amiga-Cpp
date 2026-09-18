#pragma once

/// \file types.hpp
/// Tipos base del **modelo de simulación de ecosistema** (`eng::sim`): identificadores
/// compactos, constantes de capacidad y rasgos de compilación.
///
/// `eng::sim` describe un mundo vivo (criaturas con necesidades, memoria, relaciones y
/// sociedad) de forma **agnóstica a la representación** (2D, isométrico o 3D) y al
/// backend. No conoce sprites, tiles ni registros de hardware: solo estado lógico sobre
/// datos enteros pequeños, apto para el 68000 (sin heap, sin excepciones, sin `float`).
///
/// La simulación se organiza en dos planos (ver `docs/engine/architecture/SIM_ECOSYSTEM.md`):
/// el **abstracto** (todo el mundo, tick escalonado y barato) y el **realizado** (las
/// room cercanas a la cámara, con decisión completa). `SimWorld` implementa el LOD.
///
/// Los identificadores usan el convenio "0 = válido, centinela = máximo": `no_entity`
/// (`0xffff`) marca "ninguno" y `no_room` (`0xff`) "sin habitación". Las capacidades se
/// fijan en compilación como parámetros de plantilla y se validan con `static_assert`.
///
/// Verificación: HOST-152 (modelo de criatura) y HOST-153 (mundo/LOD).

#include <eng/core/arith.hpp>
#include <eng/core/types.hpp>

namespace eng::sim {

/// Identificador de entidad. `u16` cubre de sobra cientos de criaturas y objetos.
using EntityId = eng::u16;

/// Valor "ninguna entidad".
inline constexpr EntityId no_entity = 0xffffu;

/// Identificador de habitación/región. Un mundo de hasta 255 rooms.
using RoomId = eng::u8;

/// Valor "sin habitación" (una criatura puede estar aún sin asignar).
inline constexpr RoomId no_room = 0xffu;

/// Identificador de especie (índice a la tabla de `Species`).
using SpeciesId = eng::u8;

/// Valor "sin especie".
inline constexpr SpeciesId no_species = 0xffu;

/// Identificador de facción/colonia (índice a la tabla de reputación de `Society`).
using FactionId = eng::u8;

/// Índice de un tirón/consulta de utilidad (escala 0..1000, como `eng::ai::Utility`).
using Score = eng::s16;

/// Capacidades por defecto del modelo (parámetros de plantilla de `AbstractCreature`).
/// Son pequeñas a propósito: en el A500 el presupuesto manda y una criatura guarda solo
/// un puñado de trackers y relaciones activas.
inline constexpr eng::u8 kDefaultMaxTrackers = 6u;
inline constexpr eng::u8 kDefaultMaxRelations = 6u;
inline constexpr eng::u8 kMaxPackMembers = 6u;
inline constexpr eng::u8 kMaxFactions = 8u;
inline constexpr eng::u8 kMaxMemoryEvents = 4u;
inline constexpr eng::u8 kMaxKnowledge = 8u;
inline constexpr eng::u8 kMaxPlanSteps = 8u;

/// **Rasgos de compilación** de la simulación. Se pasan como tipo (`Traits`) a
/// `SimWorld` y a las funciones de decisión, de modo que las decisiones se toman en
/// tiempo de compilación con `if constexpr`: los módulos desactivados no generan código
/// (el compilador los elimina) y la RAM de trabajo se ajusta por perfil de máquina.
///
/// - `emotions`: el modelo de mente (`Mind`) aporta modificadores afectivos a la utilidad.
/// - `society`: reputación, packs y jerarquía entran en la decisión social.
/// - `abstract_tick`: las criaturas no realizadas se simulan de forma barata y escalonada.
/// - `stagger_period`: cada cuántos frames se reparte el tick abstracto (1 = todos).
struct SimTraits {
	static constexpr bool emotions = true;
	static constexpr bool society = true;
	static constexpr bool knowledge = true;
	static constexpr bool genetics = true;
	static constexpr bool planning = true;
	static constexpr bool abstract_tick = true;
	static constexpr eng::u8 stagger_period = 4u;
};

/// Perfil mínimo, para el A500 de 512 KB: sin mente, sociedad, conocimiento, genética ni
/// planificación; todo el peso en el bucle por frame. Sirve para medir y comparar.
struct SimTraitsLean {
	static constexpr bool emotions = false;
	static constexpr bool society = false;
	static constexpr bool knowledge = false;
	static constexpr bool genetics = false;
	static constexpr bool planning = false;
	static constexpr bool abstract_tick = true;
	static constexpr eng::u8 stagger_period = 8u;
};

/// Suma saturada a `u8` (nunca desborda; el tope es `value`).
[[nodiscard]] constexpr eng::u8 u8_sat_add(eng::u8 base, eng::u8 delta) noexcept {
	const eng::u16 sum = static_cast<eng::u16>(base) + static_cast<eng::u16>(delta);
	return sum > 255u ? static_cast<eng::u8>(255u) : static_cast<eng::u8>(sum);
}

/// Resta saturada a `u8` (nunca baja de 0).
[[nodiscard]] constexpr eng::u8 u8_sat_sub(eng::u8 base, eng::u8 delta) noexcept {
	return static_cast<eng::u8>(base > delta ? static_cast<eng::u8>(base - delta) : 0u);
}

/// Escala porcentual entera: `value * pct / 100` (producto 16×16 y `divs.w` nativo del
/// 68000; evita el libcall `__udivsi3` de la división de 32 bits).
[[nodiscard]] constexpr eng::u8 u8_scale(eng::u8 value, eng::u8 pct) noexcept {
	const eng::s32 product = static_cast<eng::s32>(value) * static_cast<eng::s32>(pct);
	const eng::s32 out = eng::math::div_wide(product, static_cast<eng::s16>(100));
	return out > 255 ? static_cast<eng::u8>(255u) : static_cast<eng::u8>(out);
}

/// División `u16 / u16` por `divs.w` nativo (`den` y cociente deben caber en `s16`).
[[nodiscard]] constexpr eng::u16 div_u16(eng::u16 num, eng::u16 den) noexcept {
	return static_cast<eng::u16>(
		eng::math::div_wide(static_cast<eng::s32>(num), static_cast<eng::s16>(den)));
}

/// Complemento a 100 de un valor `[0,100]` (para "inverso de un rasgo").
[[nodiscard]] constexpr eng::u8 u8_inv100(eng::u8 value) noexcept {
	return value >= 100u ? 0u : static_cast<eng::u8>(100u - value);
}

/// Mayor de dos `u8`.
[[nodiscard]] constexpr eng::u8 u8_max(eng::u8 a, eng::u8 b) noexcept {
	return a > b ? a : b;
}

/// Menor de dos `u8`.
[[nodiscard]] constexpr eng::u8 u8_min(eng::u8 a, eng::u8 b) noexcept {
	return a < b ? a : b;
}

/// Valor absoluto de un `s16` como `u16` (sin ramas de signo dudosas).
[[nodiscard]] constexpr eng::u16 abs_s16(eng::s16 v) noexcept {
	return v < 0 ? static_cast<eng::u16>(-v) : static_cast<eng::u16>(v);
}

/// Distancia Manhattan entre dos puntos (coste de rejilla; sin cuadrado ni raíz).
[[nodiscard]] constexpr eng::u16 manhattan(eng::s16 ax, eng::s16 ay, eng::s16 bx,
					   eng::s16 by) noexcept {
	return static_cast<eng::u16>(abs_s16(static_cast<eng::s16>(ax - bx)) +
				     abs_s16(static_cast<eng::s16>(ay - by)));
}

/// Visibilidad/efecto de un rasgo de personalidad, como porcentaje con signo en
/// `[-100, +100]`: 0 es neutro (rasgo 50), +100 el máximo (rasgo 100) y -100 el mínimo
/// (rasgo 0). Multiplica el score base con `apply_mod`.
[[nodiscard]] constexpr eng::s16 trait_mod(eng::u8 trait, eng::u8 neutral = 50u) noexcept {
	const eng::s16 diff = static_cast<eng::s16>(trait) - static_cast<eng::s16>(neutral);
	return static_cast<eng::s16>(diff * 2);
}

/// Aplica un modificador porcentual `mod` (en `[-100,+100]`) a un score: devuelve
/// `score * (100 + mod) / 100` recortado a `[0, 1000]`. Usa producto/cociente
/// **ensanchados** de 16 bits (`muls.w`/`divs.w` en 68000), sin libcalls de 32 bits.
[[nodiscard]] constexpr Score apply_mod(Score score, eng::s16 mod) noexcept {
	if (score <= 0) {
		return 0;
	}
	eng::s32 factor = 100 + mod;
	if (factor < 0) {
		factor = 0;
	}
	const eng::s32 scaled = eng::math::mul_wide(score, static_cast<eng::s16>(factor));
	const eng::s32 out = eng::math::div_wide(scaled, static_cast<eng::s16>(100));
	if (out <= 0) {
		return 0;
	}
	return out > 1000 ? static_cast<Score>(1000) : static_cast<Score>(out);
}

} // namespace eng::sim
