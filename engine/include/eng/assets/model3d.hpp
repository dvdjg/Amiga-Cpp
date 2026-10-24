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
/// v2 (64 B de cabecera) anade bounds explicitos, esferas y partes articuladas:
/// partes con parent/pivot/rotationTurns, masa/densidad/friccion/restitution y
/// AABB local; cada esfera referencia su parte. El runtime calcula masa, centro de
/// masas e inercia diagonal con `eng::sim3d::CompoundBody3D`.
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

/// Esfera de colision/fisica en coordenadas locales del modelo.
struct ModelSphere {
	s16 center[3] {}; ///< centro local (x, y, z).
	u16 radius = 0; ///< radio > 0.
	u8 part = 0; ///< indice en `ModelPart[]`.
	u8 flags = 0; ///< reservado (trigger/sensor).
	u16 reserved = 0;
};

/// Parte rigida/articulada con masa, inercia derivable y esferas.
struct ModelPart {
	s16 parent = -1; ///< indice de padre; -1 = raiz.
	u16 sphere_first = 0; ///< primer indice en `ModelSphere[]`.
	u16 sphere_count = 0; ///< numero de esferas.
	u16 mass = 0; ///< masa en unidades del juego (0 = derivar de densidad/volumen).
	u16 flags = 0;
	s16 pivot[3] {}; ///< pivote local.
	u16 rotation[3] {}; ///< rotacion local en turns (0..4095).
	s16 aabb_min[3] {}; ///< AABB local minima.
	s16 aabb_max[3] {}; ///< AABB local maxima.
	u16 density = 0; ///< densidad Q8.8 (0 = 1.0).
	u16 friction = 0; ///< friccion Q8.8.
	u16 restitution = 0; ///< restitucion Q8.8.
	u16 reserved = 0;
};
/// Vista de solo lectura del chunk `Model3D` (15).
class Model3DAssetView {
public:
	static constexpr u32 kHeaderSizeV1 = 28u; ///< cabecera v1 (geometria).
	static constexpr u32 kHeaderSizeV2 = 64u; ///< cabecera v2 (geometria + fisica).
	static constexpr u32 kHeaderSize = kHeaderSizeV1; ///< alias historico.
	static constexpr u16 kVersion = 2u; ///< version maxima que entiende esta vista.
	static constexpr u16 kVersionMin = 1u; ///< version minima compatible.
	static constexpr u16 kMaxFaceVertices = 8u; ///< tope de vertices por cara.

	constexpr Model3DAssetView() = default;
	explicit constexpr Model3DAssetView(eng::UafPayload bytes) : m_bytes(bytes.raw()) {}

	/// \return `true` si el payload tiene cabecera v1, offsets dentro del chunk,
	///         caras 3..8, índices y materiales dentro de rango. Coste O(índices +
	///         caras) en la carga, no por frame.
	/// \return 	rue si el payload es v1/v2 valido: offsets dentro del chunk, caras
	///         3..8, indices/materiales en rango y, en v2, esferas/partes coherentes.
	[[nodiscard]] bool valid() const {
		if (m_bytes.size() < kHeaderSizeV1) return false;
		const u16 ver = version();
		if (ver < kVersionMin || ver > kVersion) return false;
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
		if (v_off < header_size() || v_off + static_cast<u64>(nv) * 6u > size) return false;
		if (i_off < v_off + static_cast<u64>(nv) * 6u || i_off + static_cast<u64>(ni) * 2u > size) return false;
		if (f_off < i_off + static_cast<u64>(ni) * 2u || f_off + static_cast<u64>(nf) * 6u > size) return false;
		if (m_off < f_off + static_cast<u64>(nf) * 6u || m_off + static_cast<u64>(nm) * 4u > size) return false;
		for (u32 i = 0u; i < ni; ++i) if (index(i) >= nv) return false;
		for (u32 f = 0u; f < nf; ++f) {
			const ModelFace face = this->face(f);
			if (face.count < 3u || face.count > kMaxFaceVertices) return false;
			if (static_cast<u32>(face.first) + face.count > ni) return false;
			if (face.material >= nm) return false;
		}
		if (ver == 1u) return true;
		const u32 ns = sphere_count();
		const u32 np = part_count();
		const u64 b_off = bounds_offset();
		const u64 s_off = sphere_offset();
		const u64 p_off = part_offset();
		if (np == 0u && ns != 0u) return false;
		if (ns > 0u && np == 0u) return false;
		if (b_off < m_off + static_cast<u64>(nm) * 4u || b_off + 12u > size) return false;
		if (s_off < b_off + 12u || s_off + static_cast<u64>(ns) * 12u > size) return false;
		if (p_off < s_off + static_cast<u64>(ns) * 12u || p_off + static_cast<u64>(np) * 40u > size) return false;
		for (u32 i = 0u; i < ns; ++i) {
			const ModelSphere s = sphere(i);
			if (s.radius == 0u || s.part >= np) return false;
		}
		for (u32 i = 0u; i < np; ++i) {
			const ModelPart part = this->part(i);
			if (part.parent < -1) return false;
			if (part.parent >= 0 && static_cast<u32>(part.parent) >= i) return false;
			if (static_cast<u32>(part.sphere_first) + part.sphere_count > ns) return false;
			for (u8 k = 0u; k < 3u; ++k) if (part.aabb_min[k] > part.aabb_max[k]) return false;
		}
		return true;
	}

	[[nodiscard]] bool header_ok() const { return m_bytes.size() >= kHeaderSizeV1; }
	[[nodiscard]] u16 version() const { return header_ok() ? read_be16(m_bytes.data()) : 0u; }
	/// \return tamano de la cabecera segun la version (v1=28, v2=64).
	[[nodiscard]] u32 header_size() const { return version() == 2u ? kHeaderSizeV2 : kHeaderSizeV1; }
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
		const u32 off = version() == 2u ? 32u : 12u;
		return header_ok() && m_bytes.size() >= off + 4u ? read_be32(m_bytes.data() + off) : 0u;
	}
	/// \return offset del bloque de indices dentro del blob.
	[[nodiscard]] u32 index_offset() const {
		const u32 off = version() == 2u ? 36u : 16u;
		return header_ok() && m_bytes.size() >= off + 4u ? read_be32(m_bytes.data() + off) : 0u;
	}
	[[nodiscard]] u32 face_offset() const {
		const u32 off = version() == 2u ? 40u : 20u;
		return header_ok() && m_bytes.size() >= off + 4u ? read_be32(m_bytes.data() + off) : 0u;
	}
	/// \return offset del bloque de materiales dentro del blob.
	[[nodiscard]] u32 material_offset() const {
		const u32 off = version() == 2u ? 44u : 24u;
		return header_ok() && m_bytes.size() >= off + 4u ? read_be32(m_bytes.data() + off) : 0u;
	}

	/// \return numero de esferas de fisica (v2; 0 en v1).
	[[nodiscard]] u16 sphere_count() const {
		return version() == 2u && m_bytes.size() >= 14u ? read_be16(m_bytes.data() + 12u) : 0u;
	}
	/// \return numero de partes articuladas (v2; 0 en v1).
	[[nodiscard]] u16 part_count() const {
		return version() == 2u && m_bytes.size() >= 16u ? read_be16(m_bytes.data() + 14u) : 0u;
	}
	[[nodiscard]] u32 bounds_offset() const {
		return version() == 2u && m_bytes.size() >= 52u ? read_be32(m_bytes.data() + 48u) : 0u;
	}
	[[nodiscard]] u32 sphere_offset() const {
		return version() == 2u && m_bytes.size() >= 56u ? read_be32(m_bytes.data() + 52u) : 0u;
	}
	[[nodiscard]] u32 part_offset() const {
		return version() == 2u && m_bytes.size() >= 60u ? read_be32(m_bytes.data() + 56u) : 0u;
	}
	/// Esfera `i`, o `{}` fuera de rango.
	[[nodiscard]] ModelSphere sphere(u32 i) const {
		if (version() != 2u || i >= sphere_count()) return {};
		const u64 off = sphere_offset() + static_cast<u64>(i) * 12u;
		if (!range_ok(off, 12u)) return {};
		ModelSphere s {};
		s.center[0] = static_cast<s16>(read_be16(m_bytes.data() + off));
		s.center[1] = static_cast<s16>(read_be16(m_bytes.data() + off + 2u));
		s.center[2] = static_cast<s16>(read_be16(m_bytes.data() + off + 4u));
		s.radius = read_be16(m_bytes.data() + off + 6u);
		s.part = m_bytes.data()[off + 8u];
		s.flags = m_bytes.data()[off + 9u];
		s.reserved = read_be16(m_bytes.data() + off + 10u);
		return s;
	}
	/// Parte `i`, o `{}` fuera de rango.
	[[nodiscard]] ModelPart part(u32 i) const {
		if (version() != 2u || i >= part_count()) return {};
		const u64 off = part_offset() + static_cast<u64>(i) * 40u;
		if (!range_ok(off, 40u)) return {};
		ModelPart p {};
		p.parent = static_cast<s16>(read_be16(m_bytes.data() + off));
		p.sphere_first = read_be16(m_bytes.data() + off + 2u);
		p.sphere_count = read_be16(m_bytes.data() + off + 4u);
		p.mass = read_be16(m_bytes.data() + off + 6u);
		p.flags = read_be16(m_bytes.data() + off + 8u);
		for (u8 k = 0u; k < 3u; ++k) {
			p.pivot[k] = static_cast<s16>(read_be16(m_bytes.data() + off + 10u + k * 2u));
			p.rotation[k] = read_be16(m_bytes.data() + off + 16u + k * 2u);
			p.aabb_min[k] = static_cast<s16>(read_be16(m_bytes.data() + off + 22u + k * 2u));
			p.aabb_max[k] = static_cast<s16>(read_be16(m_bytes.data() + off + 28u + k * 2u));
		}
		p.density = read_be16(m_bytes.data() + off + 34u);
		p.friction = read_be16(m_bytes.data() + off + 36u);
		p.restitution = read_be16(m_bytes.data() + off + 38u);
		return p;
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
		if (version() == 2u) {
			const u64 off = bounds_offset();
			if (off != 0u && range_ok(off, 12u)) {
				for (u8 k = 0u; k < 3u; ++k) {
					b.min.v[k] = eng::math::scalar_traits<eng::coord>::from_int(
						static_cast<s16>(read_be16(m_bytes.data() + off + k * 2u)));
					b.max.v[k] = eng::math::scalar_traits<eng::coord>::from_int(
						static_cast<s16>(read_be16(m_bytes.data() + off + 6u + k * 2u)));
				}
				return b;
			}
		}
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
