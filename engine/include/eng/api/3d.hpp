#pragma once

/// \file 3d.hpp
/// **Fachada 3D de juego** (G0 de `ROADMAP_3D_GAME_API.md`): escena retenida de
/// capacidad fija con **handles generacionales**, cámara `Camera3D<eng::coord>` y
/// render de mallas n-gon por `eng::graphics::render_solid`.
///
/// El objetivo es que un juego describa **qué** ve (nodos con posición, malla y
/// visibilidad) sin manejar bitplanes, registros, scratch, orden de pintado ni la
/// ruta de relleno. Esta cabecera es un **subconjunto estable** de esa fachada:
/// la jerarquía padre/hijo, la rotación, los materiales de asset, la animación y
/// la física llegan en las fases G2–G7 del roadmap; se añaden sin romper el
/// vocabulario de esta cabecera.
///
/// ```text
///   NodeDesc ──spawn──► Pool<Node3D, MaxNodes> ──render──► render_solid
///                                                    │
///                                                    └─► Surface (CPU o Blitter
///                                                        según el playfield)
/// ```
///
/// **Convenio numérico (G0, verificado por HOST-488)**: la cámara usa
/// `eng::coord` (`Fixed<s16,0>` en 68000, `int` en host), que es el único escalar
/// que representa `focal`/`half_w` de 320 px en 16 bits. `Q8.8` y `Q4.12` no
/// representan `half_w=160` en `s16`; la precisión sub-unidad de cámara queda para
/// G8 (escala de píxel/LUT de `1/z`). Las posiciones de nodo son **relativas a la
/// cámara** (cámara en el origen, `+z` delante), como en `render3d`.
///
/// Sin heap, sin RTTI, sin excepciones y sin variables globales de estado.

#include <eng/core/data/mesh3d.hpp>
#include <eng/core/math/linalg.hpp>
#include <eng/core/math/scalar.hpp>
#include <eng/core/types/span.hpp>
#include <eng/core/types/types.hpp>
#include <eng/core/util/noncopyable.hpp>
#include <eng/core/util/pool.hpp>
#include <eng/graphics/render3d.hpp>
#include <eng/scene3d/model_table.hpp>
#include <eng/scene3d/types.hpp>

namespace eng::scene3d {

/// Cámara de la escena: misma convención que `eng::graphics::Camera3D` (espacio
/// de cámara con `+x` derecha, `+y` abajo, `+z` delante y cámara en el origen).
/// `focal`, `near_z` y `half_w/half_h` están en `eng::coord`; `cx/cy` en píxeles.
using Camera = graphics::Camera3D<eng::coord>;

/// Nodo vivo de la escena. `position` está en el escalar de cámara
/// (`eng::coord`): la cámara ocupa el origen y mira hacia `+z`.
struct Node3D {
	ModelHandle model {}; ///< malla que se pinta; índice en la tabla de `render`.
	math3d::Vec3t<eng::coord> position {}; ///< posición relativa a la cámara (unidades del escalar).
	bool visible = true; ///< si es `false`, el nodo no entra en el render.
};

/// Descripción de alta de un nodo (`Scene::spawn`). Los campos se inicializan a
/// valores seguros: modelo inválido, origen y visible.
struct NodeDesc {
	ModelHandle model {}; ///< malla a pintar.
	math3d::Vec3t<eng::coord> position {}; ///< posición relativa a la cámara.
	bool visible = true; ///< visibilidad inicial.
};

/// **Escena 3D retenida de capacidad fija**.
///
/// \tparam MaxNodes número máximo de nodos vivos; debe ser > 0 y caber en un índice
///         `u16` (≤ 0xfffe). El pool de nodos ocupa `MaxNodes * (sizeof(Node3D) + 6)`
///         bytes aproximadamente (ítems, generaciones, free-list y bitset).
/// \tparam MaxVertices número máximo de **vértices por malla** (no el total de la
///         escena); debe ser ≥ 3. Es el tamaño del scratch de transformación de un
///         nodo. Una malla mayor se omite en `render` (no se trunca).
///
/// La escena no reserva memoria dinámica: el objeto contiene el pool y todo el
/// scratch. `render` no asigna, no falla y no lanza; los nodos inválidos, las
/// mallas fuera de rango y los modelos demasiado grandes se omiten. Por su
/// tamaño (pool + scratch), la escena se coloca en almacenamiento estático/global
/// o en el arena del juego, **no** en la pila por defecto del 68000.
template <u16 MaxNodes, u16 MaxVertices>
class Scene : public util::Noncopyable {
	static_assert(MaxNodes > 0u, "Scene: MaxNodes debe ser mayor que 0");
	static_assert(MaxNodes <= 0xfffeu, "Scene: MaxNodes debe caber en un indice u16");
	static_assert(MaxVertices >= 3u, "Scene: MaxVertices debe ser al menos 3");

public:
	/// Handle generacional de un nodo. Se invalida al liberar el nodo o al llamar
	/// `clear()`; `valid() == false` es el fallo de `spawn` con la escena llena.
	using NodeId = typename util::Pool<Node3D, MaxNodes>::Handle;

	/// \return capacidad de nodos de esta instancia.
	[[nodiscard]] static constexpr u16 node_capacity() noexcept { return MaxNodes; }
	/// \return capacidad de vértices por malla de esta instancia.
	[[nodiscard]] static constexpr u16 vertex_capacity() noexcept { return MaxVertices; }

	/// Da de alta un nodo en el primer slot libre del pool.
	/// \param desc modelo, posición inicial y visibilidad (se copian al nodo).
	/// \return handle válido del nodo, o un handle inválido si el pool está lleno.
	/// \pre Ninguna; el modelo puede ser inválido (el nodo se omite al renderizar).
	/// \post El nodo queda vivo y `live_count()` sube en uno si el alta tuvo éxito.
	[[nodiscard]] NodeId spawn(const NodeDesc& desc = {}) noexcept {
		const NodeId id = m_nodes.add();
		if (!id.valid()) {
			return {};
		}
		Node3D* node = m_nodes.get(id);
		if (node == nullptr) {
			(void)m_nodes.remove(id);
			return {};
		}
		node->model = desc.model;
		node->position = desc.position;
		node->visible = desc.visible;
		return id;
	}

	/// Libera un nodo. El handle y cualquier copia de él dejan de ser válidos.
	/// \param id handle devuelto por `spawn`.
	/// \return `false` si el handle ya era inválido; `true` si se liberó.
	/// \post El slot puede reutilizarse con una generación nueva.
	bool remove(NodeId id) noexcept { return m_nodes.remove(id); }

	/// Acceso mutable al nodo (para leer o ajustar campos que no tienen setter).
	/// \param id handle del nodo.
	/// \return puntero al nodo, o `nullptr` si el handle no es válido. El puntero
	///         vive hasta `remove`/`clear` o la reutilización del slot.
	[[nodiscard]] Node3D* node(NodeId id) noexcept { return m_nodes.get(id); }
	/// Versión const de `node`.
	[[nodiscard]] const Node3D* node(NodeId id) const noexcept { return m_nodes.get(id); }

	/// Cambia la posición relativa a la cámara.
	/// \param id handle del nodo.
	/// \param position nueva posición en unidades del escalar de cámara.
	/// \return `false` si el handle no es válido.
	bool set_position(NodeId id, math3d::Vec3t<eng::coord> position) noexcept {
		Node3D* node = m_nodes.get(id);
		if (node == nullptr) {
			return false;
		}
		node->position = position;
		return true;
	}

	/// Cambia la visibilidad sin liberar el nodo.
	/// \param id handle del nodo.
	/// \param visible nuevo estado; `false` lo excluye del render.
	/// \return `false` si el handle no es válido.
	bool set_visible(NodeId id, bool visible) noexcept {
		Node3D* node = m_nodes.get(id);
		if (node == nullptr) {
			return false;
		}
		node->visible = visible;
		return true;
	}

	/// Vacía la escena y sube la generación de todos los slots: todos los handles
	/// anteriores quedan inválidos de forma permanente.
	void clear() noexcept { m_nodes.reset(); }

	/// \return número de nodos vivos.
	[[nodiscard]] u16 live_count() const noexcept { return static_cast<u16>(m_nodes.size()); }

	/// Cámara de la escena (acceso mutable para ajustes avanzados).
	/// \return referencia viva hasta que se destruya la escena.
	[[nodiscard]] Camera& camera() noexcept { return m_camera; }
	/// Versión const de `camera()`.
	[[nodiscard]] const Camera& camera() const noexcept { return m_camera; }

	/// Sustituye la cámara si sus parámetros son utilizables.
	/// \param camera cámara a copiar; `focal`, `near_z`, `half_w` y `half_h` deben
	///        ser estrictamente positivos (se comparan en el escalar `eng::coord`).
	/// \return `true` si se copió; `false` si la cámara es inválida (no se modifica
	///         la cámara actual).
	[[nodiscard]] bool set_camera(const Camera& camera) noexcept {
		using T = eng::math::scalar_traits<eng::coord>;
		if (!(camera.focal > T::zero()) || !(camera.near_z > T::zero()) ||
		    !(camera.half_w > T::zero()) || !(camera.half_h > T::zero())) {
			return false;
		}
		m_camera = camera;
		return true;
	}

	/// **Pinta la escena** sobre `target` con la cámara actual.
	///
	/// Ordena los nodos visibles de **lejos a cerca** por `position.z` (mayor z =
	/// más lejos) y delega cada malla en `eng::graphics::render_solid` con una
	/// traslación identidad-rotación. Es un painter sin z-buffer: los objetos que
	/// se intersecan pueden ordenarse mal (limitación documentada); los sólidos
	/// convexos y separados se pintan correctamente.
	///
	/// \param models tabla no propietaria de mallas; `Node3D::model.index` debe
	///        apuntar a una entrada. Debe sobrevivir a la llamada. Un nodo con
	///        modelo inválido o índice fuera de rango se omite.
	/// \param target superficie de destino; el llamador la limpia antes (contrato
	///        del engine). Cada cara se rellena por `Surface::fill_polygon`, que en
	///        Amiga usa el sink de Blitter del playfield si está instalado.
	/// \param color_of callable `u8(NodeId, u32 face_index)` que devuelve el índice
	///        de paleta de la cara. Se invoca una vez por cara emitida; el llamador
	///        decide el sombreado plano (G3 añadirá la tabla de materiales).
	/// \return número total de caras rellenadas; 0 si no hay nada visible o la
	///         tabla de modelos está vacía.
	/// \pre `target` es válido y el llamador ha limpiado el área de dibujo.
	/// \post Ningún nodo cambia de estado; no se reserva memoria.
	/// \note `MaxVertices` es por malla. Una malla con más vértices se omite entera.
	/// \note No reentrante ni thread-safe: usa el scratch y el transform de la escena.
	template <class ColorFn>
	u32 render(eng::Span<const Mesh> models, playfield::Surface& target, ColorFn&& color_of) noexcept {
		return render_with(
			[&](ModelHandle handle) -> const Mesh* {
				return (handle.valid() && handle.index < models.size())
					       ? &models[handle.index]
					       : nullptr;
			},
			target, color_of);
	}

	/// **Pinta la escena resolviendo las mallas contra una `ModelTable`** (G1): el
	/// juego registra los modelos cocinados una vez y `Node3D::model` es el handle
	/// devuelto por `ModelTable::add`. La tabla debe sobrevivir a la llamada.
	///
	/// \tparam MaxModels capacidad de modelos de la tabla (parámetro de `ModelTable`).
	/// \tparam TableVertices vértices máximos por modelo de la tabla.
	/// \tparam TableIndices índices máximos por modelo de la tabla.
	/// \tparam TableFaces caras máximas por modelo de la tabla.
	/// \tparam TableMaterials materiales máximos por modelo de la tabla.
	/// \tparam ColorFn callable `u8(NodeId, u32 face_index)`.
	/// \param models tabla de modelos no propietaria; debe sobrevivir a la llamada.
	/// \param target superficie de destino (el llamador la limpia antes).
	/// \param color_of color por cara; puede usar `models.color(node->model, face)`.
	/// \return número total de caras rellenadas; 0 si no hay nada visible.
	/// \note La tabla puede tener más capacidad que la escena; `Scene::MaxVertices`
	///       sigue siendo el tope efectivo por malla.
	template <u16 MaxModels, u16 TableVertices, u16 TableIndices, u16 TableFaces,
		  u16 TableMaterials, class ColorFn>
	u32 render(const ModelTable<MaxModels, TableVertices, TableIndices, TableFaces, TableMaterials>& models,
		   playfield::Surface& target, ColorFn&& color_of) noexcept {
		return render_with([&](ModelHandle handle) { return models.mesh(handle); }, target,
				   color_of);
	}

private:
	using ModelTransform = eng::math::Affine<3, eng::real, eng::coord>;

	/// Implementación común de los dos `render`: `mesh_at` resuelve un `ModelHandle`
	/// a una vista (o `nullptr`); el resto del pipeline es idéntico.
	/// \tparam MeshAt callable `const Mesh*(ModelHandle)`.
	/// \tparam ColorFn callable `u8(NodeId, u32)`.
	template <class MeshAt, class ColorFn>
	u32 render_with(MeshAt&& mesh_at, playfield::Surface& target, ColorFn&& color_of) noexcept {
		u16 count = 0u;
		m_nodes.for_each([&](Node3D& node, NodeId id) {
			if (!node.visible) {
				return;
			}
			const Mesh* mesh = mesh_at(node.model);
			if (mesh == nullptr || mesh->vertex_count() > MaxVertices) {
				return;
			}
			const s32 depth = eng::math::scalar_traits<eng::coord>::to_int(node.position.v[2]);
			u16 insert = count;
			while (insert > 0u && m_depth[insert - 1u] < depth) {
				m_depth[insert] = m_depth[insert - 1u];
				m_order[insert] = m_order[insert - 1u];
				--insert;
			}
			m_depth[insert] = depth;
			m_order[insert] = id;
			++count;
		});

		u32 faces = 0u;
		for (u16 i = 0u; i < count; ++i) {
			const NodeId id = m_order[i];
			Node3D* node = m_nodes.get(id);
			if (node == nullptr) {
				continue;
			}
			const Mesh* mesh = mesh_at(node->model);
			if (mesh == nullptr) {
				continue;
			}
			m_model.m = math::Mat<3, eng::real>::identity();
			m_model.t = node->position;
			faces += graphics::render_solid(*mesh, m_model, m_camera, target,
							[&](u32 face) { return color_of(id, face); },
							m_world, m_cam_scratch, m_xs, m_ys);
		}
		return faces;
	}

	util::Pool<Node3D, MaxNodes> m_nodes {}; ///< nodos vivos y generaciones.
	Camera m_camera {}; ///< cámara única de la escena (por defecto 320x200, focal 340).
	ModelTransform m_model {}; ///< transform temporal reutilizado por nodo.
	math3d::Vec3t<eng::coord> m_world[MaxVertices] {}; ///< scratch de transformación de un nodo.
	graphics::CamVertex<eng::coord> m_cam_scratch[16] {}; ///< scratch de clip de una cara.
	s16 m_xs[16] {}; ///< coordenadas X de pantalla de una cara recortada.
	s16 m_ys[16] {}; ///< coordenadas Y de pantalla de una cara recortada.
	NodeId m_order[MaxNodes] {}; ///< nodos visibles ordenados de lejos a cerca.
	s32 m_depth[MaxNodes] {}; ///< clave de profundidad de `m_order` (z de la posición).
};

} // namespace eng::scene3d
