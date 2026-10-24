/**
 * netstate_host.cpp — see netstate_host.h. Aggregates the per-subsystem
 * snapshot hooks into the one blob sys/netstate.c stores per slot.
 */
#include "netstate_host.h"

#include "resource/RelocPointerTable.h"

#include <cstdint>
#include <cstddef>

/* bridge/lbreloc_byteswap.cpp */
extern "C" void portStructFixupsJournalEnable(int on);
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
	void *reloc_table;
};

} // namespace

extern "C" void port_netstate_host_begin(void)
{
	portStructFixupsJournalEnable(1);
}

extern "C" void port_netstate_host_end(void)
{
	portStructFixupsJournalEnable(0);
}

extern "C" void *port_netstate_host_save(void)
{
	auto *blob = new PortNetStateHostBlob;
	blob->fixup_mark = portStructFixupsJournalMark();
	blob->bridge = portRelocBridgeSnapshot();
	blob->reloc_table = portRelocPointerTableSaveMark();
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
	portRelocPointerTableRestoreMark(blob->reloc_table);
	return 1;
}

extern "C" void port_netstate_host_free(void *blob_v)
{
	auto *blob = static_cast<PortNetStateHostBlob *>(blob_v);
	if (blob == nullptr) {
		return;
	}
	portRelocBridgeFreeSnapshot(blob->bridge);
	portRelocPointerTableFreeMark(blob->reloc_table);
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
