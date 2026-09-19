#pragma once

/// \file uaf.hpp
/// Base de la capa `eng::assets`: **contenedor UAF-R** (Universal Asset Format -
/// Runtime). Es el formato "cocinado" por el exportador host que el Amiga mapea
/// por **offsets validados** (sin parsing pesado en el runtime).
///
/// Un blob UAF-R es:
///
///   header  { u32 magic="UAFR", u16 version, u16 chunk_count }
///   chunk[] { u16 type, u16 count, u32 size, <size bytes de datos>, pad a 4 }
///
/// `Blob::bind` valida el header y recorre los chunks comprobando que cada
/// `offset+size` cae dentro del blob; `data()` devuelve un `Span` acotado a cada
/// chunk. Los consumidores concretos (paletas, bitplanes, samples…) se construirán
/// encima; aquí solo vive el contrato de contenedor y su validación (host-testable).
///
/// Endianness: el blob está en **big-endian** (nativo m68k). Los lectores `read_be16/32`
/// funcionan igual en host (x86) que en Amiga.

#include <eng/core/domains.hpp>
#include <eng/core/fast_div.hpp>
#include <eng/core/mesh3d.hpp>
#include <eng/core/span.hpp>
#include <eng/core/typed.hpp>
#include <eng/core/types.hpp>
#include <eng/core/util/flat_map.hpp>
#include <eng/core/util/static_vector.hpp>

namespace eng::assets {

/// Magic del contenedor UAF-R ("UAFR").
constexpr u32 kUafMagic = 0x55414652u;
/// Versión del contenedor que entiende este runtime.
constexpr u16 kUafVersion = 1u;
/// Cabecera del contenedor: magic + version + nº de chunks.
constexpr u32 kContainerHeaderSize = 8u;
/// Cabecera de un chunk: type(2) + count(2) + size(4).
constexpr u32 kChunkHeaderSize = 8u;

/// Tipos de chunk previstos (los consumidores se añaden por encima).
enum class ChunkType : u16 {
	Palette = 1,
	Bitplanes = 2,
	CopperTemplates = 3,
	PatchTables = 4,
	Sprites = 5,
	Bobs = 6,
	Tiles = 7,
	Collision = 8,
	Strings = 9,
	Samples = 10,
	Modules = 11,
	Mesh = 12,
	WorldMap = 13,
	MeshPoly = 14,
};

/// Referencia validada a un chunk dentro del blob.
struct ChunkRef {
	ChunkType type = ChunkType::Palette;
	u16 count = 0; // elementos lógicos (semántica por tipo)
	u32 offset = 0; // inicio de los datos del chunk, dentro del blob
	u32 size = 0;   // bytes de datos
};

/// Lector big-endian (2 bytes).
constexpr u16 read_be16(const u8* p) {
	return static_cast<u16>((static_cast<u16>(p[0]) << 8u) | p[1]);
}
/// Lector big-endian (4 bytes).
constexpr u32 read_be32(const u8* p) {
	return (static_cast<u32>(p[0]) << 24u) | (static_cast<u32>(p[1]) << 16u) |
	       (static_cast<u32>(p[2]) << 8u) | static_cast<u32>(p[3]);
}
/// Escritor big-endian (2 bytes).
constexpr void write_be16(u8* p, u16 v) {
	p[0] = static_cast<u8>(v >> 8u);
	p[1] = static_cast<u8>(v);
}
/// Escritor big-endian (4 bytes).
constexpr void write_be32(u8* p, u32 v) {
	p[0] = static_cast<u8>(v >> 24u);
	p[1] = static_cast<u8>(v >> 16u);
	p[2] = static_cast<u8>(v >> 8u);
	p[3] = static_cast<u8>(v);
}

/// Cursor tipado de LECTURA sobre un `Span<const u8>` (big-endian), con
/// comprobación de límites. Si una lectura agota los datos, `ok()` pasa a `false`
/// y las lecturas siguientes devuelven 0 (no se sale del buffer). Es la primitiva
/// segura sobre la que se construyen las vistas de chunk.
class Reader {
public:
	constexpr Reader() = default;
	explicit constexpr Reader(eng::UafPayload data) : m_data(data.raw()) {}

	constexpr bool ok() const { return m_ok; }
	constexpr u32 position() const { return m_pos; }
	constexpr u32 remaining() const { return m_ok ? static_cast<u32>(m_data.size()) - m_pos : 0u; }

	u8 read_u8() {
		if (remaining() < 1u) { m_ok = false; return 0u; }
		return m_data.data()[m_pos++];
	}
	s8 read_s8() { return static_cast<s8>(read_u8()); }
	u16 read_u16() {
		if (remaining() < 2u) { m_ok = false; m_pos = static_cast<u32>(m_data.size()); return 0u; }
		const u16 v = read_be16(m_data.data() + m_pos);
		m_pos += 2u;
		return v;
	}
	u32 read_u32() {
		if (remaining() < 4u) { m_ok = false; m_pos = static_cast<u32>(m_data.size()); return 0u; }
		const u32 v = read_be32(m_data.data() + m_pos);
		m_pos += 4u;
		return v;
	}
	/// Salta `n` bytes (false si no quedan).
	bool skip(u32 n) {
		if (remaining() < n) { m_ok = false; return false; }
		m_pos += n;
		return true;
	}
	/// Toma `n` bytes como sub-vista (vacía si no quedan).
	Span<const u8> take(u32 n) {
		if (remaining() < n) { m_ok = false; return {}; }
		Span<const u8> s { m_data.data() + m_pos, n };
		m_pos += n;
		return s;
	}

private:
	Span<const u8> m_data {};
	u32 m_pos = 0;
	bool m_ok = true;
};

/// Vista validada sobre un blob UAF-R. No copia: apunta a la memoria del asset.
class Blob {
public:
	static constexpr u32 kMaxChunks = 32;

	/// Valida el header y recorre los chunks. Devuelve true si el blob es válido.
	bool bind(eng::UafPayload blob) {
		m_blob = blob.raw();
		m_chunks.clear();
		m_index.clear();
		m_ok = false;
		if (blob.data() == nullptr || blob.size() < kContainerHeaderSize) {
			return false;
		}
		Reader r {blob};
		if (r.read_u32() != kUafMagic || r.read_u16() != kUafVersion) {
			return false;
		}
		const u32 n = r.read_u16();
		if (!r.ok() || n > kMaxChunks) {
			return false;
		}
		for (u32 i = 0; i < n; ++i) {
			ChunkRef ref {};
			ref.type = static_cast<ChunkType>(r.read_u16());
			ref.count = r.read_u16();
			ref.size = r.read_u32();
			if (!r.ok()) {
				return false;
			}
			ref.offset = r.position();
			if (!r.skip(ref.size)) {
				return false;
			}
			const u32 pad = (4u - (ref.size & 3u)) & 3u; // los datos van 4-alineados
			if (!r.skip(pad)) {
				return false;
			}
			// Índice por tipo para `find` (el primer chunk de cada tipo manda). Se
			// construye aquí, en la única pasada de parseo, con un `FlatMap` de tamaño
			// acotado por `kMaxChunks`.
			const u16 index = static_cast<u16>(m_chunks.size());
			if (m_index.find(ref.type) == nullptr) {
				m_index.insert(ref.type, index);
			}
			m_chunks.push_back(ref);
		}
		m_ok = true;
		return true;
	}

	constexpr bool ok() const { return m_ok; }
	constexpr u32 chunk_count() const { return static_cast<u32>(m_chunks.size()); }
	constexpr const ChunkRef& chunk(u32 i) const { return m_chunks[i]; }
	constexpr eng::UafPayload blob() const { return eng::UafPayload { m_blob.data(), m_blob.size() }; }

	/// Datos de un chunk (vacío si el índice no es válido).
	eng::UafPayload data(u32 i) const {
		if (i >= m_chunks.size()) {
			return {};
		}
		return data(m_chunks[i]);
	}

	/// Datos del chunk referenciado (vista sobre el blob, sin copiar).
	eng::UafPayload data(const ChunkRef& c) const {
		return { m_blob.data() + c.offset, c.size };
	}

	/// Primer chunk del tipo pedido (o `nullptr`). Usa el índice por tipo, en `O(log n)`.
	const ChunkRef* find(ChunkType type) const {
		const u16* index = m_index.find(type);
		return index != nullptr ? &m_chunks[*index] : nullptr;
	}

private:
	Span<const u8> m_blob {};
	/// Chunks en orden de aparición (capacidad fija, sin `m_count` manual).
	eng::util::StaticVector<ChunkRef, kMaxChunks> m_chunks {};
	/// Índice tipo → posición en `m_chunks` (primer chunk de cada tipo).
	eng::util::FlatMap<ChunkType, u16, kMaxChunks> m_index {};
	bool m_ok = false;
};

/// Vista tipada de un chunk de **paleta**: N colores RGB444 (u16 big-endian).
/// Interfaz segura: no expone punteros crudos y valida que el tamaño sea par.
class PaletteView {
public:
	constexpr PaletteView() = default;
	explicit constexpr PaletteView(eng::UafPayload bytes) : m_bytes(bytes.raw()) {}
	constexpr u32 count() const { return static_cast<u32>(m_bytes.size() / 2u); }
	constexpr bool valid() const { return (m_bytes.size() & 1u) == 0u; }
	/// Color `i` en RGB444 (big-endian; válido en host y m68k).
	constexpr u16 color(u32 i) const { return read_be16(m_bytes.data() + i * 2u); }
	/// Vista de dominio de las palabras de la paleta (`PaletteWords`, RGB444). Está en
	/// orden **nativo** (m68k); en host los bytes van en big-endian, así que para leer un
	/// color de forma portable usa `color(i)`.
	eng::PaletteWords words() const {
		return eng::PaletteWords {reinterpret_cast<const u16*>(m_bytes.data()), count()};
	}

private:
	Span<const u8> m_bytes {};
};

/// Vista tipada de un chunk de **sample**: N bytes 8-bit con signo.
class SampleView {
public:
	constexpr SampleView() = default;
	explicit constexpr SampleView(eng::UafPayload bytes)
		: m_samples(bytes.data(), bytes.size()) {}
	constexpr u32 size() const { return static_cast<u32>(m_samples.size()); }
	constexpr bool empty() const { return m_samples.empty(); }
	/// Muestras 8-bit con signo como vista de dominio de audio (`AudioSample`).
	constexpr eng::AudioSample samples() const { return m_samples; }

private:
	eng::AudioSample m_samples {};
};

/// Vista tipada de un chunk de **bitplanes**: cabecera de geometría + datos
/// planares contiguos. Formato del chunk de datos:
///
///   u16 width, u16 height, u16 row_bytes, u8 planes, u8 layout, u8 flags, u8 resv
///   <row_bytes * height * planes bytes>
///
/// `read()` valida la longitud con el `Reader` y deja una vista acotada (sin copia),
/// lista para inicializar un `gfx::Bitmap`.
class BitplanesView {
public:
	bool read(eng::UafPayload bytes) {
		Reader r {bytes};
		m_width = r.read_u16();
		m_height = r.read_u16();
		m_row_bytes = r.read_u16();
		m_planes = r.read_u8();
		m_layout = r.read_u8();
		r.read_u8(); // flags
		r.read_u8(); // reservado
		const u32 plane_bytes = static_cast<u32>(m_row_bytes) * m_height * m_planes;
		m_data = eng::PlaneViewBytes::from(r.take(plane_bytes));
		return r.ok();
	}

	constexpr u16 width() const { return m_width; }
	constexpr u16 height() const { return m_height; }
	constexpr u16 row_bytes() const { return m_row_bytes; }
	constexpr u8 planes() const { return m_planes; }
	constexpr u8 layout() const { return m_layout; }
	/// Datos planares como vista de dominio de plano (`PlaneViewBytes`).
	constexpr eng::PlaneViewBytes data() const { return m_data; }

private:
	u16 m_width = 0;
	u16 m_height = 0;
	u16 m_row_bytes = 0;
	u8 m_planes = 0;
	u8 m_layout = 0;
	eng::PlaneViewBytes m_data {};
};

/// Vista tipada de un chunk de **textos**: `count` cadenas C separadas por NUL
/// (UTF-8), la última terminada en NUL. `string(i)` devuelve un `const char*`
/// apuntando dentro del chunk (o `nullptr` si el índice no es válido).
class StringsView {
public:
	constexpr StringsView() = default;
	explicit constexpr StringsView(eng::UafPayload bytes) : m_bytes(bytes.raw()) {}

	/// Nº de cadenas (cada NUL cierra una).
	u32 count() const {
		u32 n = 0;
		for (const u8 b : m_bytes) {
			if (b == 0u) {
				++n;
			}
		}
		return n;
	}

	/// Cadena `i` como C-string dentro del chunk (o `nullptr`). El NUL que cierra
	/// la cadena vive en el propio chunk, así que el puntero es válido mientras lo
	/// sea el blob.
	const char* string(u32 i) const {
		u32 seen = 0;
		u32 start = 0;
		const u8* p = m_bytes.data();
		for (u32 k = 0; k < m_bytes.size(); ++k) {
			if (p[k] == 0u) {
				if (seen == i) {
					return reinterpret_cast<const char*>(p + start);
				}
				++seen;
				start = k + 1u;
			}
		}
		return nullptr;
	}

private:
	Span<const u8> m_bytes {};
};

/// Vista tipada de un banco de **tiles** de tamaño fijo (`tile_bytes`). El tamaño
/// del tile lo conoce el llamador (formato del banco); `tile(i)` devuelve el
/// sub-`Span` del tile `i` (vacío si el índice no es válido).
class TilesView {
public:
	constexpr TilesView() = default;
	constexpr TilesView(eng::UafPayload bytes, u16 tile_bytes)
		: m_bytes(bytes.data(), bytes.size()), m_tile(tile_bytes) {}

	constexpr u32 count() const {
		return m_tile == 0u ? 0u : static_cast<u32>(m_bytes.size()) / m_tile;
	}
	/// Tile `i` como vista de dominio de tiles indexados (`IndexedTiles`).
	eng::IndexedTiles tile(u32 i) const {
		if (m_tile == 0u || i >= count()) {
			return {};
		}
		return eng::IndexedTiles {m_bytes.data() + i * m_tile, m_tile};
	}

private:
	eng::IndexedTiles m_bytes {};
	u16 m_tile = 0;
};

/// Vista tipada de un chunk de **sprites hardware**: cabecera `u16 words_per_sprite`
/// y `count` sprites de palabras big-endian. `word(i, w)` da la palabra `w` del
/// sprite `i` (los mismos 16 bits que copiaría el Copper/agnus a SPRxDATA).
class SpritesView {
public:
	constexpr SpritesView() = default;
	explicit constexpr SpritesView(eng::UafPayload bytes) : m_bytes(bytes.raw()) {}

	u16 words_per_sprite() const {
		return m_bytes.size() >= 2u ? read_be16(m_bytes.data()) : 0u;
	}
	u32 count() const {
		const u16 w = words_per_sprite();
		if (w == 0u || m_bytes.size() < 2u) {
			return 0u;
		}
		return static_cast<u32>(m_bytes.size() - 2u) / (static_cast<u32>(w) * 2u);
	}
	u16 word(u32 i, u32 w) const {
		if (w >= words_per_sprite() || i >= count()) {
			return 0u;
		}
		const u32 off = 2u + (i * words_per_sprite() + w) * 2u;
		return read_be16(m_bytes.data() + off);
	}
	/// Palabras del chunk como vista de dominio de sprite (`SpriteWords`); la primera
	/// palabra es `words_per_sprite`. Está en orden **nativo** (m68k); para lectura
	/// portable usa `word(i, w)`.
	eng::SpriteWords words() const {
		return eng::SpriteWords {reinterpret_cast<const u16*>(m_bytes.data()), m_bytes.size() / 2u};
	}

private:
	Span<const u8> m_bytes {};
};

/// Vista tipada de un chunk de **copperlist** portátil: `count` palabras `u16`
/// big-endian (`WAIT`/`MOVE`). El backend decide cómo materializarla.
class CopperView {
public:
	constexpr CopperView() = default;
	explicit constexpr CopperView(eng::UafPayload bytes) : m_bytes(bytes.raw()) {}

	constexpr u32 count() const { return static_cast<u32>(m_bytes.size()) / 2u; }
	u16 word(u32 i) const {
		return i < count() ? read_be16(m_bytes.data() + i * 2u) : 0u;
	}
	/// Palabras del chunk como vista de dominio de Copper (`CopperWords`: pares
	/// WAIT/MOVE). Está en orden **nativo** (m68k), listo para el backend; para lectura
	/// portable usa `word(i)`.
	eng::CopperWords words() const {
		return eng::CopperWords {reinterpret_cast<const u16*>(m_bytes.data()), count()};
	}

private:
	Span<const u8> m_bytes {};
};

/// Vista tipada de un chunk de **malla 3D** (formato `obj2c`):
///
///   header { u16 vertex_count, u16 face_count }
///   vertex[] { s16 x, s16 y, s16 z }   (big-endian)
///   face[]   { u16 a, u16 b, u16 c }   (big-endian, índices de vértice)
///
/// `vertex(i)`/`face(i)` decodifican big-endian, así que funcionan igual en host
/// (x86) que en Amiga; el llamador copia los vértices a su buffer y construye un
/// `math3d::MeshView` (ver `eng/core/mesh3d.hpp`).
class MeshAssetView {
public:
	constexpr MeshAssetView() = default;
	explicit constexpr MeshAssetView(eng::UafPayload bytes) : m_bytes(bytes.raw()) {}

	u32 vertex_count() const {
		return m_bytes.size() >= 2u ? read_be16(m_bytes.data()) : 0u;
	}
	u32 face_count() const {
		return m_bytes.size() >= 4u ? read_be16(m_bytes.data() + 2u) : 0u;
	}
	/// ¿El chunk contiene cabecera + todos los vértices y caras declarados?
	bool valid() const {
		const u32 needed = 4u + vertex_count() * 6u + face_count() * 6u;
		return m_bytes.size() >= needed;
	}
	math3d::Vec3 vertex(u32 i) const {
		const u32 off = 4u + i * 6u;
		return math3d::vec3(static_cast<s16>(read_be16(m_bytes.data() + off)),
				    static_cast<s16>(read_be16(m_bytes.data() + off + 2u)),
				    static_cast<s16>(read_be16(m_bytes.data() + off + 4u)));
	}
	math3d::Face face(u32 i) const {
		const u32 off = 4u + vertex_count() * 6u + i * 6u;
		return {
			read_be16(m_bytes.data() + off),
			read_be16(m_bytes.data() + off + 2u),
			read_be16(m_bytes.data() + off + 4u),
		};
	}

private:
	Span<const u8> m_bytes {};
};

/// Vista tipada de un chunk de **malla n-gon** (caras de longitud variable):
///
///   header { u16 vertex_count, u16 face_count, u16 index_count }
///   vertex[] { s16 x, s16 y, s16 z }        (big-endian)
///   index[]  { u16 }                        (big-endian; índices de vértice, concatenados)
///   face[]   { u16 first, u16 count }       (big-endian; rango en index[])
///
/// Es la forma amiga del raster (rellena polígonos convexos) y evita triangular caras de
/// 5-6 lados. `vertex(i)`/`index(i)`/`face(i)` decodifican big-endian, así que funcionan
/// igual en host (x86) que en Amiga; `view()` copia a buffers del llamador y devuelve un
/// `math3d::PolyMeshView` listo para `math3d::mesh_patches_order`.
class PolyMeshAssetView {
public:
	constexpr PolyMeshAssetView() = default;
	explicit constexpr PolyMeshAssetView(eng::UafPayload bytes) : m_bytes(bytes.raw()) {}

	u32 vertex_count() const { return m_bytes.size() >= 2u ? read_be16(m_bytes.data()) : 0u; }
	u32 face_count() const { return m_bytes.size() >= 4u ? read_be16(m_bytes.data() + 2u) : 0u; }
	u32 index_count() const { return m_bytes.size() >= 6u ? read_be16(m_bytes.data() + 4u) : 0u; }

	/// ¿El chunk contiene cabecera + todos los vértices, índices y caras declarados?
	bool valid() const {
		const u32 needed = 6u + vertex_count() * 6u + index_count() * 2u + face_count() * 4u;
		return m_bytes.size() >= needed;
	}

	math3d::Vec3 vertex(u32 i) const {
		const u32 off = 6u + i * 6u;
		return math3d::vec3(static_cast<s16>(read_be16(m_bytes.data() + off)),
				    static_cast<s16>(read_be16(m_bytes.data() + off + 2u)),
				    static_cast<s16>(read_be16(m_bytes.data() + off + 4u)));
	}
	u16 index(u32 i) const {
		const u32 off = 6u + vertex_count() * 6u + i * 2u;
		return read_be16(m_bytes.data() + off);
	}
	eng::math3d::FaceSpan face(u32 i) const {
		const u32 off = 6u + vertex_count() * 6u + index_count() * 2u + i * 4u;
		return {read_be16(m_bytes.data() + off), read_be16(m_bytes.data() + off + 2u)};
	}

	/// Copia a buffers del llamador (sin heap) y devuelve la vista n-gon. La vista apunta a
	/// `verts`/`indices`/`faces`; se acota a lo que quepa.
	template <class S = eng::coord>
	eng::math3d::PolyMeshViewT<S> view(eng::Span<eng::math3d::Vec3t<S>> verts,
					   eng::Span<eng::u16> indices,
					   eng::Span<eng::math3d::FaceSpan> faces) const {
		using T = eng::math::scalar_traits<S>;
		using TC = eng::math::scalar_traits<eng::coord>;
		u32 nv = vertex_count();
		if (nv > verts.size()) nv = static_cast<u32>(verts.size());
		for (u32 i = 0; i < nv; ++i) {
			const eng::math3d::Vec3 v = vertex(i);
			verts[i] = eng::math3d::Vec3t<S> {
				{T::from_int(TC::to_int(v.v[0])), T::from_int(TC::to_int(v.v[1])),
				 T::from_int(TC::to_int(v.v[2]))}};
		}
		u32 ni = index_count();
		if (ni > indices.size()) ni = static_cast<u32>(indices.size());
		for (u32 i = 0; i < ni; ++i) indices[i] = index(i);
		u32 nf = face_count();
		if (nf > faces.size()) nf = static_cast<u32>(faces.size());
		for (u32 i = 0; i < nf; ++i) faces[i] = face(i);
		return eng::math3d::PolyMeshViewT<S> {
			eng::Span<const eng::math3d::Vec3t<S>>(verts.data(), nv),
			eng::Span<const eng::u16>(indices.data(), ni),
			eng::Span<const eng::math3d::FaceSpan>(faces.data(), nf)};
	}

private:
	Span<const u8> m_bytes {};
};

/// Vista tipada de un chunk **WorldMap**: mapa de tiles en chunks. Formato en
/// `docs/engine/architecture/WORLD_FORMAT.md`. Valida cabecera, descriptores de
/// capa y bloques (directorio/celdas/meta) contra el tamaño del chunk, y resuelve
/// el acceso por celda con wrap/borde sin decodificar el mapa.
///
/// El `gid` ya viene convertido a índice de banco por el pipeline host; aquí solo
/// se leen índices listos para el motor de tiles (`SparseTileMap`/`TileMapView`).
class WorldView {
public:
	static constexpr u16 kVersion = 1u;
	static constexpr u8 kMaxLayers = 8u;
	static constexpr u32 kHeaderSize = 16u;
	static constexpr u32 kLayerDescSize = 32u;
	static constexpr u32 kChunkEntrySize = 4u;
	static constexpr u32 kMetaEntrySize = 12u;

	/// Entrada de metadatos por capa (spawns/triggers/colisión).
	struct Meta {
		u16 type = 0, x = 0, y = 0, a = 0, b = 0, c = 0;
	};

	bool read(eng::UafPayload bytes) {
		m_bytes = bytes.raw();
		m_ok = false;
		m_layers = 0;
		if (bytes.data() == nullptr || bytes.size() < kHeaderSize) return false;
		Reader r {bytes};
		m_version = r.read_u16();
		m_flags = r.read_u16();
		m_chunk_log2 = r.read_u8();
		const u8 layers = r.read_u8();
		m_tiles_chunk = r.read_u16();
		m_palette_chunk = r.read_u16();
		r.read_u32(); // reservado
		r.read_u16(); // reservado
		if (!r.ok() || m_version != kVersion) return false;
		if (m_chunk_log2 == 0u || m_chunk_log2 > 8u) return false;
		if (layers > kMaxLayers) return false;
		Reader lr {bytes};
		if (!lr.skip(kHeaderSize)) return false;
		const u32 chunk_cells = 1u << (2u * m_chunk_log2);
		for (u8 i = 0; i < layers; ++i) {
			Layer& L = m_layer[i];
			L = Layer {};
			L.id = lr.read_u16();
			L.kind = lr.read_u16();
			L.width = lr.read_u16();
			L.height = lr.read_u16();
			L.wrap_x = lr.read_u16();
			L.wrap_y = lr.read_u16();
			L.empty_tile = lr.read_u16();
			L.meta_count = lr.read_u16();
			L.dir_off = lr.read_u32();
			L.dir_count = lr.read_u32();
			L.cells_off = lr.read_u32();
			L.meta_off = lr.read_u32();
			if (!lr.ok()) return false;
			if (!block_fits(L.dir_off, L.dir_count, kChunkEntrySize, bytes.size())) return false;
			if (!block_fits(L.cells_off, L.dir_count, chunk_cells * 2u, bytes.size())) return false;
			if (L.meta_count != 0u) {
				if (L.meta_off == 0u) return false;
				if (!block_fits(L.meta_off, L.meta_count, kMetaEntrySize, bytes.size())) return false;
			}
		}
		m_layers = layers;
		m_ok = true;
		return true;
	}

	constexpr bool valid() const { return m_ok; }
	constexpr u16 version() const { return m_version; }
	constexpr u16 flags() const { return m_flags; }
	constexpr u8 chunk_log2() const { return m_chunk_log2; }
	constexpr u32 chunk_size() const { return 1u << m_chunk_log2; }
	constexpr u32 chunk_cell_count() const { return 1u << (2u * m_chunk_log2); }
	constexpr u8 layer_count() const { return m_layers; }
	constexpr u16 tiles_chunk() const { return m_tiles_chunk; }
	constexpr u16 palette_chunk() const { return m_palette_chunk; }

	constexpr u16 layer_id(u32 i) const { return m_layer[i].id; }
	constexpr u16 layer_kind(u32 i) const { return m_layer[i].kind; }
	constexpr u16 layer_width(u32 i) const { return m_layer[i].width; }
	constexpr u16 layer_height(u32 i) const { return m_layer[i].height; }
	constexpr u16 layer_wrap_x(u32 i) const { return m_layer[i].wrap_x; }
	constexpr u16 layer_wrap_y(u32 i) const { return m_layer[i].wrap_y; }
	constexpr u16 layer_empty_tile(u32 i) const { return m_layer[i].empty_tile; }
	constexpr u32 layer_dir_count(u32 i) const { return m_layer[i].dir_count; }
	constexpr u16 layer_meta_count(u32 i) const { return m_layer[i].meta_count; }

	/// Índice en el directorio del chunk `(cx,cy)`, o -1 si está ausente.
	/// Búsqueda binaria sobre el directorio ordenado por `(cy,cx)`.
	s32 find_chunk(u32 i, s32 cx, s32 cy) const {
		if (i >= m_layers) return -1;
		const Layer& L = m_layer[i];
		s32 lo = 0, hi = static_cast<s32>(L.dir_count) - 1;
		while (lo <= hi) {
			const s32 mid = lo + (hi - lo) / 2;
			const u8* e = entry(i, static_cast<u32>(mid));
			const s32 ecx = static_cast<s16>(read_be16(e));
			const s32 ecy = static_cast<s16>(read_be16(e + 2u));
			if (ecy == cy && ecx == cx) return mid;
			if (ecy < cy || (ecy == cy && ecx < cx)) lo = mid + 1;
			else hi = mid - 1;
		}
		return -1;
	}

	/// Celda `cell_index` de la capa (en orden de directorio). Fuera de rango ->
	/// `empty_tile`.
	u16 cell(u32 i, u32 cell_index) const {
		if (i >= m_layers) return 0xFFFFu;
		const Layer& L = m_layer[i];
		if (cell_index >= L.dir_count * chunk_cell_count()) return L.empty_tile;
		return read_be16(m_bytes.data() + L.cells_off + cell_index * 2u);
	}

	/// Bloque crudo de celdas (big-endian) del chunk `dir_index` de la capa.
	Span<const u8> chunk_bytes(u32 i, u32 dir_index) const {
		if (i >= m_layers) return {};
		const Layer& L = m_layer[i];
		if (dir_index >= L.dir_count) return {};
		return { m_bytes.data() + L.cells_off + dir_index * chunk_cell_count() * 2u,
		         chunk_cell_count() * 2u };
	}

	/// Copia las celdas del chunk `dir_index` (big-endian) a `dst` (words con
	/// dominio) como `u16` nativos. `false` si el índice no es válido o `dst` es
	/// corto. Es el puente hacia un `Loader` de streaming.
	template <class Tag>
	bool decode_chunk(u32 i, u32 dir_index, eng::Words<Tag> dst) const {
		if (i >= m_layers || dst.data() == nullptr || dst.size() < chunk_cell_count()) return false;
		const Layer& L = m_layer[i];
		if (dir_index >= L.dir_count) return false;
		const u8* p = m_bytes.data() + L.cells_off + dir_index * chunk_cell_count() * 2u;
		for (u32 k = 0; k < chunk_cell_count(); ++k) dst[k] = read_be16(p + k * 2u);
		return true;
	}

	/// Tile de la capa en coordenadas de mundo `(x,y)`, con wrap/borde. Los chunks
	/// ausentes y las celdas fuera de `width`/`height` devuelven `empty_tile`.
	u16 tile_at(u32 i, s32 x, s32 y) const {
		if (i >= m_layers) return 0xFFFFu;
		const Layer& L = m_layer[i];
		if (L.wrap_x != 0u) x = eng::wrap_period(x, L.wrap_x);
		else if (L.width != 0u && (x < 0 || x >= static_cast<s32>(L.width))) return L.empty_tile;
		if (L.wrap_y != 0u) y = eng::wrap_period(y, L.wrap_y);
		else if (L.height != 0u && (y < 0 || y >= static_cast<s32>(L.height))) return L.empty_tile;
		const s32 cx = x >> m_chunk_log2;
		const s32 cy = y >> m_chunk_log2;
		const s32 idx = find_chunk(i, cx, cy);
		if (idx < 0) return L.empty_tile;
		const u32 mask = chunk_size() - 1u;
		const u32 lx = static_cast<u32>(x) & mask;
		const u32 ly = static_cast<u32>(y) & mask;
		return cell(i, static_cast<u32>(idx) * chunk_cell_count() + ly * chunk_size() + lx);
	}

	/// Entrada de metadatos `idx` de la capa (cero si no existe).
	Meta meta_entry(u32 i, u32 idx) const {
		Meta m {};
		if (i >= m_layers) return m;
		const Layer& L = m_layer[i];
		if (L.meta_off == 0u || idx >= L.meta_count) return m;
		const u8* p = m_bytes.data() + L.meta_off + idx * kMetaEntrySize;
		m.type = read_be16(p);
		m.x = read_be16(p + 2u);
		m.y = read_be16(p + 4u);
		m.a = read_be16(p + 6u);
		m.b = read_be16(p + 8u);
		m.c = read_be16(p + 10u);
		return m;
	}

private:
	struct Layer {
		u16 id = 0, kind = 0, width = 0, height = 0;
		u16 wrap_x = 0, wrap_y = 0, empty_tile = 0xFFFFu, meta_count = 0;
		u32 dir_off = 0, dir_count = 0, cells_off = 0, meta_off = 0;
	};

	static bool block_fits(u32 off, u32 count, u32 elem, u32 size) {
		if (count == 0u) return true;
		if (off > size) return false;
		return count <= (size - off) / elem;
	}
	const u8* entry(u32 i, u32 idx) const {
		return m_bytes.data() + m_layer[i].dir_off + idx * kChunkEntrySize;
	}

	Span<const u8> m_bytes {};
	Layer m_layer[kMaxLayers] {};
	u16 m_version = 0, m_flags = 0, m_tiles_chunk = 0, m_palette_chunk = 0;
	u8 m_chunk_log2 = 0, m_layers = 0;
	bool m_ok = false;
};

/// Ensambla un blob UAF-R en un buffer del llamador (exportador host o tests).
/// No posee memoria; `finish()` fija el nº de chunks y devuelve la vista escrita.
class BlobWriter {
public:
	explicit constexpr BlobWriter(Span<u8> buffer) : m_buffer(buffer) {}

	/// Escribe la cabecera (chunk_count se fija en `finish`).
	bool begin() {
		if (m_buffer.data() == nullptr || m_buffer.size() < kContainerHeaderSize) {
			m_ok = false;
			return false;
		}
		write_be32(m_buffer.data(), kUafMagic);
		write_be16(m_buffer.data() + 4u, kUafVersion);
		write_be16(m_buffer.data() + 6u, 0u);
		m_used = kContainerHeaderSize;
		m_count = 0;
		m_ok = true;
		return true;
	}

	/// Añade un chunk (los datos se rellenan a múltiplo de 4).
	bool add_chunk(ChunkType type, u16 count, eng::UafPayload data) {
		if (!m_ok) {
			return false;
		}
		const u32 n = static_cast<u32>(data.size());
		const u32 need = kChunkHeaderSize + n;
		if (m_used + need > m_buffer.size()) {
			m_ok = false;
			return false;
		}
		u8* c = m_buffer.data() + m_used;
		write_be16(c, static_cast<u16>(type));
		write_be16(c + 2u, count);
		write_be32(c + 4u, n);
		for (u32 i = 0; i < n; ++i) {
			c[kChunkHeaderSize + i] = data[i];
		}
		m_used += need;
		const u32 pad = (4u - (m_used & 3u)) & 3u;
		for (u32 i = 0; i < pad; ++i) {
			m_buffer.data()[m_used + i] = 0u;
		}
		m_used += pad;
		++m_count;
		return true;
	}

	/// Fija el nº de chunks y devuelve la vista escrita.
	eng::UafPayload finish() {
		if (!m_ok) {
			return {};
		}
		write_be16(m_buffer.data() + 6u, static_cast<u16>(m_count));
		return { m_buffer.data(), m_used };
	}

	constexpr bool ok() const { return m_ok; }
	constexpr u32 size() const { return m_used; }

private:
	Span<u8> m_buffer {}; // buffer completo del llamador (capacidad = size())
	u32 m_used = 0;       // bytes ya escritos (cursor de escritura)
	u32 m_count = 0;      // chunks añadidos
	bool m_ok = false;
};

} // namespace eng::assets
