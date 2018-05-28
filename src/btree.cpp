#include "btree.hpp"
#include "volume.hpp"

void StringKey::dump() const
{
	std::string str(value(), length());
	std::cout
		<< "Length: " << str.length() << std::endl
		<< "Value: " << str << std::endl
		<< std::endl;
}

StringBTree::CallbackResult StringBTree::traverseCallback(const uint8_t* data) const
{
	CallbackResult result = CallbackResult::CONTINUE;

	KeyType obj;
	if (parseData(obj, data)) {
		std::string str(obj.value(), obj.length());
		std::cout << str << std::endl;
	} else {
		std::cerr << "Cannot parse String object." << std::endl;
	}

	return result;
}

void EntryKey::dump() const
{
	std::cout
		<< std::hex
		<< "Flags: 0x" << flags() << std::endl
		<< "Name index: 0x" << nameIndex() << std::endl
		<< "Ext index: 0x" << extIndex() << std::endl
		<< "Link index: 0x" << linkIndex() << std::endl
		<< "Is directory: " << (isDirectory() ? "true" : "false") << std::endl
		<< "Is file: " << (isFile() ? "true" : "false") << std::endl
		<< std::dec << std::endl;
}

EntryBTree::CallbackResult EntryBTree::traverseCallback(const uint8_t* data) const
{
	CallbackResult result = CallbackResult::CONTINUE;

	KeyType obj;
	if (parseData(obj, data)) {
		std::cout << obj.nameIndex() << ":" << obj.extIndex() << ":" << obj.linkIndex() << std::endl;
	} else {
		std::cerr << "Cannot parse Entry object." << std::endl;
	}

	return result;
}

void NodeKey::dump() const
{
	std::cout
		<< std::hex
		<< "Flags: 0x" << flags() << std::endl
		<< "Size1: 0x" << size1() << std::endl
		<< "Size2: 0x" << size2() << std::endl
		<< "Node index: 0x" << nodeIndex() << std::endl
		<< "Volume index: 0x" << volumeIndex() << std::endl
		<< "Sector index: 0x" << sectorIndex() << std::endl
		<< "Has compression: " << (hasCompression() ? "true" : "false") << std::endl
		<< "Has bit 4: " << (hasBit4() ? "true" : "false") << std::endl
		<< "Has bit 5: " << (hasBit5() ? "true" : "false") << std::endl
		<< std::dec << std::endl;
}

NodeBTree::CallbackResult NodeBTree::traverseCallback(const uint8_t* data) const
{
	CallbackResult result = CallbackResult::CONTINUE;

	KeyType obj;
	if (parseData(obj, data)) {
		std::cout << obj.nodeIndex() << ":" << obj.volumeIndex() << ":" << obj.size1() << ":" << obj.size2() << std::endl;
	} else {
		std::cerr << "Cannot parse Node object." << std::endl;
	}

	return result;
}


