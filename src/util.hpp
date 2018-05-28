#pragma once

#include "common.hpp"

#include <cctype>
#include <stdexcept>
#include <type_traits>

template<typename T>
inline typename std::enable_if<std::is_arithmetic<T>::value, T>::type alignUp(T x, size_t alignment)
{
	const T mask = ~(static_cast<T>(alignment) - 1);
	return (x + (alignment - 1)) & mask;
}

template<typename T>
inline typename std::enable_if<std::is_arithmetic<T>::value, T>::type alignDown(T x, size_t alignment)
{
	const T mask = ~(static_cast<T>(alignment) - 1);
	return x & mask;
}

template<typename T>
inline const T* advancePointer(const T* data, intptr_t count)
{
	return reinterpret_cast<const T*>(
		reinterpret_cast<const char*>(data) + count
	);
}

template<typename T>
inline void advancePointerInplace(const T*& buffer, size_t count)
{
	buffer = advancePointer(buffer, count);
}

template<typename T, typename = typename std::enable_if_t<std::is_unsigned<T>::value>>
inline constexpr T rotateLeft(const T x, unsigned n)
{
	const unsigned bitCount = sizeof(T) * CHAR_BIT;
	return (x << n) | (x >> (bitCount - n));
}

inline int charToInt(int x)
{
	if (x >= '0' && x <= '9')
		return (x - '0');
	if (x >= 'A' && x <= 'F')
		return (x - 'A' + 10);
	if (x >= 'a' && x <= 'f')
		return (x - 'a' + 10);
	throw std::invalid_argument("Unexpected character");
}

inline size_t parseHexString(const char* str, uint8_t* data, size_t maxSize)
{
	size_t n;
	
	for (n = 0; str[0] && str[1] && n < maxSize; ) {
		if (std::isspace(*str)) {
			++str;
			continue;
		}
		
		*data++ = static_cast<uint8_t>((charToInt(str[0]) << 4) + charToInt(str[1]));
		str += 2;
		++n;
	}
	
	return n;
}

inline size_t parseHexString(const std::string& str, uint8_t* data, size_t maxSize)
{
	return parseHexString(str.c_str(), data, maxSize);
}
