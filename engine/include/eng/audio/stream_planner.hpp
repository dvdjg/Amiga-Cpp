#pragma once

/// \file stream_planner.hpp
/// Planner no bloqueante para intenciones de reproducción chunked.
///
/// `SoundPlanner` conserva la semántica de voces del mixer. Este planner gobierna streams largos:
/// encola `StreamIntent`, el backend reserva buffers y el puerto recibe `IntentDone` cuando la
/// petición se materializa. La IRQ solo avanza `AudioFeeder`; las completaciones se drenan aquí.

#include <eng/audio/stream_intent.hpp>
#include <eng/core/types/ptr.hpp>
#include <eng/os/intent_done.hpp>
#include <eng/os/message.hpp>
#include <eng/os/port.hpp>

namespace eng::audio {

/// Planner de streams con capacidad fija y puerto de completaciones.
template <u16 QueueN, u16 PortN, StreamBackend Backend>
class StreamingAudioPlanner {
public:
	/// Liga el backend y el puerto no propietarios a la cola de intenciones.
	StreamingAudioPlanner(Backend& backend, eng::os::MsgPort<PortN>& port) noexcept
		: m_executor(backend), m_port(&port) {
		m_queue.bind(m_executor);
		m_queue.bind_done(eng::os::IntentDonePoster<PortN> {&port});
	}

	/// Encola una petición sin esperar a buffers libres ni a Paula.
	[[nodiscard]] Ticket declare(const StreamIntent& intent) noexcept { return m_queue.enqueue(intent); }

	/// Ejecuta las intenciones pendientes y postea `IntentDone` desde el bucle normal.
	void flush() noexcept { m_queue.flush(); }

	/// Espera explícitamente a que se materialicen todas las peticiones encoladas.
	void wait_all() noexcept { m_queue.wait_all(); }

	/// Indica si no quedan intenciones pendientes.
	[[nodiscard]] bool empty() const noexcept { return m_queue.empty(); }

private:
	/// Ejecutor que convierte valores de dominio en operaciones del backend.
	StreamExecutor<QueueN, Backend> m_executor;
	/// Puerto no propietario para eventos de completación.
	eng::Ref<eng::os::MsgPort<PortN>> m_port {};
	/// Cola fija compartida por la familia de planners del engine.
	StreamQueue<QueueN, Backend, eng::os::IntentDonePoster<PortN>> m_queue {};
};

} // namespace eng::audio
