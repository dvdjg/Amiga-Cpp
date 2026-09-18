#pragma once

/// \file file_block_source.hpp
/// **Fuente de bloques respaldada por fichero** del PC (host). Implementa el
/// contrato `BlockSource` leyendo bloques de un fichero con `std::FILE`; es la E/S
/// real del lado de herramientas/host, simétrica al trackloader que irá en el
/// Amiga.
///
/// En `m68k` freestanding no hay `<cstdio>` ni sistema de archivos: la clase existe
/// (para no romper el contrato) pero `open` falla y `fetch` devuelve `Empty`. El
/// backend de disquete del Amiga se implementará con el trackloader de hardware
/// (`docs/engine/architecture/STREAMING_LOADER.md` §5).
///
/// Verificación: HOST-151.

#include <eng/board/storage/block_source.hpp>
#include <eng/core/span.hpp>
#include <eng/core/types.hpp>
#include <eng/core/util/string_view.hpp>

#if !defined(__m68k__)
#include <cstdio>
#endif

namespace eng::board {

#if !defined(__m68k__)

/// Fichero del PC leído por bloques. No copiable (posee el `FILE*`).
class FileBlockSource {
public:
	static constexpr eng::usize max_path = 512u;

	FileBlockSource() noexcept = default;
	~FileBlockSource() noexcept { close(); }
	FileBlockSource(const FileBlockSource&) = delete;
	FileBlockSource& operator=(const FileBlockSource&) = delete;

	/// Abre el fichero y fija el tamaño de bloque. `false` si falla.
	[[nodiscard]] bool open(eng::util::StringView path, u32 block_size) noexcept {
		close();
		if (block_size == 0u || path.empty()) {
			return false;
		}
		char buffer[max_path];
		const eng::usize count = (path.size() < (max_path - 1u)) ? path.size() : (max_path - 1u);
		for (eng::usize i = 0u; i < count; ++i) {
			buffer[i] = path[i];
		}
		buffer[count] = '\0';

		std::FILE* file = std::fopen(buffer, "rb");
		if (file == nullptr) {
			return false;
		}
		if (std::fseek(file, 0, SEEK_END) != 0) {
			std::fclose(file);
			return false;
		}
		const long size = std::ftell(file);
		if (size < 0 || std::fseek(file, 0, SEEK_SET) != 0) {
			std::fclose(file);
			return false;
		}
		m_file = file;
		m_block_size = block_size;
		m_block_count = static_cast<u32>(static_cast<unsigned long>(size) / block_size);
		return true;
	}

	void close() noexcept {
		if (m_file != nullptr) {
			std::fclose(m_file);
			m_file = nullptr;
		}
		m_block_size = 0u;
		m_block_count = 0u;
	}

	[[nodiscard]] bool valid() const noexcept { return m_file != nullptr; }
	[[nodiscard]] u32 block_size() const noexcept { return m_block_size; }
	[[nodiscard]] u32 block_count() const noexcept { return m_block_count; }

	[[nodiscard]] BlockStatus fetch(u32 block_id, eng::Span<u8> dst) const noexcept {
		if (m_file == nullptr || block_id >= m_block_count) {
			return BlockStatus::Empty;
		}
		const long offset = static_cast<long>(static_cast<unsigned long>(block_id) * m_block_size);
		if (std::fseek(m_file, offset, SEEK_SET) != 0) {
			return BlockStatus::Empty;
		}
		const eng::usize count = (dst.size() < m_block_size) ? dst.size() : m_block_size;
		// `fread` exige un puntero C; es la frontera de E/S (única excepción admitida).
		if (std::fread(dst.data(), 1u, count, m_file) != count) {
			return BlockStatus::Empty;
		}
		return BlockStatus::Ready;
	}

private:
	std::FILE* m_file = nullptr;
	u32 m_block_size = 0u;
	u32 m_block_count = 0u;
};

#else

/// Stub para m68k freestanding: sin sistema de archivos. Reservado al trackloader.
class FileBlockSource {
public:
	[[nodiscard]] bool open(eng::util::StringView, u32) noexcept { return false; }
	void close() noexcept {}
	[[nodiscard]] bool valid() const noexcept { return false; }
	[[nodiscard]] u32 block_size() const noexcept { return 0u; }
	[[nodiscard]] u32 block_count() const noexcept { return 0u; }
	[[nodiscard]] BlockStatus fetch(u32, eng::Span<u8>) const noexcept {
		return BlockStatus::Empty;
	}
};

#endif

static_assert(BlockSource<FileBlockSource>, "FileBlockSource cumple el contrato BlockSource");

} // namespace eng::board
