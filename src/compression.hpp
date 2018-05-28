#pragma once

#include "common.hpp"

#include <vector>

class FileExpand
{
public:
	static bool inflate(std::vector<uint8_t>& out, const uint8_t* data, size_t dataSize);
	
	static bool checkIfExpanded(const std::vector<uint8_t>& data);
	static bool unexpand(const std::vector<uint8_t>& in, std::vector<uint8_t>& out);

private:
	static const auto MAGIC = UINT32_C(0xFFF7F32F);
	
	static const auto ALIGNMENT = 0x400;

	struct SuperHeader
	{
		uint32_t magic;
		uint32_t decompressedFileSize;
		uint32_t fileSize;
		uint32_t segmentSize;
		uint32_t flags;
		uint32_t pad[3];
	};
	
	struct SegmentHeader
	{
		uint32_t magic;
		uint32_t size;
		uint32_t zSize;
		uint32_t checkSum;
	};
};
