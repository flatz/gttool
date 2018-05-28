#include "debug.hpp"

void hexDump(const void* data, size_t dataSize)
{
	const uint8_t* p = static_cast<const uint8_t*>(data);

	// TODO: refactor
	for (size_t i = 0; i < dataSize; ++i) {
		printf("%02X ", p[i]);
		if (i != 0 && (i & 0xF) == 0xF) {
			printf("\n");
		}
	}
}
