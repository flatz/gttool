#pragma once

#include "util.hpp"

#include <cstring>
#include <vector>

#include <boost/endian/conversion.hpp>

template<typename T, typename CharT>
inline T read(const CharT* buffer)
{
	T value;
	std::memcpy(&value, buffer, sizeof(value));
	return value;
}

template<typename T, typename CharT>
inline T* readN(const CharT* buffer, T* data, size_t count)
{
	std::memcpy(data, buffer, count);
	return data;
}

template<typename T, typename CharT>
inline T readNext(const CharT*& buffer)
{
	const T value = read<T, CharT>(buffer);
	advancePointerInplace(buffer, sizeof(value));
	return value;
}

template<typename T, typename CharT>
inline T* readNextN(const CharT*& buffer, T* data, size_t count)
{
	data = readN<T, CharT>(buffer, data, count);
	advancePointerInplace(buffer, count);
	return data;
}

template<typename T, typename CharT>
inline T readAt(const CharT* buffer, size_t offset)
{
	T value;
	std::memcpy(&value, advancePointer(buffer, offset), sizeof(value));
	return value;
}

template<typename T, typename CharT>
inline T* readAtN(const CharT* buffer, size_t offset, T* data, size_t count)
{
	std::memcpy(data, advancePointer(buffer, offset), count);
	return data;
}

template<typename T, typename CharT>
inline T readWithByteSwap(const CharT* buffer)
{
	const T value = read<T, CharT>(buffer);
	return boost::endian::endian_reverse(value);
}

template<typename T, typename CharT>
inline T readNextWithByteSwap(const CharT*& buffer)
{
	const T value = readNext<T, CharT>(buffer);
	return boost::endian::endian_reverse(value);
}

template<typename T, typename CharT>
inline T readAtWithByteSwap(const CharT* buffer, size_t offset)
{
	const T value = readAt<T, CharT>(buffer, offset);
	return boost::endian::endian_reverse(value);
}

bool loadFromFile(const std::string& filePath, std::vector<uint8_t>& data);
bool saveToFile(const std::string& filePath, const void* data, size_t dataSize);
