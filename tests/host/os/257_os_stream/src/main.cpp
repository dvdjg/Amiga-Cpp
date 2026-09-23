// ============================================================================
// Test HOST-257: streaming de chunks con doble buffer (eng/os/stream.hpp).
// ============================================================================
//
// Valida la politica de buffers: que pide los vacios, que `advance` consume el actual y pasa al
// siguiente (con underrun si no estaba listo) y el fin de archivo.
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/os/257_os_stream

#include <cstdio>

#include <eng/os/stream.hpp>

using namespace eng;
using namespace eng::os;

namespace {

int failures = 0;
void check(bool ok, const char* msg) {
	if (!ok) {
		std::printf("  [FAIL] %s\n", msg);
		++failures;
	}
}

void test_double_buffer() {
	ChunkStream<2> s;
	check(s.request_mask() == 0x03u, "al inicio pide los 2 buffers");
	check(!s.play_ready(), "sin datos no hay buffer para reproducir");

	(void)s.on_chunk_ready(0);
	check(s.play_ready(), "el buffer 0 esta listo");
	check(s.request_mask() == 0x02u, "solo falta el buffer 1");
	(void)s.on_chunk_ready(1);
	check(s.request_mask() == 0x00u, "no hay trabajo pendiente");

	check(s.advance(), "avanza al buffer 1 (estaba listo)");
	check(s.play_index() == 1u, "ahora suena el buffer 1");
	check(s.request_mask() == 0x01u, "el buffer 0 quedo libre");
	check(!s.underrun(), "sin underrun");
}

void test_underrun() {
	ChunkStream<2> u;
	(void)u.on_chunk_ready(0);
	check(u.advance() == false, "el siguiente buffer no estaba listo -> underrun");
	check(u.underrun(), "underrun marcado");
	u.clear_underrun();
	check(!u.underrun(), "underrun limpiado");
}

void test_eof() {
	ChunkStream<3> e;
	check(e.needs_data(), "sin eof pide datos");
	e.set_eof();
	check(!e.needs_data(), "con eof no pide mas");
	(void)e.on_chunk_ready(0);
	check(e.play_ready(), "el buffer listo se reproduce");
	check(!e.finished(), "no ha terminado (queda buffer por consumir)");
}

} // namespace

int main() {
	test_double_buffer();
	test_underrun();
	test_eof();

	if (failures == 0) {
		std::printf("OK: streaming de chunks (doble buffer, underrun, eof) validado.\n");
		return 0;
	}
	std::printf("FAIL: %d comprobaciones\n", failures);
	return 1;
}
