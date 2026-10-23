#pragma once

/// \file types.hpp
/// **Vocabulario base de la escena 3D de juego** (G0/G1 de `ROADMAP_3D_GAME_API.md`):
/// `ModelHandle` (referencia compacta a una malla registrada) y `Mesh` (vista n-gon
/// en el escalar de cámara `eng::coord`). Vive en su propia cabecera para que la
/// tabla de modelos (`model_table.hpp`) y la fachada (`eng/api/3d.hpp`) compartan
/// los tipos sin dependencias circulares.
///
/// Sin heap, sin RTTI, sin excepciones y sin hardware.

#include <eng/core/data/mesh3d.hpp>
#include <eng/core/math/scalar.hpp>
#include <eng/core/types/types.hpp>

namespace eng::scene3d {

/// Vista de malla que consume la escena: n-gon convexo, sin copia de vértices.
using Mesh = math3d::PolyMeshViewT<eng::coord>;

/// **Handle generacional de un nodo** de la escena. `index` ocupa un slot del pool
/// y `generation` cambia al reciclarlo; un handle viejo deja de ser válido.
struct NodeId {
	u16 index = 0xffffu; ///< slot del pool; `0xffff` = inválido.
	u16 generation = 0u; ///< generación del slot; cambia al liberar/reciclar.

	[[nodiscard]] constexpr bool valid() const noexcept { return index != 0xffffu; }
	/// \param other handle a comparar.
	/// \return `true` si indice y generacion coinciden.
	[[nodiscard]] constexpr bool operator==(const NodeId& other) const noexcept {
		return index == other.index && generation == other.generation;
	}
	/// \param other handle a comparar.
	/// \return `true` si difieren indice o generacion.
	[[nodiscard]] constexpr bool operator!=(const NodeId& other) const noexcept {
		return !(*this == other);
	}
};

/// **Referencia a una malla registrada** en una `ModelTable` o en la tabla que el
/// llamador pase a `Scene::render`.
///
/// Es un índice compacto, no un puntero: el asset runtime (G1) lo resolverá contra
/// las vistas `Mesh` cargadas del blob UAF-R. `0xffff` significa «sin modelo». El
/// índice es local a la tabla usada en cada llamada; no se mezclan tablas.
struct ModelHandle {
	u16 index = 0xffffu; ///< índice en la tabla de modelos; `0xffff` = inválido.

	[[nodiscard]] constexpr bool valid() const noexcept { return index != 0xffffu; }
	/// \param other handle a comparar.
	/// \return `true` si el indice coincide.
	[[nodiscard]] constexpr bool operator==(const ModelHandle& other) const noexcept {
		return index == other.index;
	}
	/// \param other handle a comparar.
	/// \return `true` si el indice difiere.
	[[nodiscard]] constexpr bool operator!=(const ModelHandle& other) const noexcept {
		return !(*this == other);
	}
};

} // namespace eng::scene3d
