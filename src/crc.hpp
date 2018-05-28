#pragma once

#include "common.hpp"

#include <algorithm>
#include <array>
#include <iterator>
#include <numeric>

extern const std::array<uint32_t, 256> g_crc32_0x04C11DB7;

template<typename InputIt>
inline uint32_t crc32_0x04C11DB7(InputIt first, InputIt last, uint32_t initial)
{
	typedef typename std::iterator_traits<InputIt>::value_type ValueType;

	return std::accumulate(
		first, last,
		initial,
		[](uint32_t& crc, const ValueType& data) -> uint32_t {
			const uint8_t byte = data & 0xFF;
			crc = (crc << 8) ^ g_crc32_0x04C11DB7[(crc >> 24) ^ byte];
			return crc;
		}
	);
}
