#pragma once

/// \file types.hpp
/// Tipos base del **núcleo de mundo RPG** (`eng::rpg`).
///
/// La capa RPG describe el mundo *jugable* de un juego de rol (hechos de historia, objetos
/// con instancia propia, inventario, equipo y progreso) sobre el ecosistema de `eng::sim`
/// (criaturas, necesidades, economía, clima), que actúa de sustrato vivo. Diseño completo y
/// límite entre capas: `docs/engine/architecture/RPG_LAYER.md`.
///
/// Convenios (los mismos que `eng::sim/types.hpp`):
/// - identificadores con "0 = válido, centinela = máximo" (`no_fact`);
/// - capacidades en compilación (parámetros de plantilla) y cero heap;
/// - enteros pequeños (`u8`/`u16`) con coste de 68000 en mente.
///
/// Las **etiquetas de objeto** (`item_tags`) son una máscara de bits: las reglas preguntan
/// "¿tiene la etiqueta arma?" (`has_tags`) en vez de ramificar por clase o nombre — no hay
/// jerarquía `class Sword : Weapon`.
///
/// Verificación: HOST-454. NO VERIFICADA por demo (el vertical slice que la cierra está
/// planificado en `docs/guides/roadmap/ROADMAP_RPG.md`, hito M6).

#include <eng/core/types/types.hpp>

namespace eng::rpg {

/// Identificador de **hecho del mundo** (clave del `FactStore`). El juego asigna los ids
/// con un `enum` de contenido; el almacén los usa como índice denso.
using FactId = eng::u16;

/// Valor "sin hecho" (p. ej. devolver "no encontrado").
inline constexpr FactId no_fact = 0xffffu;

/// Identificador de **tipo de objeto** (índice al `ItemCatalog`). Un catálogo de hasta 255
/// tipos cubre de sobra el contenido de un CRPG de A500 (la tabla vive en ROM o en datos).
using ItemTypeId = eng::u8;

/// Valor "sin tipo de objeto".
inline constexpr ItemTypeId no_item_type = 0xffu;

/// Identificador de **atributo** (índice a `StatSheet`): ataque, defensa, vida máxima… El
/// juego define cuántos y cuáles; el engine solo opera sobre índices y modificadores.
using StatId = eng::u8;

/// Valor "sin atributo": una fila de `StatMod` con este id se ignora (permite plantillas
/// de modificadores huecas en tablas de contenido).
inline constexpr StatId no_stat = 0xffu;

/// **Etiquetas de objeto**: 16 capacidades que las reglas consultan con `has_tags`. Un
/// objeto puede llevar varias a la vez (un cuchillo es `tool` y `weapon`), y las recetas,
/// tiendas y ranuras de equipo preguntan por capacidades, no por tipo concreto.
namespace item_tags {
inline constexpr eng::u16 weapon = 1u << 0u;    ///< se puede empuñar (daño base)
inline constexpr eng::u16 armor = 1u << 1u;     ///< se pone en una ranura de protección
inline constexpr eng::u16 food = 1u << 2u;      ///< se come (enlace con `Needs` de la simulación)
inline constexpr eng::u16 drink = 1u << 3u;     ///< se bebe (agua, poción)
inline constexpr eng::u16 key = 1u << 4u;       ///< abre una cerradura (metadata = qué puerta)
inline constexpr eng::u16 material = 1u << 5u;  ///< ingrediente de recetas (craft)
inline constexpr eng::u16 tool = 1u << 6u;      ///< habilita acciones en una estación de trabajo
inline constexpr eng::u16 valuable = 1u << 7u;  ///< se vende bien (tesoro, moneda)
inline constexpr eng::u16 quest = 1u << 8u;     ///< objeto de misión (las quests lo rastrean)
inline constexpr eng::u16 light = 1u << 9u;     ///< alumbra (mazmorras)
inline constexpr eng::u16 container = 1u << 10u;///< puede guardar otros objetos (cofre, mochila)
inline constexpr eng::u16 ammo = 1u << 11u;     ///< munición de armas a distancia
inline constexpr eng::u16 book = 1u << 12u;     ///< legible (cartas, diarios, mapas)
inline constexpr eng::u16 currency = 1u << 13u; ///< moneda (se intercambia por valor nominal)
inline constexpr eng::u16 mount = 1u << 14u;    ///< permite montar (enlace con `MountLink`)
inline constexpr eng::u16 clothing = 1u << 15u; ///< vestimenta sin protección (trajes, disfraces)
} // namespace item_tags

/// ¿`tags` cubre **todas** las etiquetas pedidas? `mask == 0` siempre cumple ("cualquiera").
/// Es la consulta de capacidad que usan las reglas (recetas, ranuras de equipo, comercio).
[[nodiscard]] constexpr bool has_tags(eng::u16 tags, eng::u16 mask) noexcept {
	return (tags & mask) == mask;
}

/// ¿`tags` contiene **alguna** de las etiquetas pedidas? `mask == 0` nunca cumple.
[[nodiscard]] constexpr bool has_any_tag(eng::u16 tags, eng::u16 mask) noexcept {
	return (tags & mask) != 0u;
}

} // namespace eng::rpg
