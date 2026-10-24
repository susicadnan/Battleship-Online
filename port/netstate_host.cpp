/**
 * netstate_host.cpp — see netstate_host.h. Aggregates the per-subsystem
 * snapshot hooks into the one blob sys/netstate.c stores per slot.
 */
#include "netstate_host.h"

#include <cstdint>
#include <cstddef>

/* bridge/lbreloc_byteswap.cpp */
extern "C" void portStructFixupsJournalEnable(int on, const void *rewind_base, size_t rewind_size);
extern "C" uint64_t portStructFixupsJournalMark(void);
extern "C" int portStructFixupsJournalRewind(uint64_t mark);
extern "C" void portStructFixupsJournalTrim(uint64_t oldest_mark);

/* bridge/lbreloc_bridge.cpp */
extern "C" void *portRelocBridgeSnapshot(void);
extern "C" void portRelocBridgeRestore(const void *blob);
extern "C" void portRelocBridgeFreeSnapshot(void *blob);

namespace {

struct PortNetStateHostBlob
{
	uint64_t fixup_mark;
	void *bridge;
};

} // namespace

extern "C" void port_netstate_host_begin(const void *arena, size_t arena_size)
{
	portStructFixupsJournalEnable(1, arena, arena_size);
}

extern "C" void port_netstate_host_end(void)
{
	portStructFixupsJournalEnable(0, nullptr, 0);
}

extern "C" void *port_netstate_host_save(void)
{
	auto *blob = new PortNetStateHostBlob;
	blob->fixup_mark = portStructFixupsJournalMark();
	blob->bridge = portRelocBridgeSnapshot();
	return blob;
}

extern "C" int port_netstate_host_restore(const void *blob_v)
{
	const auto *blob = static_cast<const PortNetStateHostBlob *>(blob_v);
	if (blob == nullptr) {
		return 0;
	}
	if (!portStructFixupsJournalRewind(blob->fixup_mark)) {
		return 0;
	}
	portRelocBridgeRestore(blob->bridge);
	return 1;
}

extern "C" void port_netstate_host_free(void *blob_v)
{
	auto *blob = static_cast<PortNetStateHostBlob *>(blob_v);
	if (blob == nullptr) {
		return;
	}
	portRelocBridgeFreeSnapshot(blob->bridge);
	delete blob;
}

extern "C" void port_netstate_host_trim(const void *blob_v)
{
	const auto *blob = static_cast<const PortNetStateHostBlob *>(blob_v);
	if (blob == nullptr) {
		return;
	}
	portStructFixupsJournalTrim(blob->fixup_mark);
}
