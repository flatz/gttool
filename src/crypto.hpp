#pragma once

#include "crc.hpp"
#include "util.hpp"

#include <algorithm>
#include <array>
#include <string>
#include <type_traits>

#include <boost/endian/conversion.hpp>

class Keyset
{
public:
	typedef std::array<uint32_t, 4> Key;
	
	Keyset(const std::string& magic, const Key& key)
		: m_magic(magic)
		, m_key(key)
	{
	}

	const Key computeKey(uint32_t seed) const
	{
		const auto c0 = (~crc32_0x04C11DB7(m_magic.begin(), m_magic.end(), 0)) ^ seed;

		const auto c1 = invXorShift(c0, m_key[0]);
		const auto c2 = invXorShift(c1, m_key[1]);
		const auto c3 = invXorShift(c2, m_key[2]);
		const auto c4 = invXorShift(c3, m_key[3]);

		return Key({{
			c1 & ((1 << 17) - 1),
			c2 & ((1 << 19) - 1),
			c3 & ((1 << 23) - 1),
			c4 & ((1 << 29) - 1),
		}});
	}

	template<
		typename InputIt, typename OutputIt,
		typename = typename std::enable_if_t<
			std::is_same<typename std::iterator_traits<InputIt>::value_type, uint8_t>::value &&
			std::is_same<typename std::iterator_traits<OutputIt>::value_type, uint8_t>::value
		>
	>
	void cryptBytes(InputIt srcFirst, InputIt srcLast, OutputIt dstFirst, uint32_t seed) const
	{
		typedef typename std::iterator_traits<InputIt>::value_type ValueType;

		auto c = computeKey(seed);

		std::transform(
			srcFirst, srcLast, dstFirst,
			[&c](const ValueType in) {
				const ValueType out = (((c[0] ^ c[1]) ^ in) ^ (c[2] ^ c[3])) & UINT8_C(0xFF);

				c[0] = ((rotateLeft(c[0], 9) & UINT32_C(0x1FE00)) | (c[0] >> 8));
				c[1] = ((rotateLeft(c[1], 11) & UINT32_C(0x7F800)) | (c[1] >> 8));
				c[2] = ((rotateLeft(c[2], 15) & UINT32_C(0x7F8000)) | (c[2] >> 8));
				c[3] = ((rotateLeft(c[3], 21) & UINT32_C(0x1FE00000)) | (c[3] >> 8));

				return out;
			}
		);
	}

	template<typename InputIt, typename OutputIt>
	static void cryptBlocks(InputIt srcFirst, InputIt srcLast, OutputIt dstFirst)
	{
		cryptBlocksInternal<InputIt, OutputIt, false>(srcFirst, srcLast, dstFirst);
	}

	template<typename InputIt, typename OutputIt>
	static void cryptBlocksWithSwapEndian(InputIt srcFirst, InputIt srcLast, OutputIt dstFirst)
	{
		cryptBlocksInternal<InputIt, OutputIt, true>(srcFirst, srcLast, dstFirst);
	}
	
	const auto& magic() const { return m_magic; }

	const auto& key() const { return m_key; }
	auto key(size_t i) const { return m_key[i]; }

private:
	static uint32_t xorShift(uint32_t x, uint32_t y)
	{
		auto result = x;
		const auto count = sizeof(x) * CHAR_BIT;
		for (auto i = 0u; i < count; ++i) {
			const auto hasUpperBit = (result & UINT32_C(0x80000000)) != 0;
			result <<= 1;
			if (hasUpperBit) {
				result ^= y;
			}
		}
		return result;
	}

	static uint32_t invXorShift(uint32_t x, uint32_t y)
	{
		return ~xorShift(x, y);
	}

	static uint32_t shuffleBits(uint32_t x)
	{
		auto crc = UINT32_C(0);
		for (auto i = 0; i < 4; ++i) {
			crc = (crc << 8) ^ g_crc32_0x04C11DB7[(rotateLeft(x ^ crc, 10) & 0x3FC) >> 2];
			x <<= 8;
		}
		return ~crc;
	}

	static uint32_t cryptBlock(uint32_t x, uint32_t y)
	{
		return x ^ shuffleBits(y);
	}

	template<
		typename InputIt, typename OutputIt, bool NeedSwapEndian,
		typename = typename std::enable_if_t<
			std::is_same<typename std::iterator_traits<InputIt>::value_type, uint32_t>::value &&
			std::is_same<typename std::iterator_traits<OutputIt>::value_type, uint32_t>::value
		>
	>
	static void cryptBlocksInternal(InputIt srcFirst, InputIt srcLast, OutputIt dstFirst)
	{
		if (srcFirst == srcLast) {
			return;
		}

		typedef typename std::iterator_traits<InputIt>::value_type ValueType;
		using boost::endian::endian_reverse;

		ValueType prevBlock = endian_reverse(*srcFirst++);
		*dstFirst++ = (NeedSwapEndian ? prevBlock : endian_reverse(prevBlock));

		if (srcFirst == srcLast) {
			return;
		}

		std::transform(
			srcFirst, srcLast, dstFirst,
			[&prevBlock](const ValueType in) {
				const ValueType curBlock = endian_reverse(in);
				ValueType out = cryptBlock(curBlock, prevBlock);
				if (!NeedSwapEndian) {
					out = endian_reverse(out);
				}
				prevBlock = curBlock;
				return out;
			}
		);
	}

	const std::string m_magic;
	const Key m_key;
};

class Salsa20Cipher
{
public:
	static const auto STATE_SIZE = 16u;
	static const auto BLOCK_SIZE = 64u;
	static const auto KEY_MAX_SIZE = 32u;

	Salsa20Cipher(const uint8_t key[KEY_MAX_SIZE], size_t keySize, const uint8_t* iv = nullptr)
		: m_state()
	{
		setKey(key, keySize);
		setIv(iv);
	}
	
	explicit Salsa20Cipher(const std::string& key, const uint8_t* iv = nullptr)
		: Salsa20Cipher(reinterpret_cast<const uint8_t*>(key.c_str()), key.size(), iv)
	{
	}
	
	void setKey(const uint8_t key[KEY_MAX_SIZE], size_t keySize)
	{
		const auto sz = sizeof(uint32_t);
		
		uint8_t paddedKey[KEY_MAX_SIZE] = { 0 };
		std::copy(key, key + keySize, paddedKey);
		
		if (keySize > KEY_MAX_SIZE / 2) {
			static const auto magic = reinterpret_cast<const uint8_t*>("expand 32-byte k");
			
			bytesToUint(magic + 0 * sz, m_state[0]);
			bytesToUint(paddedKey + 0 * sz, m_state[1]);
			bytesToUint(paddedKey + 1 * sz, m_state[2]);
			bytesToUint(paddedKey + 2 * sz, m_state[3]);
			bytesToUint(paddedKey + 3 * sz, m_state[4]);
			bytesToUint(magic + 1 * sz, m_state[5]);
			bytesToUint(magic + 2 * sz, m_state[10]);
			bytesToUint(paddedKey + 4 * sz, m_state[11]);
			bytesToUint(paddedKey + 5 * sz, m_state[12]);
			bytesToUint(paddedKey + 6 * sz, m_state[13]);
			bytesToUint(paddedKey + 7 * sz, m_state[14]);
			bytesToUint(magic + 3 * sz, m_state[15]);
		} else {
			static const auto magic = reinterpret_cast<const uint8_t*>("expand 16-byte k");
			
			bytesToUint(magic + 0 * sz, m_state[0]);
			bytesToUint(paddedKey + 0 * sz, m_state[1]);
			bytesToUint(paddedKey + 1 * sz, m_state[2]);
			bytesToUint(paddedKey + 2 * sz, m_state[3]);
			bytesToUint(paddedKey + 3 * sz, m_state[4]);
			bytesToUint(magic + 1 * sz, m_state[5]);
			bytesToUint(magic + 2 * sz, m_state[10]);
			bytesToUint(paddedKey + 0 * sz, m_state[11]);
			bytesToUint(paddedKey + 1 * sz, m_state[12]);
			bytesToUint(paddedKey + 2 * sz, m_state[13]);
			bytesToUint(paddedKey + 3 * sz, m_state[14]);
			bytesToUint(magic + 3 * sz, m_state[15]);
		}
	}
	
	void setIv(const uint8_t* iv)
	{
		if (iv) {
			const auto sz = sizeof(uint32_t);
			
			bytesToUint(iv + 0 * sz, m_state[6]);
			bytesToUint(iv + 1 * sz, m_state[7]);
			
			m_state[8] = m_state[9] = 0;
		} else {
			m_state[6] = m_state[7] = m_state[8] = m_state[9] = 0;
		}
	}

	void processBlocks(const uint8_t* in, uint8_t* out, size_t blockCount)
	{
		uint8_t keyStream[BLOCK_SIZE];

		for (auto i = 0u; i < blockCount; ++i) {
			generateKeyStream(keyStream);

			for (auto j = 0u; j < BLOCK_SIZE; ++j) {
				*out++ = keyStream[j] ^ *in++;
			}
		}
	}
	
	void processBytes(const uint8_t* in, uint8_t* out, size_t byteCount)
	{
		uint8_t keyStream[BLOCK_SIZE];

		while (byteCount != 0) {
			generateKeyStream(keyStream);

			const auto count = byteCount < BLOCK_SIZE ? byteCount : BLOCK_SIZE;
			for (auto i = 0u; i < count; ++i, --byteCount)
				*out++ = keyStream[i] ^ *in++;
		}
	}

private:
	struct MatrixElement
	{
		const uint8_t outIndex;
		const uint8_t term1Index;
		const uint8_t term2Index;
		const uint8_t shift;
	};
	
	static const std::array<MatrixElement, 32> MATRIX;

	void generateKeyStream(uint8_t keyStream[BLOCK_SIZE])
	{
		std::array<uint32_t, STATE_SIZE> newState = m_state;

		for (auto i = 20; i > 0; i -= 2) {
			for (const auto& el: MATRIX) {
				newState[el.outIndex] ^= rotateLeft(newState[el.term1Index] + newState[el.term2Index], el.shift);
			}
		}
		
		const auto sz = sizeof(uint32_t);
		for (auto i = 0u; i < STATE_SIZE; ++i) {
			newState[i] += m_state[i];
			uintToBytes(keyStream + i * sz, newState[i]);
		}
		
		m_state[8]++;
		m_state[9] += (m_state[8] == 0) ? 1 : 0;
	}
	
	static uint8_t* uintToBytes(uint8_t* bytes, uint32_t value)
	{
		*bytes++ = static_cast<uint8_t>(value >> 0);
		*bytes++ = static_cast<uint8_t>(value >> 8);
		*bytes++ = static_cast<uint8_t>(value >> 16);
		*bytes++ = static_cast<uint8_t>(value >> 24);
		
		return bytes;
	}
	
	static const uint8_t* bytesToUint(const uint8_t* bytes, uint32_t& value)
	{
		value = static_cast<uint32_t>(*bytes++) << 0;
		value |= static_cast<uint32_t>(*bytes++) << 8;
		value |= static_cast<uint32_t>(*bytes++) << 16;
		value |= static_cast<uint32_t>(*bytes++) << 24;
		
		return bytes;
	}
	
	std::array<uint32_t, STATE_SIZE> m_state;
};