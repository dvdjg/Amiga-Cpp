#pragma once

/// \file model3d.hpp
/// **Vista validada del chunk `Model3D` (15)** de UAF-R (G1 de
/// `ROADMAP_3D_GAME_API.md`): la forma mínima cocinada de un modelo estático
/// n-gon con materiales por cara.
///
/// El chunk es **big-endian** y se valida entero antes de publicar la vista
/// (índices, rangos de cara, materiales y offsets). El runtime no parsea JSON ni
/// copia geometría por frame: la `ModelTable` de la escena copia los arrays una
/// vez a buffers de capacidad fija y construye una `math3d::PolyMeshViewT`.
///
/// ```text
/// Model3D payload (v1, 28 B de cabecera)
///   u16 version            1
///   u16 flags              reservado (0)
///   u16 vertex_count
///   u16 face_count
///   u16 index_count
///   u16 material_count
///   u32 vertex_offset      relativo al inicio del payload
///   u32 index_offset
///   u32 face_offset
///   u32 material_offset
///   vertex[]   { s16 x, s16 y, s16 z }
///   index[]    { u16 }
///   face[]     { u16 first, u16 count, u16 material }
///   material[] { u8 color, u8 pattern, u16 flags }
/// ```
///
/// `first/count` indexan `index[]`; `material` indexa `material[]`. Una cara debe
/// ser convexa y de 3 a 8 vértices (contrato de `render3d`); el cooker valida la
/// convexidad y el winding, el runtime solo valida los rangos.
///
/// No posee memoria: apunta al payload del blob, que debe sobrevivir a la vista.

#include <eng/assets/uaf.hpp>
#include <eng/core/data/mesh3d.hpp>
#include <eng/core/types/types.hpp>

namespace eng::assets {

/// Material plano de una cara: índice de paleta + patrón + flags.
struct ModelMaterial {
	u8 color = 0; ///< índice de paleta (0..255).
	u8 pattern = 0; ///< id de patrón (`FillPattern`/asset); 0 = sólido.
	u16 flags = 0; ///< reservado: `opaque`/`doubleSided`/`noCull` (G3).
};

/// Cara n-gon de un `Model3D`: rango en `index[]` + material.
struct ModelFace {
	u16 first = 0; ///< primer índice de la cara en `index[]`.
	u16 count = 0; ///< número de vértices (3..8 en el contrato del renderer).
	u16 material = 0; ///< índice en `material[]`.
};

/// Límites axis-aligned del modelo (en unidades de `eng::coord`).
struct ModelBounds {
	math3d::Vec3 min {}; ///< esquina mínima (x, y, z).
	math3d::Vec3 max {}; ///< esquina máxima (x, y, z).
};

/// Vista de solo lectura del chunk `Model3D` (15).
class Model3DAssetView {
public:
	static constexpr u32 kHeaderSize = 28u; ///< bytes de la cabecera v1.
	static constexpr u16 kVersion = 1u; ///< versión del schema que entiende esta vista.
	static constexpr u16 kMaxFaceVertices = 8u; ///< tope de vértices por cara de `render3d`.

	constexpr Model3DAssetView() = default;
	explicit constexpr Model3DAssetView(eng::UafPayload bytes) : m_bytes(bytes.raw()) {}

	/// \return `true` si el payload tiene cabecera v1, offsets dentro del chunk,
	///         caras 3..8, índices y materiales dentro de rango. Coste O(índices +
	///         caras) en la carga, no por frame.
	[[nodiscard]] bool valid() const {
		if (m_bytes.size() < kHeaderSize) return false;
		if (read_be16(m_bytes.data()) != kVersion) return false;
		const u32 nv = vertex_count();
		const u32 nf = face_count();
		const u32 ni = index_count();
		const u32 nm = material_count();
		if (nv == 0u || nf == 0u || ni < 3u || nm == 0u) return false;
		const u64 v_off = vertex_offset();
		const u64 i_off = index_offset();
		const u64 f_off = face_offset();
		const u64 m_off = material_offset();
		const u64 size = m_bytes.size();
		if (v_off < kHeaderSize || v_off + static_cast<u64>(nv) * 6u > size) return false;
		if (i_off < v_off + static_cast<u64>(nv) * 6u ||
		    i_off + static_cast<u64>(ni) * 2u > size) {
			return false;
		}
		if (f_off < i_off + static_cast<u64>(ni) * 2u ||
		    f_off + static_cast<u64>(nf) * 6u > size) {
			return false;
		}
		if (m_off < f_off + static_cast<u64>(nf) * 6u ||
		    m_off + static_cast<u64>(nm) * 4u > size) {
			return false;
		}
		for (u32 i = 0u; i < ni; ++i) {
			if (index(i) >= nv) return false;
		}
		for (u32 f = 0u; f < nf; ++f) {
			const ModelFace face = this->face(f);
			if (face.count < 3u || face.count > kMaxFaceVertices) return false;
			if (static_cast<u32>(face.first) + face.count > ni) return false;
			if (face.material >= nm) return false;
		}
		return true;
	}

	[[nodiscard]] bool header_ok() const { return m_bytes.size() >= kHeaderSize; }
	[[nodiscard]] u16 version() const { return header_ok() ? read_be16(m_bytes.data()) : 0u; }
	/// \return flags de formato declaradas por la cabecera.
	[[nodiscard]] u16 flags() const {
		return header_ok() ? read_be16(m_bytes.data() + 2u) : 0u;
	}
	/// \return numero de vertices declarado por la cabecera.
	[[nodiscard]] u16 vertex_count() const {
		return header_ok() ? read_be16(m_bytes.data() + 4u) : 0u;
	}
	[[nodiscard]] u16 face_count() const {
		return header_ok() ? read_be16(m_bytes.data() + 6u) : 0u;
	}
	/// \return numero de indices declarado por la cabecera.
	[[nodiscard]] u16 index_count() const {
		return header_ok() ? read_be16(m_bytes.data() + 8u) : 0u;
	}
	/// \return numero de materiales declarado por la cabecera.
	[[nodiscard]] u16 material_count() const {
		return header_ok() ? read_be16(m_bytes.data() + 10u) : 0u;
	}
	/// \return offset del bloque de vertices dentro del blob.
	[[nodiscard]] u32 vertex_offset() const {
		return header_ok() ? read_be32(m_bytes.data() + 12u) : 0u;
	}
	/// \return offset del bloque de indices dentro del blob.
	[[nodiscard]] u32 index_offset() const {
		return header_ok() ? read_be32(m_bytes.data() + 16u) : 0u;
	}
	[[nodiscard]] u32 face_offset() const {
		return header_ok() ? read_be32(m_bytes.data() + 20u) : 0u;
	}
	/// \return offset del bloque de materiales dentro del blob.
	[[nodiscard]] u32 material_offset() const {
		return header_ok() ? read_be32(m_bytes.data() + 24u) : 0u;
	}

	/// Vértice `i` en el escalar de cámara (`eng::coord`), o `{0,0,0}` fuera de rango.
	[[nodiscard]] math3d::Vec3 vertex(u32 i) const {
		if (i >= vertex_count()) return {};
		const u64 off = vertex_offset() + static_cast<u64>(i) * 6u;
		if (!range_ok(off, 6u)) return {};
		return math3d::vec3(static_cast<s16>(read_be16(m_bytes.data() + off)),
				    static_cast<s16>(read_be16(m_bytes.data() + off + 2u)),
				    static_cast<s16>(read_be16(m_bytes.data() + off + 4u)));
	}

	/// Índice `i` de `index[]`, o 0 fuera de rango.
	[[nodiscard]] u16 index(u32 i) const {
		if (i >= index_count()) return 0u;
		const u64 off = index_offset() + static_cast<u64>(i) * 2u;
		return range_ok(off, 2u) ? read_be16(m_bytes.data() + off) : 0u;
	}

	/// Cara `i`, o `{}` fuera de rango.
	[[nodiscard]] ModelFace face(u32 i) const {
		if (i >= face_count()) return {};
		const u64 off = face_offset() + static_cast<u64>(i) * 6u;
		if (!range_ok(off, 6u)) return {};
		return ModelFace {read_be16(m_bytes.data() + off),
				  read_be16(m_bytes.data() + off + 2u),
				  read_be16(m_bytes.data() + off + 4u)};
	}

	/// Material `i`, o `{}` fuera de rango.
	[[nodiscard]] ModelMaterial material(u32 i) const {
		if (i >= material_count()) return {};
		const u64 off = material_offset() + static_cast<u64>(i) * 4u;
		if (!range_ok(off, 4u)) return {};
		return ModelMaterial {m_bytes.data()[off], m_bytes.data()[off + 1u],
				      read_be16(m_bytes.data() + off + 2u)};
	}

	/// Límites axis-aligned de todos los vértices.
	[[nodiscard]] ModelBounds bounds() const {
		ModelBounds b {};
		const u32 n = vertex_count();
		if (n == 0u) return b;
		b.min = vertex(0u);
		b.max = b.min;
		for (u32 i = 1u; i < n; ++i) {
			const math3d::Vec3 v = vertex(i);
			for (u8 k = 0u; k < 3u; ++k) {
				if (v.v[k] < b.min.v[k]) b.min.v[k] = v.v[k];
				if (v.v[k] > b.max.v[k]) b.max.v[k] = v.v[k];
			}
		}
		return b;
	}

private:
	/// ¿Caben `len` bytes en el payload desde `off`? Evita lecturas fuera del chunk
	/// cuando el llamador no ha validado antes.
	[[nodiscard]] bool range_ok(u64 off, u64 len) const noexcept {
		const u64 size = m_bytes.size();
		return off <= size && len <= size - off;
	}

	eng::Span<const u8> m_bytes {}; ///< payload del chunk, no propietario.
};

} // namespace eng::assets
