#pragma once

#include "io_util.hpp"
#include "debug.hpp" // TODO: temporarily

#include <algorithm>
#include <iostream> // TODO: temporarily
#include <string> // TODO: temporarily

#include <boost/noncopyable.hpp>

static inline uint16_t getBitsAt(const uint8_t* data, uint32_t offset)
{
	const auto offsetAligned = (offset * 12) / 8;
	auto result = readAtWithByteSwap<uint16_t>(data, offsetAligned);
	if ((offset & 0x1) == 0)
		result >>= 4;
	return result & UINT16_C(0xFFF);
}

static inline uint64_t decodeBitsAndAdvance(const uint8_t*& data)
{
	uint64_t value = *data++;
	uint64_t mask = 0x80;
	while (value & mask) {
		value = ((value - mask) << 8) | (*data++);
		mask <<= 7;
	}
	return value;
}

template<typename Derived, typename Key>
class BTree
{
private:
	struct SearchResult
	{
		SearchResult()
			: lowerBound(0)
			, upperBound(0)
			, index(INVALID_INDEX)
			, maxIndex(0)
		{
		}
		
		unsigned int lowerBound;
		unsigned int upperBound;
		
		unsigned int index;
		unsigned int maxIndex;
	};
	
	typedef int (Derived::*KeyCompareOp)(const Key& key, const uint8_t* data) const;
	
public:
	typedef Key KeyType;

	enum class CallbackResult
	{
		STOP,
		CONTINUE,
	};

	static const auto INVALID_INDEX = ~0u;
	
	const uint8_t* getByIndex(unsigned int index) const
	{
		const auto& self = static_cast<const Derived&>(*this);
		
		unsigned int childNodeCount = readAtWithByteSwap<uint8_t>(m_data, 0);
		
		unsigned int nodeDataOffset = readAtWithByteSwap<uint32_t>(m_data, 0) & UINT32_C(0xFFFFFF);
		unsigned int nodeOffset;
		
		const uint8_t* nodeData = advancePointer(m_data, nodeDataOffset);
		const uint8_t* node;
		
		unsigned int startKeyIndex = 0, nextKeyIndex, keyCount;
		bool found = false;
		for (auto i = childNodeCount; i != 0 && !found; --i) {
			keyCount = getBitsAt(nodeData, 0) & 0x7FFu;
			node = nullptr;
			for (auto j = 0u; j < keyCount; ++j) {
				nodeOffset = getBitsAt(nodeData, j + 1);
				node = advancePointer(nodeData, nodeOffset);
				nextKeyIndex = static_cast<uint32_t>(decodeBitsAndAdvance(node));
				if (index < nextKeyIndex) {
					found = true;
					break;
				}
				startKeyIndex = nextKeyIndex;
			}
			if (!node)
				break;
				
			node = self.advanceData(node);
			
			nodeDataOffset = static_cast<uint32_t>(decodeBitsAndAdvance(node));
			nodeData = advancePointer(m_data, nodeDataOffset);
		}
		
		keyCount = getBitsAt(nodeData, 0) & 0x7FFu;
		nodeOffset = getBitsAt(nodeData, (index - startKeyIndex) + 1);
		node = advancePointer(nodeData, nodeOffset);
		
		return node;
	}
	
	bool getByIndex(unsigned int index, Key& key) const
	{
		const auto& self = static_cast<const Derived&>(*this);
		const uint8_t* data = getByIndex(index);
		if (!data)
			return false;
		return self.parseData(key, data) != nullptr;
	}
	
	bool searchByIndexOldest(unsigned int index, Key& key) const
	{
		return getByIndex(index, key);
	}

	bool searchByIndex(unsigned int index, Key& key) const
	{
		const auto& self = static_cast<const Derived&>(*this);

		auto p = m_data;
		
		const auto offsetAndCount = readNextWithByteSwap<uint32_t>(p);
		const auto nodeCount = readNextWithByteSwap<uint16_t>(p);
		
		UNUSED(offsetAndCount);
		
		for (auto i = 0u; i < nodeCount; ++i) {
			const auto high = getBitsAt(p, 0) & UINT32_C(0x7FF);
			const auto nextOffset = getBitsAt(p, high + 1);

			if (index < high)
				break;
			index -= high;

			p = advancePointer(p, nextOffset);
		}
		
		const auto offset = getBitsAt(p, index + 1);
		p = advancePointer(p, offset);
		
		return self.parseData(key, p) != nullptr;
	}
	
	unsigned int searchByKey(Key& key) const
	{
		const auto& self = static_cast<const Derived&>(*this);
		
		const auto count = static_cast<unsigned int>(readAtWithByteSwap<uint8_t>(m_data, 0));
		const auto offset = readAtWithByteSwap<uint32_t>(m_data, 0) & UINT32_C(0xFFFFFF);
		
		auto data = advancePointer(m_data, offset);
		SearchResult result;
		for (auto i = count; i != 0; i--) {
			data = searchWithComparison(result, data, count, key, &Derived::lessThanKeyCompareOp);
			if (!data)
				goto done;
			
			result.maxIndex = static_cast<uint32_t>(decodeBitsAndAdvance(data));
			
			data = self.advanceData(data);
			
			const auto nextOffset = static_cast<uint32_t>(decodeBitsAndAdvance(data));
			data = advancePointer(m_data, nextOffset);
		}
		
		data = searchWithComparison(result, data, 0, key, &Derived::equalKeyCompareOp);

	done:
		if (count == 0)
			result.upperBound = 0;
			
		if (data) {
			const auto index = (result.maxIndex - result.upperBound + result.lowerBound);
			self.parseData(key, data);
			return index;
		} else {
			return INVALID_INDEX;
		}
	}
	
	template<typename TraverseFunctor>
	unsigned int traverse(TraverseFunctor& traverseFunctor) const
	{
		const auto& self = static_cast<const Derived&>(*this);

		auto p = m_data;
		
		const auto offsetAndCount = readNextWithByteSwap<uint32_t>(p);
		const auto nodeCount = readNextWithByteSwap<uint16_t>(p);
		
		UNUSED(offsetAndCount);
		
		auto visitedKeyCount = 0u;
		for (auto i = 0u; i < nodeCount; ++i) {
			const auto high = getBitsAt(p, 0) & UINT32_C(0x7FF);
			const auto nextOffset = getBitsAt(p, high + 1);
			
			for (auto j = 0u; j < high; ++j) {
				const auto offset = getBitsAt(p, j + 1);
				const auto data = advancePointer(p, offset);
				
				Key key;
				if (self.parseData(key, data)) {
					++visitedKeyCount;
					if (!traverseFunctor(key))
						goto done;
				}
			}
			
			p = advancePointer(p, nextOffset);
		}
		
	done:
		return visitedKeyCount;
	}
	
protected:
	explicit BTree(const uint8_t* data)
		: m_data(data)
	{
	}
	
	const uint8_t* searchWithComparison(SearchResult& result, const uint8_t* data, unsigned int count, const Key& key, KeyCompareOp compOp) const
	{
		const auto& self = static_cast<const Derived&>(*this);

		auto high = getBitsAt(data, 0) & UINT32_C(0x7FF), low = 0u;
		auto index = 0u;
		const uint8_t* subData;
		
		result.upperBound = high;

		while (low < high) {
			const auto mid = low + (high - low) / 2;
			index = mid + 1;

			const auto offset = getBitsAt(data, index);
			subData = advancePointer(data, offset);

			const auto ret = (self.*compOp)(key, subData);
			if (ret == 0) {
				result.lowerBound = mid;
				result.index = mid;
				goto done;
			} else if (ret > 0) {
				low = index;
			} else if (ret < 0) {
				high = mid;
				index = mid;
			}
		}
		
		subData = nullptr;

		result.lowerBound = index;
		result.index = INVALID_INDEX;
		
		if (count != 0 && index != result.upperBound) {
			const auto offset = getBitsAt(data, index + 1);
			subData = advancePointer(data, offset);
		}
		
	done:
		return subData;
	}
	
	const uint8_t* parseData(KeyType& key, const uint8_t* data) const { return nullptr; }
	const uint8_t* advanceData(const uint8_t* data) const { return nullptr; }

	int equalKeyCompareOp(const KeyType& key, const uint8_t* data) const { return -1; }
	int lessThanKeyCompareOp(const KeyType& key, const uint8_t* data) const { return -1; }

	CallbackResult traverseCallback(const uint8_t* data) const { return CallbackResult::CONTINUE; }
	
	const uint8_t* m_data;
};

class StringKey
{
public:
	StringKey()
		: m_value(nullptr)
		, m_length(0)
	{
	}
	
	explicit StringKey(const char* value, uint32_t length = ~0u)
		: StringKey()
	{
		setValue(value);
		
		if (length == ~0u)
			length = static_cast<uint32_t>(strlen(value));
		setLength(length);
	}

	StringKey(const StringKey& other) = default;
	
	void dump() const;

	StringKey& setValue(const char* value)
	{
		m_value = value;
		return *this;
	}

	const auto* value() const { return m_value; }
	
	StringKey& setLength(uint32_t length)
	{
		m_length = length;
		return *this;
	}

	auto length() const { return m_length; }

private:
	const char* m_value;
	unsigned int m_length;
};

class EntryKey
{
public:
	static const auto INVALID_INDEX = ~0u;
	
	static const auto FLAG_DIRECTORY = (1u << 0);
	static const auto FLAG_FILE = (1u << 1);

	EntryKey()
		: m_flags(0)
		, m_nameIndex(INVALID_INDEX)
		, m_extIndex(INVALID_INDEX)
		, m_linkIndex(INVALID_INDEX)
	{
	}
	
	explicit EntryKey(uint32_t nameIndex, uint32_t extIndex = 0)
		: EntryKey()
	{
		setNameIndex(nameIndex);
		setExtIndex(extIndex);
	}
	
	EntryKey(const EntryKey& other) = default;

	void dump() const;

	EntryKey& setFlags(uint32_t flags)
	{
		m_flags = flags;
		return *this;
	}

	auto flags() const { return m_flags; }
	
	EntryKey& setNameIndex(uint32_t index)
	{
		m_nameIndex = index;
		return *this;
	}

	auto nameIndex() const { return m_nameIndex; }
	
	// If no extension is needed then index should be 0.
	EntryKey& setExtIndex(uint32_t index)
	{
		m_extIndex = index;
		return *this;
	}

	auto extIndex() const { return m_extIndex; }
	
	EntryKey& setLinkIndex(uint32_t index)
	{
		m_linkIndex = index;
		return *this;
	}

	// For directory entries it points to next entry tree index, for file entries it points to node index.
	auto linkIndex() const { return m_linkIndex; }

	bool isDirectory() const { return (m_flags & FLAG_DIRECTORY) != 0; }
	bool isFile() const { return (m_flags & FLAG_FILE) != 0; }

private:
	uint32_t m_flags;
	uint32_t m_nameIndex;
	uint32_t m_extIndex;
	uint32_t m_linkIndex;
};

class NodeKey
{
public:
	static const auto INVALID_INDEX = ~0u;
	
	static const auto FLAG_COMPRESSED = (1u << 0);
	static const auto FLAG_BIT1 = (1u << 1); // TODO: figure out
	static const auto FLAG_BIT4 = (1u << 4); // TODO: figure out
	static const auto FLAG_BIT5 = (1u << 5); // TODO: figure out
	static const auto FLAG_BIT0123 = 0xFu; // TODO: figure out
	static const auto FLAG_0x13 = FLAG_COMPRESSED | FLAG_BIT1 | FLAG_BIT4;

	NodeKey()
		: m_flags(0)
		, m_nodeIndex(INVALID_INDEX)
		, m_size1(0)
		, m_size2(0)
		, m_volumeIndex(INVALID_INDEX)
		, m_sectorIndex(0)
	{
	}
	
	explicit NodeKey(uint32_t nodeIndex)
		: NodeKey()
	{
		setNodeIndex(nodeIndex);
	}

	NodeKey(const NodeKey& other) = default;

	void dump() const;

	NodeKey& setFlags(uint32_t flags)
	{
		m_flags = flags;
		return *this;
	}

	auto flags() const { return m_flags; }
	
	NodeKey& setNodeIndex(uint32_t index)
	{
		m_nodeIndex = index;
		return *this;
	}

	auto nodeIndex() const { return m_nodeIndex; }
	
	NodeKey& setSize1(uint32_t size)
	{
		m_size1 = size;
		return *this;
	}
	
	auto size1() const { return m_size1; }
	
	NodeKey& setSize2(uint32_t size)
	{
		m_size2 = size;
		return *this;
	}

	auto size2() const { return m_size2; }
	
	NodeKey& setVolumeIndex(uint32_t index)
	{
		m_volumeIndex = index;
		return *this;
	}

	auto volumeIndex() const { return m_volumeIndex; }
	
	NodeKey& setSectorIndex(uint32_t index)
	{
		m_sectorIndex = index;
		return *this;
	}
	
	auto sectorIndex() const { return m_sectorIndex; }
	
	// TODO: figure out
	bool hasCompression() const { return (m_flags & FLAG_COMPRESSED) != 0; }
	bool hasBit4() const { return (m_flags & FLAG_BIT4) != 0; } // TODO: figure out
	bool hasBit5() const { return (m_flags & FLAG_BIT5) != 0; } // TODO: figure out
	bool hasBits0123() const { return (m_flags & FLAG_BIT0123) != 0; } // TODO: figure out

private:
	uint32_t m_flags;
	uint32_t m_nodeIndex;
	uint32_t m_size1;
	uint32_t m_size2;
	uint32_t m_volumeIndex;
	uint32_t m_sectorIndex;
};

class StringBTree
	: public BTree<StringBTree, StringKey>
{
	friend class BTree<StringBTree, StringKey>;

public:
	explicit StringBTree(const uint8_t* data)
		: BTree(data)
	{
	}

protected:
	const uint8_t* parseData(KeyType& key, const uint8_t* data) const
	{
		const auto length = static_cast<uint32_t>(decodeBitsAndAdvance(data));
		key.setLength(length);

		const auto value = reinterpret_cast<const char*>(data);
		key.setValue(value);

		data += length;
		
		return data;
	}

	const uint8_t* advanceData(const uint8_t* data) const
	{
		const auto length = static_cast<uint32_t>(decodeBitsAndAdvance(data));
		data += length;
		return data;
	}
	
	int equalKeyCompareOp(const KeyType& key, const uint8_t* data) const
	{
		const auto curLength = key.length();
		const auto curValue = key.value();
		const auto otherLength = static_cast<uint32_t>(decodeBitsAndAdvance(data));
		const auto otherValue = reinterpret_cast<const char*>(data);
		const auto minLength = std::min(key.length(), otherLength);
		
		for (auto i = 0u; i < minLength; ++i) {
			if (curValue[i] < otherValue[i])
				return -1;
			else if (curValue[i] > otherValue[i])
				return 1;
		}
		
		if (curLength < otherLength)
			return -1;
		else if (curLength > otherLength)
			return 1;

		return 0;
	}
	
	int lessThanKeyCompareOp(const KeyType& key, const uint8_t* data) const
	{
		// TODO: figure out
		const auto unk = static_cast<uint32_t>(decodeBitsAndAdvance(data));

		const auto result = equalKeyCompareOp(key, data);
		
		return (result != 0) ? result : 1;
	}
	
	CallbackResult traverseCallback(const uint8_t* data) const;
};

class EntryBTree
	: public BTree<EntryBTree, EntryKey>
{
	friend class BTree<EntryBTree, EntryKey>;

public:
	explicit EntryBTree(const uint8_t* data)
		: BTree(data)
	{
	}

protected:
	const uint8_t* parseData(KeyType& key, const uint8_t* data) const
	{
		const auto flags = readNext<uint8_t>(data);
		key.setFlags(flags);

		const auto nameIndex = static_cast<uint32_t>(decodeBitsAndAdvance(data));
		key.setNameIndex(nameIndex);

		const auto extIndex = key.isFile() ? static_cast<uint32_t>(decodeBitsAndAdvance(data)) : 0;
		key.setExtIndex(extIndex);

		const auto linkIndex = static_cast<uint32_t>(decodeBitsAndAdvance(data));
		key.setLinkIndex(linkIndex);
		
		return data;
	}

	const uint8_t* advanceData(const uint8_t* data) const
	{
		// TODO: figure out
		const auto unk = static_cast<uint32_t>(decodeBitsAndAdvance(data));

		return data;
	}
	
	int equalKeyCompareOp(const KeyType& key, const uint8_t* data) const
	{
		const auto flags = readNext<uint8_t>(data);
		
		const auto nameIndex = static_cast<uint32_t>(decodeBitsAndAdvance(data));
		if (key.nameIndex() < nameIndex)
			return -1;
		else if (key.nameIndex() > nameIndex)
			return 1;
		
		const auto extIndex = (flags & EntryKey::FLAG_FILE) != 0 ? static_cast<uint32_t>(decodeBitsAndAdvance(data)) : 0;
		if (key.extIndex() < extIndex)
			return -1;
		else if (key.extIndex() > extIndex)
			return 1;
			
		return 0;
	}
	
	int lessThanKeyCompareOp(const KeyType& key, const uint8_t* data) const
	{
		const auto nameIndex = static_cast<uint32_t>(decodeBitsAndAdvance(data));
		if (key.nameIndex() < nameIndex)
			return -1;
		else if (key.nameIndex() > nameIndex)
			return 1;
		
		const auto extIndex = static_cast<uint32_t>(decodeBitsAndAdvance(data));
		if (key.extIndex() < extIndex)
			return -1;
		else if (key.extIndex() > extIndex)
			return 1;
		else
			UNREACHABLE_CODE(0xDEAD);
	}

	CallbackResult traverseCallback(const uint8_t* data) const;
};

class NodeBTree
	: public BTree<NodeBTree, NodeKey>
{
	friend class BTree<NodeBTree, NodeKey>;

public:
	explicit NodeBTree(const uint8_t* data, bool hasMultipleVolumes)
		: BTree(data)
		, m_hasMultipleVolumes(hasMultipleVolumes)
	{
	}

protected:
	const uint8_t* parseData(KeyType& key, const uint8_t* data) const
	{
		const auto flags = readNext<uint8_t>(data);
		key.setFlags(flags);
		
		const auto nodeIndex = static_cast<uint32_t>(decodeBitsAndAdvance(data));
		key.setNodeIndex(nodeIndex);

		const auto size1 = static_cast<uint32_t>(decodeBitsAndAdvance(data));
		key.setSize1(size1);
		
		// TODO: figure out
		const auto size2 = key.hasBits0123() ? static_cast<uint32_t>(decodeBitsAndAdvance(data)) : size1;
		key.setSize2(size2);
		
		if (m_hasMultipleVolumes) {
			const auto volumeIndex = static_cast<uint32_t>(decodeBitsAndAdvance(data));
			key.setVolumeIndex(volumeIndex);
		} else {
			key.setVolumeIndex(0);
		}
		
		const auto sectorIndex = static_cast<uint32_t>(decodeBitsAndAdvance(data));
		key.setSectorIndex(sectorIndex);
		
		return data;
	}

	const uint8_t* advanceData(const uint8_t* data) const
	{
		return data;
	}
	
	int equalKeyCompareOp(const KeyType& key, const uint8_t* data) const
	{
		const auto flags = readNext<uint8_t>(data);
		UNUSED(flags);

		const auto nodeIndex = static_cast<uint32_t>(decodeBitsAndAdvance(data));
		if (key.nodeIndex() < nodeIndex)
			return -1;
		else if (key.nodeIndex() > nodeIndex)
			return 1;

		return 0;
	}
	
	int lessThanKeyCompareOp(const KeyType& key, const uint8_t* data) const
	{
		const auto nodeIndex = static_cast<uint32_t>(decodeBitsAndAdvance(data));
		if (key.nodeIndex() < nodeIndex)
			return -1;
		else if (key.nodeIndex() > nodeIndex)
			return 1;
		else
			UNREACHABLE_CODE(0xDEAD);
	}

	CallbackResult traverseCallback(const uint8_t* data) const;
	
	bool m_hasMultipleVolumes;
};
