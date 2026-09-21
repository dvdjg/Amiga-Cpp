// ============================================================================
// Backend Amiga del acceso a disco a nivel de device (`eng/os/trackdisk.hpp`).
// ============================================================================
//
// Abre `trackdisk.device` (KS 1.3) con `IOExtTD` y hace la E/S con `DoIO`. El buffer de
// `td_read_sync` debe ser **Chip RAM** (la DMA de disco no ve Fast RAM). Ver la ficha del
// emulador `docs/reference/emulators/winuae/trackdisk.md` y `AGENTS.md` §1.11.

#include <eng/os/trackdisk.hpp>

#include <devices/trackdisk.h>
#include <exec/io.h>
#include <exec/ports.h>
#include <exec/types.h>
#include <proto/exec.h>

namespace {

/// Una unidad abierta: puerto de mensajes + IORequest del device.
struct TdSlot {
	struct MsgPort* port = nullptr;
	struct IOExtTD* req = nullptr;
	bool open = false;
};

constexpr eng::u16 kMaxUnits = 4u;
TdSlot g_slots[kMaxUnits] {};

/// Slot de un handle válido y abierto (o `nullptr`).
TdSlot* slot_for(eng::os::TdHandle h) {
	if (h == 0u || h > kMaxUnits) {
		return nullptr;
	}
	TdSlot& s = g_slots[h - 1u];
	return s.open ? &s : nullptr;
}

/// `DoIO` de un comando corto; `true` si no hubo error.
bool do_io(TdSlot& s) {
	return DoIO(reinterpret_cast<struct IORequest*>(s.req)) == 0;
}

} // namespace

eng::os::TdHandle eng::os::td_open(eng::u16 unit) {
	if (unit >= kMaxUnits) {
		return 0u;
	}
	TdSlot& s = g_slots[unit];
	if (s.open) {
		return static_cast<TdHandle>(unit + 1u);
	}
	s.port = CreateMsgPort();
	if (s.port == nullptr) {
		return 0u;
	}
	s.req = static_cast<struct IOExtTD*>(CreateIORequest(s.port, sizeof(struct IOExtTD)));
	if (s.req == nullptr) {
		DeleteMsgPort(s.port);
		s.port = nullptr;
		return 0u;
	}
	if (OpenDevice(reinterpret_cast<CONST_STRPTR>("trackdisk.device"), unit,
		       reinterpret_cast<struct IORequest*>(s.req), 0) != 0) {
		DeleteIORequest(s.req);
		s.req = nullptr;
		DeleteMsgPort(s.port);
		s.port = nullptr;
		return 0u;
	}
	s.open = true;
	return static_cast<TdHandle>(unit + 1u);
}

void eng::os::td_close(TdHandle h) {
	TdSlot* s = slot_for(h);
	if (s == nullptr) {
		return;
	}
	CloseDevice(reinterpret_cast<struct IORequest*>(s->req));
	DeleteIORequest(s->req);
	DeleteMsgPort(s->port);
	*s = TdSlot {};
}

bool eng::os::td_motor(TdHandle h, bool on) {
	TdSlot* s = slot_for(h);
	if (s == nullptr) {
		return false;
	}
	s->req->iotd_Req.io_Command = TD_MOTOR;
	s->req->iotd_Req.io_Length = on ? 1L : 0L;
	return do_io(*s);
}

bool eng::os::td_read_sync(TdHandle h, eng::u32 byte_offset, eng::Span<eng::u8> dst) {
	TdSlot* s = slot_for(h);
	if (s == nullptr || dst.size() == 0u) {
		return false;
	}
	s->req->iotd_Req.io_Command = CMD_READ;
	s->req->iotd_Req.io_Data = static_cast<APTR>(dst.data());
	s->req->iotd_Req.io_Length = static_cast<LONG>(dst.size());
	s->req->iotd_Req.io_Offset = static_cast<ULONG>(byte_offset);
	return do_io(*s);
}

eng::u8 eng::os::td_change_state(TdHandle h) {
	TdSlot* s = slot_for(h);
	if (s == nullptr) {
		return 1u; // sin handle, se asume "no hay disco"
	}
	s->req->iotd_Req.io_Command = TD_CHANGESTATE;
	if (!do_io(*s)) {
		return 1u;
	}
	return s->req->iotd_Req.io_Actual != 0u ? 1u : 0u;
}

eng::u32 eng::os::td_change_num(TdHandle h) {
	TdSlot* s = slot_for(h);
	if (s == nullptr) {
		return 0u;
	}
	s->req->iotd_Req.io_Command = TD_CHANGENUM;
	if (!do_io(*s)) {
		return 0u;
	}
	return static_cast<eng::u32>(s->req->iotd_Req.io_Actual);
}
