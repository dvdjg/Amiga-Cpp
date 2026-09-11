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

#include <eng/core/math3d.hpp>
#include <eng/core/span.hpp>
#include <eng/core/types.hpp>

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
	explicit constexpr Reader(Span<const u8> data) : m_data(data) {}

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
	bool bind(Span<const u8> blob) {
		m_blob = blob;
		m_count = 0;
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
			m_chunks[m_count++] = ref;
		}
		m_ok = true;
		return true;
	}

	constexpr bool ok() const { return m_ok; }
	constexpr u32 chunk_count() const { return m_count; }
	constexpr const ChunkRef& chunk(u32 i) const { return m_chunks[i]; }
	constexpr Span<const u8> blob() const { return m_blob; }

	/// Datos de un chunk (vacío si el índice no es válido).
	Span<const u8> data(u32 i) const {
		if (i >= m_count) {
			return {};
		}
		const ChunkRef& c = m_chunks[i];
		return { m_blob.data() + c.offset, c.size };
	}

	/// Primer chunk del tipo pedido (o `nullptr`).
	const ChunkRef* find(ChunkType type) const {
		for (u32 i = 0; i < m_count; ++i) {
			if (m_chunks[i].type == type) {
				return &m_chunks[i];
			}
		}
		return nullptr;
	}

private:
	Span<const u8> m_blob {};
	ChunkRef m_chunks[kMaxChunks] {};
	u32 m_count = 0;
	bool m_ok = false;
};

/// Vista tipada de un chunk de **paleta**: N colores RGB444 (u16 big-endian).
/// Interfaz segura: no expone punteros crudos y valida que el tamaño sea par.
class PaletteView {
public:
	constexpr PaletteView() = default;
	explicit constexpr PaletteView(Span<const u8> bytes) : m_bytes(bytes) {}
	constexpr u32 count() const { return static_cast<u32>(m_bytes.size() / 2u); }
	constexpr bool valid() const { return (m_bytes.size() & 1u) == 0u; }
	/// Color `i` en RGB444 (big-endian; válido en host y m68k).
	constexpr u16 color(u32 i) const { return read_be16(m_bytes.data() + i * 2u); }
	constexpr Span<const u8> bytes() const { return m_bytes; }

private:
	Span<const u8> m_bytes {};
};

/// Vista tipada de un chunk de **sample**: N bytes 8-bit con signo.
class SampleView {
public:
	constexpr SampleView() = default;
	explicit constexpr SampleView(Span<const u8> bytes) : m_bytes(bytes) {}
	constexpr u32 size() const { return static_cast<u32>(m_bytes.size()); }
	constexpr bool empty() const { return m_bytes.size() == 0u; }
	constexpr Span<const u8> bytes() const { return m_bytes; }

private:
	Span<const u8> m_bytes {};
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
	bool read(Span<const u8> bytes) {
		Reader r {bytes};
		m_width = r.read_u16();
		m_height = r.read_u16();
		m_row_bytes = r.read_u16();
		m_planes = r.read_u8();
		m_layout = r.read_u8();
		r.read_u8(); // flags
		r.read_u8(); // reservado
		const u32 plane_bytes = static_cast<u32>(m_row_bytes) * m_height * m_planes;
		m_data = r.take(plane_bytes);
		return r.ok();
	}

	constexpr u16 width() const { return m_width; }
	constexpr u16 height() const { return m_height; }
	constexpr u16 row_bytes() const { return m_row_bytes; }
	constexpr u8 planes() const { return m_planes; }
	constexpr u8 layout() const { return m_layout; }
	constexpr Span<const u8> data() const { return m_data; }

private:
	u16 m_width = 0;
	u16 m_height = 0;
	u16 m_row_bytes = 0;
	u8 m_planes = 0;
	u8 m_layout = 0;
	Span<const u8> m_data {};
};

/// Vista tipada de un chunk de **textos**: `count` cadenas C separadas por NUL
/// (UTF-8), la última terminada en NUL. `string(i)` devuelve un `const char*`
/// apuntando dentro del chunk (o `nullptr` si el índice no es válido).
class StringsView {
public:
	constexpr StringsView() = default;
	explicit constexpr StringsView(Span<const u8> bytes) : m_bytes(bytes) {}

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
	constexpr TilesView(Span<const u8> bytes, u16 tile_bytes) : m_bytes(bytes), m_tile(tile_bytes) {}

	constexpr u32 count() const {
		return m_tile == 0u ? 0u : static_cast<u32>(m_bytes.size()) / m_tile;
	}
	Span<const u8> tile(u32 i) const {
		if (m_tile == 0u || i >= count()) {
			return {};
		}
		return { m_bytes.data() + i * m_tile, m_tile };
	}

private:
	Span<const u8> m_bytes {};
	u16 m_tile = 0;
};

/// Vista tipada de un chunk de **sprites hardware**: cabecera `u16 words_per_sprite`
/// y `count` sprites de palabras big-endian. `word(i, w)` da la palabra `w` del
/// sprite `i` (los mismos 16 bits que copiaría el Copper/agnus a SPRxDATA).
class SpritesView {
public:
	constexpr SpritesView() = default;
	explicit constexpr SpritesView(Span<const u8> bytes) : m_bytes(bytes) {}

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

private:
	Span<const u8> m_bytes {};
};

/// Vista tipada de un chunk de **copperlist** portátil: `count` palabras `u16`
/// big-endian (`WAIT`/`MOVE`). El backend decide cómo materializarla.
class CopperView {
public:
	constexpr CopperView() = default;
	explicit constexpr CopperView(Span<const u8> bytes) : m_bytes(bytes) {}

	constexpr u32 count() const { return static_cast<u32>(m_bytes.size()) / 2u; }
	u16 word(u32 i) const {
		return i < count() ? read_be16(m_bytes.data() + i * 2u) : 0u;
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
	explicit constexpr MeshAssetView(Span<const u8> bytes) : m_bytes(bytes) {}

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
		return {
			static_cast<s16>(read_be16(m_bytes.data() + off)),
			static_cast<s16>(read_be16(m_bytes.data() + off + 2u)),
			static_cast<s16>(read_be16(m_bytes.data() + off + 4u)),
		};
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
	bool add_chunk(ChunkType type, u16 count, Span<const u8> data) {
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
	Span<const u8> finish() {
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
