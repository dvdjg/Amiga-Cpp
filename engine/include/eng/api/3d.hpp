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
#include <eng/core/types/ptr.hpp>
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

/// Nodo vivo de la escena. `position`/`rotation` son el **transform local** en el
/// escalar de cámara (`eng::coord` para traslación, Q4.12 para la rotación). La
/// cámara ocupa el origen y mira hacia `+z`; un nodo sin padre usa su transform
/// local como world. `world_position`/`world_rotation` son la caché resuelta por
/// `Scene::update_transforms` (solo lectura para el juego).
struct Node3D {
	ModelHandle model {}; ///< malla que se pinta; índice en la tabla de `render`.
	math3d::Vec3t<eng::coord> position {}; ///< traslación local (unidades de `eng::coord`).
	math::Mat<3, eng::real> rotation = math::Mat<3, eng::real>::identity(); ///< rotación local (Q4.12).
	NodeId parent {}; ///< padre en la jerarquía; inválido = raíz.
	bool visible = true; ///< si es `false`, el nodo no entra en el render.
	math3d::Vec3t<eng::coord> world_position {}; ///< traslación world resuelta (caché).
	math::Mat<3, eng::real> world_rotation = math::Mat<3, eng::real>::identity(); ///< rotación world resuelta (caché).
	bool dirty = true; ///< transform local o de un ancestro pendiente de recomponer.
	bool world_valid = false; ///< `world_position`/`world_rotation` son válidos.
};

/// Descripción de alta de un nodo (`Scene::spawn`). Los campos se inicializan a
/// valores seguros: modelo inválido, origen, rotación identidad, raíz y visible.
struct NodeDesc {
	ModelHandle model {}; ///< malla a pintar.
	math3d::Vec3t<eng::coord> position {}; ///< traslación local.
	math::Mat<3, eng::real> rotation = math::Mat<3, eng::real>::identity(); ///< rotación local (Q4.12).
	NodeId parent {}; ///< padre opcional; inválido = raíz.
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
/// \tparam MaxEdges aristas únicas máximas por malla para `render_wire`; por defecto
///         64 (cubre la icosidodecaedro de 60 aristas). Si una malla tiene más, se
///         dibujan las primeras `MaxEdges` (no se desborda).
///
/// La escena no reserva memoria dinámica: el objeto contiene el pool y todo el
/// scratch. `render` no asigna, no falla y no lanza; los nodos inválidos, las
/// mallas fuera de rango y los modelos demasiado grandes se omiten. Por su
/// tamaño (pool + scratch), la escena se coloca en almacenamiento estático/global
/// o en el arena del juego, **no** en la pila por defecto del 68000.
template <u16 MaxNodes, u16 MaxVertices, u16 MaxEdges = 64u>
class Scene : public util::Noncopyable {
	static_assert(MaxNodes > 0u, "Scene: MaxNodes debe ser mayor que 0");
	static_assert(MaxNodes <= 0xfffeu, "Scene: MaxNodes debe caber en un indice u16");
	static_assert(MaxVertices >= 3u, "Scene: MaxVertices debe ser al menos 3");
	static_assert(MaxEdges > 0u, "Scene: MaxEdges debe ser mayor que 0");

public:
	/// Handle generacional de un nodo. Se invalida al liberar el nodo o al llamar
	/// `clear()`; `valid() == false` es el fallo de `spawn` con la escena llena.
	using NodeId = eng::scene3d::NodeId;
	/// Handle interno del pool (misma disposición que `NodeId`).
	using PoolHandle = typename util::Pool<Node3D, MaxNodes>::Handle;

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
		const PoolHandle handle = m_nodes.add();
		if (!handle.valid()) {
			return {};
		}
		eng::Ref<Node3D> node = m_nodes.get(handle);
		if (!node.valid()) {
			(void)m_nodes.remove(handle);
			return {};
		}
		node->model = desc.model;
		node->position = desc.position;
		node->rotation = desc.rotation;
		node->parent = desc.parent;
		node->visible = desc.visible;
		node->dirty = true;
		node->world_valid = false;
		return to_node_id(handle);
	}

	/// Libera un nodo. El handle y cualquier copia de él dejan de ser válidos.
	/// \param id handle devuelto por `spawn`.
	/// \return `false` si el handle ya era inválido; `true` si se liberó.
	/// \post El slot puede reutilizarse con una generación nueva.
	bool remove(NodeId id) noexcept {
		if (!m_nodes.valid(to_pool_handle(id))) {
			return false;
		}
		m_nodes.for_each([&](Node3D& child, PoolHandle child_handle) {
			const NodeId child_id = to_node_id(child_handle);
			if (child_id != id && child.parent == id) {
				child.parent = {};
				mark_dirty(child_id);
			}
		});
		return m_nodes.remove(to_pool_handle(id));
	}

	/// Acceso mutable al nodo (para leer o ajustar campos que no tienen setter).
	/// \param id handle del nodo.
	/// \return puntero al nodo, o `nullptr` si el handle no es válido. El puntero
	///         vive hasta `remove`/`clear` o la reutilización del slot.
	[[nodiscard]] eng::Ref<Node3D> node(NodeId id) noexcept { return m_nodes.get(to_pool_handle(id)); }
	/// Versión const de `node`.
	[[nodiscard]] eng::Ref<const Node3D> node(NodeId id) const noexcept {
		return m_nodes.get(to_pool_handle(id));
	}

	/// Cambia la posición relativa a la cámara.
	/// \param id handle del nodo.
	/// \param position nueva posición en unidades del escalar de cámara.
	/// \return `false` si el handle no es válido.
	bool set_position(NodeId id, math3d::Vec3t<eng::coord> position) noexcept {
		eng::Ref<Node3D> node = m_nodes.get(to_pool_handle(id));
		if (!node.valid()) {
			return false;
		}
		node->position = position;
		mark_dirty(id);
		return true;
	}

	/// Cambia la rotación local (Q4.12). La matriz debe ser ortogonal; el llamador
	/// la construye con sus ángulos (`math3d::load_rotate` o equivalente).
	/// \param id handle del nodo.
	/// \param rotation matriz de rotación local.
	/// \return `false` si el handle no es válido.
	bool set_rotation(NodeId id, const math::Mat<3, eng::real>& rotation) noexcept {
		eng::Ref<Node3D> node = m_nodes.get(to_pool_handle(id));
		if (!node.valid()) {
			return false;
		}
		node->rotation = rotation;
		mark_dirty(id);
		return true;
	}

	/// Liga `child` a `parent` (o lo convierte en raíz con un `parent` inválido).
	/// Rechaza ciclos y handles inválidos.
	/// \param child nodo que cambia de padre.
	/// \param parent nuevo padre, o inválido para dejarlo como raíz.
	/// \return `false` si algún handle no es válido, si `child == parent` o si
	///         `parent` es descendiente de `child` (crearía un ciclo).
	bool set_parent(NodeId child, NodeId parent) noexcept {
		eng::Ref<Node3D> node = m_nodes.get(to_pool_handle(child));
		if (!node.valid()) {
			return false;
		}
		if (parent.valid()) {
			if (parent == child || !m_nodes.valid(to_pool_handle(parent))) {
				return false;
			}
			NodeId walk = parent;
			for (u16 depth = 0u; depth < MaxNodes; ++depth) {
				if (walk == child) {
					return false;
				}
				eng::Ref<const Node3D> ancestor = m_nodes.get(to_pool_handle(walk));
				if (!ancestor.valid() || !ancestor->parent.valid()) {
					break;
				}
				walk = ancestor->parent;
			}
		}
		node->parent = parent;
		mark_dirty(child);
		return true;
	}

	/// Cambia la visibilidad sin liberar el nodo.
	/// \param id handle del nodo.
	/// \param visible nuevo estado; `false` lo excluye del render.
	/// \return `false` si el handle no es válido.
	bool set_visible(NodeId id, bool visible) noexcept {
		eng::Ref<Node3D> node = m_nodes.get(to_pool_handle(id));
		if (!node.valid()) {
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
	/// Resuelve la jerarquía de transforms (solo nodos sucios), ordena los visibles de
	/// **lejos a cerca** por `world_position.z` (mayor z = más lejos) y delega cada malla
	/// en `eng::graphics::render_solid_styled` con su transform world. Es un painter sin
	/// z-buffer: los objetos que se intersecan pueden ordenarse mal (limitación
	/// documentada); los sólidos convexos y separados se pintan correctamente.
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
	/// \tparam ColorFn callable `u8(NodeId, u32 face_index)` para el color de cara.
	/// \note No reentrante ni thread-safe: usa el scratch y el transform de la escena.
	template <class ColorFn>
	u32 render(eng::Span<const Mesh> models, playfield::Surface& target, ColorFn&& color_of) noexcept {
		return render_with(
			[&](ModelHandle handle) -> eng::Ref<const Mesh> {
				return (handle.valid() && handle.index < models.size())
					       ? &models[handle.index]
					       : nullptr;
			},
			target,
			[&](NodeId id, u32 face) -> graphics::FaceStyle {
				return graphics::FaceStyle {static_cast<u8>(color_of(id, face)), 0u, 0u};
			});
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
		return render_with(
			[&](ModelHandle handle) { return models.mesh(handle); }, target,
			[&](NodeId id, u32 face) -> graphics::FaceStyle {
				return graphics::FaceStyle {static_cast<u8>(color_of(id, face)), 0u, 0u};
			});
	}

	/// **Pinta la escena con los materiales cocinados** de la `ModelTable`: usa
	/// `ModelTable::color` y `ModelTable::pattern` de la cara, sin callback. Es la
	/// forma normal de un juego G1/G3: registrar modelos y llamar `render(models, screen)`.
	/// \tparam MaxModels capacidad de modelos de la tabla.
	/// \tparam TableVertices vértices máximos por modelo de la tabla.
	/// \tparam TableIndices índices máximos por modelo de la tabla.
	/// \tparam TableFaces caras máximas por modelo de la tabla.
	/// \tparam TableMaterials materiales máximos por modelo de la tabla.
	/// \param models tabla de modelos no propietaria; debe sobrevivir a la llamada.
	/// \param target superficie de destino (el llamador la limpia antes).
	/// \return número total de caras rellenadas; 0 si no hay nada visible.
	template <u16 MaxModels, u16 TableVertices, u16 TableIndices, u16 TableFaces,
		  u16 TableMaterials>
	u32 render(const ModelTable<MaxModels, TableVertices, TableIndices, TableFaces, TableMaterials>& models,
		   playfield::Surface& target) noexcept {
		return render_with(
			[&](ModelHandle handle) { return models.mesh(handle); }, target,
			[&](NodeId id, u32 face) -> graphics::FaceStyle {
				const eng::Ref<Node3D> node = this->node(id);
				const ModelHandle handle = node.valid() ? node->model : ModelHandle {};
				return graphics::FaceStyle {models.color(handle, face),
							    models.pattern(handle, face), 0u};
			});
	}

	/// **Pinta la escena por silueta convexa** (P8): un solo fill por objeto y color
	/// plano. Para modelos con caras de colores distintos, el llamador debe usar
	/// `render`; esta ruta usa `color_of(id, 0)`. Si un objeto cruza el near, cae
	/// automaticamente a la ruta por cara.
	template <class ColorFn>
	u32 render_silhouette(eng::Span<const Mesh> models, playfield::Surface& target,
			      ColorFn&& color_of) noexcept {
		return render_silhouette_with(
			[&](ModelHandle handle) -> eng::Ref<const Mesh> {
				return (handle.valid() && handle.index < models.size())
					       ? &models[handle.index]
					       : nullptr;
			},
			target,
			[&](NodeId id) -> u8 { return static_cast<u8>(color_of(id, 0u)); });
	}

	/// Variante de silueta contra una `ModelTable` y el color del material de la
	/// cara 0 de cada modelo.
	template <u16 MaxModels, u16 TableVertices, u16 TableIndices, u16 TableFaces,
		  u16 TableMaterials>
	u32 render_silhouette(
		const ModelTable<MaxModels, TableVertices, TableIndices, TableFaces, TableMaterials>& models,
		playfield::Surface& target) noexcept {
		return render_silhouette_with(
			[&](ModelHandle handle) { return models.mesh(handle); }, target,
			[&](NodeId id) -> u8 {
				const eng::Ref<Node3D> node = this->node(id);
				return node.valid() ? models.color(node->model, 0u) : 0u;
			});
	}

	/// **Pinta la escena en alambre** con la `ModelTable`: resuelve la jerarquía,
	/// proyecta cada arista única y la dibuja con `color` sobre `target`. Es la ruta
	/// barata para depurar escena/rotación sin depender del relleno de caras.
	/// \param models tabla de modelos no propietaria; debe sobrevivir a la llamada.
	/// \param target superficie de destino (el llamador la limpia antes).
	/// \param color índice de paleta de todas las líneas.
	/// \param plan `FramePlan` opcional para que un `BlitterRaster` encole líneas en
	///        vez de rasterizarlas por CPU; vacío = ruta CPU.
	/// \return número de aristas dibujadas (suma de nodos).
	/// \tparam MaxModels capacidad de modelos de la tabla.
	/// \tparam TableVertices vértices máximos por modelo.
	/// \tparam TableIndices índices máximos por modelo.
	/// \tparam TableFaces caras máximas por modelo.
	/// \tparam TableMaterials materiales máximos por modelo.
	/// \note `MaxEdges` es por malla; las aristas sobrantes se omiten.
	template <u16 MaxModels, u16 TableVertices, u16 TableIndices, u16 TableFaces,
		  u16 TableMaterials>
	u32 render_wire(
		const ModelTable<MaxModels, TableVertices, TableIndices, TableFaces, TableMaterials>& models,
		playfield::Surface& target, u8 color,
		eng::Ref<graphics::FramePlan> plan = {},
		playfield::RasterOp op = playfield::RasterOp::Copy) noexcept {
		return render_wire_with([&](ModelHandle handle) { return models.mesh(handle); }, target,
					color, plan, op);
	}

	/// Igual que el anterior, resolviendo contra una tabla de vistas `Mesh`.
	/// \param models tabla no propietaria de mallas; `Node3D::model.index` la indexa.
	/// \param target superficie de destino.
	/// \param color índice de paleta de las líneas.
	/// \return número de aristas dibujadas.
	u32 render_wire(eng::Span<const Mesh> models, playfield::Surface& target, u8 color,
			eng::Ref<graphics::FramePlan> plan = {},
			playfield::RasterOp op = playfield::RasterOp::Copy) noexcept {
		return render_wire_with(
			[&](ModelHandle handle) -> eng::Ref<const Mesh> {
				return (handle.valid() && handle.index < models.size())
					       ? &models[handle.index]
					       : nullptr;
			},
			target, color, plan, op);
	}

private:
	using ModelTransform = eng::math::Affine<3, eng::real, eng::coord>;

	/// Implementación común de `render_wire`: `mesh_at` resuelve el handle y se
	/// reutiliza el scratch de la escena por nodo (sin orden de painter: el alambre
	/// no tapa).
	/// \tparam MeshAt callable `eng::Ref<const Mesh>(ModelHandle)`.
	template <class MeshAt>
	u32 render_wire_with(MeshAt&& mesh_at, playfield::Surface& target, u8 color,
			      eng::Ref<graphics::FramePlan> plan,
			      playfield::RasterOp op) noexcept {
		update_transforms();
		u32 edges = 0u;
		m_nodes.for_each([&](Node3D& node, PoolHandle handle) {
			(void)handle;
			if (!node.visible) {
				return;
			}
			eng::Ref<const Mesh> mesh = mesh_at(node.model);
			if (!mesh.valid() || mesh->vertex_count() > MaxVertices) {
				return;
			}
			m_model.m = node.world_rotation;
			m_model.t = node.world_position;
			edges += graphics::render_wire(*mesh, m_model, m_camera, target, color,
						       m_world, m_edges, MaxEdges, m_cam_scratch,
						       m_xs, m_ys, plan, op);
		});
		return edges;
	}

	/// Convierte el handle del `Pool` al tipo público `NodeId` (misma disposición).
	[[nodiscard]] static constexpr NodeId to_node_id(PoolHandle handle) noexcept {
		return NodeId {handle.index, handle.generation};
	}
	/// Convierte el `NodeId` público al handle del `Pool`.
	[[nodiscard]] static constexpr PoolHandle to_pool_handle(NodeId id) noexcept {
		return PoolHandle {id.index, id.generation};
	}

	/// Marca `id` y todos sus descendientes como pendientes de recomponer.
	/// \param id nodo changed.
	/// \param depth guarda contra ciclos si el juego muta `Node3D::parent` a mano
	///        (la API `set_parent` los rechaza).
	void mark_dirty(NodeId id, u16 depth = 0u) noexcept {
		if (depth > MaxNodes) {
			return;
		}
		eng::Ref<Node3D> node = m_nodes.get(to_pool_handle(id));
		if (!node.valid()) {
			return;
		}
		node->dirty = true;
		node->world_valid = false;
		m_nodes.for_each([&](Node3D& child, PoolHandle handle) {
			const NodeId child_id = to_node_id(handle);
			if (child_id != id && child.parent == id) {
				mark_dirty(child_id, static_cast<u16>(depth + 1u));
			}
		});
	}

	/// Recompone el world de un nodo (primero el padre) y lo deja en caché.
	void update_node(NodeId id, u16 depth) noexcept {
		if (depth > MaxNodes) {
			return;
		}
		eng::Ref<Node3D> node = m_nodes.get(to_pool_handle(id));
		if (!node.valid() || (node->world_valid && !node->dirty)) {
			return;
		}
		const ModelTransform local {node->rotation, node->position};
		bool parent_ok = false;
		if (node->parent.valid() && node->parent != id) {
			update_node(node->parent, static_cast<u16>(depth + 1u));
			eng::Ref<const Node3D> parent = m_nodes.get(to_pool_handle(node->parent));
			if (parent.valid() && parent->world_valid) {
				const ModelTransform parent_world {parent->world_rotation,
							   parent->world_position};
				const ModelTransform world = eng::math::compose(parent_world, local);
				node->world_rotation = world.m;
				node->world_position = world.t;
				parent_ok = true;
			}
		}
		if (!parent_ok) {
			node->world_rotation = node->rotation;
			node->world_position = node->position;
		}
		node->dirty = false;
		node->world_valid = true;
	}

	/// Resuelve los transforms de todos los nodos vivos antes de extraer el render.
	void update_transforms() noexcept {
		m_nodes.for_each([&](Node3D&, PoolHandle handle) {
			update_node(to_node_id(handle), 0u);
		});
	}

	/// Implementación común de los `render`: `mesh_at` resuelve un `ModelHandle`
	/// a una vista (o `nullptr`); `style_of` da el estilo de cada cara; el resto del
	/// pipeline es idéntico.
	/// \tparam MeshAt callable `eng::Ref<const Mesh>(ModelHandle)`.
	/// \tparam StyleFn callable `graphics::FaceStyle(NodeId, u32)`.
	template <class MeshAt, class StyleFn>
	u32 render_with(MeshAt&& mesh_at, playfield::Surface& target, StyleFn&& style_of) noexcept {
		update_transforms();
		u16 count = 0u;
		m_nodes.for_each([&](Node3D& node, PoolHandle handle) {
			if (!node.visible) {
				return;
			}
			eng::Ref<const Mesh> mesh = mesh_at(node.model);
			if (!mesh.valid() || mesh->vertex_count() > MaxVertices) {
				return;
			}
			const s32 depth =
				eng::math::scalar_traits<eng::coord>::to_int(node.world_position.v[2]);
			u16 insert = count;
			while (insert > 0u && m_depth[insert - 1u] < depth) {
				m_depth[insert] = m_depth[insert - 1u];
				m_order[insert] = m_order[insert - 1u];
				--insert;
			}
			m_depth[insert] = depth;
			m_order[insert] = to_node_id(handle);
			++count;
		});

		u32 faces = 0u;
		for (u16 i = 0u; i < count; ++i) {
			const NodeId id = m_order[i];
			eng::Ref<Node3D> node = m_nodes.get(to_pool_handle(id));
			if (!node.valid()) {
				continue;
			}
			eng::Ref<const Mesh> mesh = mesh_at(node->model);
			if (!mesh.valid()) {
				continue;
			}
			m_model.m = node->world_rotation;
			m_model.t = node->world_position;
			faces += graphics::render_solid_styled(
				*mesh, m_model, m_camera, target,
				[&](u32 face) { return style_of(id, face); }, m_world,
				m_cam_scratch, m_xs, m_ys);
		}
		return faces;
	}

	/// Implementacion comun de las rutas de silueta: mismo orden painter que
	/// `render_with`; si un modelo no cabe en el hull (near o >16 vertices), cae a
	/// `render_solid_styled` para no perder la cara.
	template <class MeshAt, class ColorAt>
	u32 render_silhouette_with(MeshAt&& mesh_at, playfield::Surface& target,
				   ColorAt&& color_at) noexcept {
		update_transforms();
		u16 count = 0u;
		m_nodes.for_each([&](Node3D& node, PoolHandle handle) {
			if (!node.visible) {
				return;
			}
			eng::Ref<const Mesh> mesh = mesh_at(node.model);
			if (!mesh.valid() || mesh->vertex_count() > MaxVertices) {
				return;
			}
			const s32 depth =
				eng::math::scalar_traits<eng::coord>::to_int(node.world_position.v[2]);
			u16 insert = count;
			while (insert > 0u && m_depth[insert - 1u] < depth) {
				m_depth[insert] = m_depth[insert - 1u];
				m_order[insert] = m_order[insert - 1u];
				--insert;
			}
			m_depth[insert] = depth;
			m_order[insert] = to_node_id(handle);
			++count;
		});

		u32 drawn = 0u;
		for (u16 i = 0u; i < count; ++i) {
			const NodeId id = m_order[i];
			eng::Ref<Node3D> node = m_nodes.get(to_pool_handle(id));
			if (!node.valid()) {
				continue;
			}
			eng::Ref<const Mesh> mesh = mesh_at(node->model);
			if (!mesh.valid()) {
				continue;
			}
			m_model.m = node->world_rotation;
			m_model.t = node->world_position;
			const u8 color = color_at(id);
			const u32 one = graphics::render_convex_silhouette(
				*mesh, m_model, m_camera, target, color, m_world, m_cam_scratch,
				m_xs, m_ys, m_hull, 16u);
			if (one != 0u) {
				drawn += one;
				continue;
			}
			drawn += graphics::render_solid_styled(
				*mesh, m_model, m_camera, target,
				[&](u32) { return graphics::FaceStyle {color, 0u, 0u}; }, m_world,
				m_cam_scratch, m_xs, m_ys);
		}
		return drawn;
	}
	util::Pool<Node3D, MaxNodes> m_nodes {}; ///< nodos vivos y generaciones.
	Camera m_camera {}; ///< cámara única de la escena (por defecto 320x200, focal 340).
	ModelTransform m_model {}; ///< transform temporal reutilizado por nodo.
	math3d::Vec3t<eng::coord> m_world[MaxVertices] {}; ///< scratch de transformación de un nodo.
	graphics::CamVertex<eng::coord> m_cam_scratch[16] {}; ///< scratch de clip de una cara.
	s16 m_xs[16] {}; ///< coordenadas X de pantalla de una cara recortada.
	s16 m_ys[16] {}; ///< coordenadas Y de pantalla de una cara recortada.
	u16 m_hull[16] {}; ///< scratch de la envolvente convexa (P8).
	NodeId m_order[MaxNodes] {}; ///< nodos visibles ordenados de lejos a cerca.
	s32 m_depth[MaxNodes] {}; ///< clave de profundidad de `m_order` (z de la posición).
	graphics::WireEdge m_edges[MaxEdges] {}; ///< scratch de aristas únicas de `render_wire`.
};

} // namespace eng::scene3d
