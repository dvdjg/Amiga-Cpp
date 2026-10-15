#pragma once

/// \file terrain_types.hpp
/// **Tipos del terreno modular** (`eng::gfx::proc::terrain`): el borde tipado (`Edge`),
/// los rasgos de interior (`Feature`) y la especificación de pieza (`TileSpec`). Son el
/// vocabulario de datos del sistema; el pintor vive en `terrain_paint.hpp` y el
/// contenedor/catálogos en `terrain.hpp`.
///
/// ```text
///   TileSpec { edges = {n,e,s,w: Edge}, features = Feature mask, seed }
///        │
///        ├─ edges       bordes N/E/S/W: lo que casa con los vecinos (Wang/Carcassonne)
///        └─ features    rasgos del interior (rio, lago, arboles, muralla, patio…)
/// ```
///
/// ## Estado de verificación: NO VERIFICADA
/// Solo respaldada por test host (`HOST-442`); sin demo que la ejercite en hardware.

#include <eng/core/types/types.hpp>
#include <eng/core/util/type_traits.hpp>
#include <eng/graphics/proc/tiles.hpp>

namespace eng::gfx::proc::terrain {

/// **Rasgo del borde** de un tile: lo que "conecta" con el vecino por ese lado. Dos tiles
/// encadenan si el id del borde compartido coincide (río con río, muralla con muralla…).
enum class Edge : eng::u8 {
	/// Terreno llano (hierba): sin borde estructural.
	Grass = 0,
	/// Río o lago: el cauce conecta por el centro del borde.
	Water,
	/// Camino de tierra.
	Path,
	/// Muralla / piedra trabajada.
	Wall,
	/// Borde de bosque.
	Forest,
	/// Cresta de montaña.
	Ridge,
	/// Cultivo.
	Crop,
	/// Entrada de cueva / oscuridad.
	Cave,
	/// Número de ids (no es un borde válido).
	Count,
};

/// **Rasgo del interior** de un tile (máscara combinable con `operator|`).
enum class Feature : eng::u16 {
	/// Sin rasgos: solo el material base.
	None = 0u,
	/// El interior lleva un cauce (conecta por los bordes `Water`).
	River = 1u << 0u,
	/// El interior lleva un lago/balsa (con `River` en los cruces).
	Lake = 1u << 1u,
	/// Silueta de montaña (perfil de colina con cima nevada).
	Mountain = 1u << 2u,
	/// Copas de árbol con tronco.
	Trees = 1u << 3u,
	/// Peñascos/rocas sueltas.
	Rocks = 1u << 4u,
	/// Casa con tejado a dos aguas y puerta.
	House = 1u << 5u,
	/// Surcos de cultivo.
	Crops = 1u << 6u,
	/// Tramo de muralla (la orientación la dan los bordes `Wall`).
	WallSeg = 1u << 7u,
	/// Torre cilíndrica (con merlones).
	Tower = 1u << 8u,
	/// Puerta/rastrillo: hueco en el tramo de muralla.
	Gate = 1u << 9u,
	/// Patio interior adoquinado con musgo.
	CastleYard = 1u << 10u,
	/// Boca de cueva oscura con borde rocoso.
	CaveMouth = 1u << 11u,
	/// Puente sobre el cauce (con bordes `Water`+`Path`).
	Bridge = 1u << 12u,
};

/// Combina dos rasgos (OR de la máscara).
/// \param a,b rasgos a unir. \return máscara con ambos bits.
[[nodiscard]] constexpr Feature operator|(Feature a, Feature b) {
	return Feature(eng::util::to_underlying(a) | eng::util::to_underlying(b));
}

/// ¿La máscara `f` contiene el rasgo `bit`?
/// \param f máscara de rasgos. \param bit rasgo a comprobar. \return true si está activo.
[[nodiscard]] constexpr bool has(Feature f, Feature bit) {
	return (eng::util::to_underlying(f) & eng::util::to_underlying(bit)) != 0u;
}

/// **Especificación de una pieza de terreno**: sus cuatro bordes tipados, los rasgos del
/// interior y un seed para la variación orgánica (copas, rocas, cauce, musgo).
struct TileSpec {
	/// Bordes N/E/S/W (el id que casa con el vecino correspondiente).
	EdgeSides<Edge> edges {};
	/// Máscara de rasgos del interior.
	Feature features = Feature::None;
	/// Semilla de la variación orgánica de la pieza.
	eng::u32 seed = 0;
};

} // namespace eng::gfx::proc::terrain
