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
		const u8* base = blob.data();
		if (read_be32(base) != kUafMagic) {
			return false;
		}
		if (read_be16(base + 4u) != kUafVersion) {
			return false;
		}
		const u32 n = read_be16(base + 6u);
		if (n > kMaxChunks) {
			return false;
		}
		u32 off = kContainerHeaderSize;
		for (u32 i = 0; i < n; ++i) {
			if (off + kChunkHeaderSize > blob.size()) {
				return false;
			}
			const u8* c = base + off;
			ChunkRef r {};
			r.type = static_cast<ChunkType>(read_be16(c));
			r.count = read_be16(c + 2u);
			r.size = read_be32(c + 4u);
			off += kChunkHeaderSize;
			if (off + r.size > blob.size()) {
				return false;
			}
			r.offset = off;
			m_chunks[m_count++] = r;
			off += r.size;
			off = (off + 3u) & ~3u; // padding a 4 bytes
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
