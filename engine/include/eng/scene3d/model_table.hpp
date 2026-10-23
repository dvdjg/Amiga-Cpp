#pragma once

/// \file model_table.hpp
/// **Tabla de modelos 3D de capacidad fija** (G1 de `ROADMAP_3D_GAME_API.md`):
/// registra mallas cocinadas (`eng::assets::Model3DAssetView`) en buffers
/// propietarios y entrega `ModelHandle` para que `Scene::render` no vea blobs,
/// offsets ni punteros.
///
/// La tabla es **no movible** a propósito: cada `Mesh` apunta a los arrays de su
/// propio slot. Copiarla o moverla dejaría las vistas apuntando a la copia
/// anterior. Sin heap, sin RTTI y sin excepciones.
///
/// ```cpp
/// eng::scene3d::ModelTable<8, 64, 128, 24, 8> models;
/// const auto hero = models.add(eng::assets::Model3DAssetView {chunk}); // una vez
/// scene.render(models, screen, [&](auto id, u32 face) { return models.color(...); });
/// ```

#include <eng/assets/model3d.hpp>
#include <eng/core/data/mesh3d.hpp>
#include <eng/core/math/scalar.hpp>
#include <eng/core/types/span.hpp>
#include <eng/core/types/types.hpp>
#include <eng/core/util/noncopyable.hpp>
#include <eng/scene3d/types.hpp>

namespace eng::scene3d {

/// **Registro de mallas de capacidad fija**.
///
/// \tparam MaxModels número máximo de modelos; > 0 y ≤ 0xfffe (índice `u16`).
/// \tparam MaxVertices vértices máximos por modelo; ≥ 3.
/// \tparam MaxIndices índices máximos por modelo (suma de vértices de sus caras); ≥ 3.
/// \tparam MaxFaces caras máximas por modelo; ≥ 1.
/// \tparam MaxMaterials materiales máximos por modelo; ≥ 1.
///
/// El objeto contiene todo el almacenamiento: `MaxModels × (MaxVertices·6 +
/// MaxIndices·2 + MaxFaces·6 + MaxFaces·2 + MaxMaterials·4)` bytes
/// aproximadamente. Colocarlo en almacenamiento estático/global o en el arena del
/// juego, no en la pila del 68000.
template <u16 MaxModels, u16 MaxVertices, u16 MaxIndices, u16 MaxFaces, u16 MaxMaterials>
class ModelTable : public util::NonMovable {
	static_assert(MaxModels > 0u, "ModelTable: MaxModels debe ser > 0");
	static_assert(MaxModels <= 0xfffeu, "ModelTable: MaxModels debe caber en u16");
	static_assert(MaxVertices >= 3u, "ModelTable: MaxVertices debe ser >= 3");
	static_assert(MaxIndices >= 3u, "ModelTable: MaxIndices debe ser >= 3");
	static_assert(MaxFaces >= 1u, "ModelTable: MaxFaces debe ser >= 1");
	static_assert(MaxMaterials >= 1u, "ModelTable: MaxMaterials debe ser >= 1");

public:
	/// Registra un modelo cocinado copiando sus arrays a un slot libre.
	/// \param asset vista validada del chunk `Model3D`; debe sobrevivir a la llamada.
	/// \return handle del modelo, o inválido si el asset no es válido, no cabe en las
	///         capacidades de la tabla o la tabla está llena.
	/// \post Los datos quedan en buffers propios; el asset puede liberarse después.
	[[nodiscard]] ModelHandle add(const assets::Model3DAssetView& asset) noexcept {
		if (!asset.valid() || m_count >= MaxModels) {
			return {};
		}
		const u16 nv = asset.vertex_count();
		const u16 ni = asset.index_count();
		const u16 nf = asset.face_count();
		const u16 nm = asset.material_count();
		if (nv > MaxVertices || ni > MaxIndices || nf > MaxFaces || nm > MaxMaterials) {
			return {};
		}
		Slot& slot = m_slots[m_count];
		for (u16 i = 0u; i < nv; ++i) {
			slot.verts[i] = asset.vertex(i);
		}
		for (u16 i = 0u; i < ni; ++i) {
			slot.indices[i] = asset.index(i);
		}
		for (u16 i = 0u; i < nf; ++i) {
			const assets::ModelFace face = asset.face(i);
			slot.faces[i] = math3d::FaceSpan {face.first, face.count};
			slot.face_material[i] = face.material;
		}
		for (u16 i = 0u; i < nm; ++i) {
			slot.materials[i] = asset.material(i);
		}
		slot.bounds = asset.bounds();
		slot.view = Mesh {
			eng::Span<const math3d::Vec3t<eng::coord>>(slot.verts, nv),
			eng::Span<const u16>(slot.indices, ni),
			eng::Span<const math3d::FaceSpan>(slot.faces, nf),
			{},
		};
		slot.nv = nv;
		slot.ni = ni;
		slot.nf = nf;
		slot.nm = nm;
		++m_count;
		return ModelHandle {static_cast<u16>(m_count - 1u)};
	}

	/// Vista de la malla del modelo, o `nullptr` si el handle no está registrado.
	[[nodiscard]] const Mesh* mesh(ModelHandle handle) const noexcept {
		return handle.index < m_count ? &m_slots[handle.index].view : nullptr;
	}

	/// Material de la cara `face` del modelo, o `{}` si el handle/cara no existen.
	[[nodiscard]] assets::ModelMaterial material(ModelHandle handle, u32 face) const noexcept {
		const Slot* slot = slot_of(handle);
		if (slot == nullptr || face >= slot->nf) return {};
		const u16 mi = slot->face_material[face];
		return mi < slot->nm ? slot->materials[mi] : assets::ModelMaterial {};
	}

	/// Índice de material de la cara `face`, o `0xffff` si no existe.
	[[nodiscard]] u16 material_index(ModelHandle handle, u32 face) const noexcept {
		const Slot* slot = slot_of(handle);
		return (slot != nullptr && face < slot->nf) ? slot->face_material[face] : 0xffffu;
	}

	/// Índice de paleta de la cara `face` (atajo para el callback de color).
	[[nodiscard]] u8 color(ModelHandle handle, u32 face) const noexcept {
		return material(handle, face).color;
	}

	/// Id de patrón de la cara `face` (0 = sólido; ver G3/P7).
	[[nodiscard]] u8 pattern(ModelHandle handle, u32 face) const noexcept {
		return material(handle, face).pattern;
	}

	/// Límites del modelo, o `{}` si el handle no existe.
	[[nodiscard]] assets::ModelBounds bounds(ModelHandle handle) const noexcept {
		const Slot* slot = slot_of(handle);
		return slot != nullptr ? slot->bounds : assets::ModelBounds {};
	}

	/// \return número de modelos registrados.
	[[nodiscard]] u16 size() const noexcept { return m_count; }
	/// \return capacidad de modelos de esta instancia.
	[[nodiscard]] static constexpr u16 capacity() noexcept { return MaxModels; }

	/// Invalida todos los handles (los datos permanecen en los slots).
	void clear() noexcept { m_count = 0u; }

private:
	struct Slot {
		math3d::Vec3t<eng::coord> verts[MaxVertices] {};
		u16 indices[MaxIndices] {};
		math3d::FaceSpan faces[MaxFaces] {};
		u16 face_material[MaxFaces] {};
		assets::ModelMaterial materials[MaxMaterials] {};
		assets::ModelBounds bounds {};
		Mesh view {};
		u16 nv = 0;
		u16 ni = 0;
		u16 nf = 0;
		u16 nm = 0;
	};

	[[nodiscard]] const Slot* slot_of(ModelHandle handle) const noexcept {
		return handle.index < m_count ? &m_slots[handle.index] : nullptr;
	}

	Slot m_slots[MaxModels] {};
	u16 m_count = 0u;
};

} // namespace eng::scene3d
