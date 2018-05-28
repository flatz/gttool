#include "volume.hpp"
#include "compression.hpp"
#include "debug.hpp"

#include <algorithm>
#include <iostream>
#include <fstream>

#include <boost/algorithm/string.hpp>
#include <boost/format.hpp>

static inline bool prepareStream(std::ifstream& stream, const std::string& filePath, uint64_t* fileSize = nullptr)
{
	stream.open(filePath, std::ifstream::in | std::ifstream::binary);
	if (!stream.is_open()) {
		return false;
	}

	if (fileSize) {
		stream.seekg(0, std::ios_base::end);
		*fileSize = static_cast<uint64_t>(stream.tellg());
		stream.seekg(0, std::ios_base::beg);
	}

	return true;
}

bool VolumeFile::load(const std::string& filePath)
{
	m_origPath = boost::filesystem::path(filePath);
	m_basePath = m_origPath.parent_path();
	m_baseName = m_origPath.filename();

	if (!prepareStream(m_mainStream, filePath, &m_mainFileSize)) {
		return false;
	}

	std::vector<uint8_t> headerData;
	if (!readDataAt(headerData, 0, getHeaderSize())) {
		return false;
	}
	if (!decryptHeader(headerData.data(), headerData.size())) {
		return false;
	}
	if (!parseHeader(headerData.data(), headerData.size())) {
		return false;
	}
	if (!parseSegment()) {
		return false;
	}

	return true;
}

bool VolumeFile::readDataAt(std::ifstream& stream, std::vector<uint8_t>& data, uint64_t offset, uint64_t size)
{
	data.resize(size);

	stream.seekg(offset);
	stream.read(reinterpret_cast<char*>(data.data()), data.size());

	return true;
}

bool VolumeFile::parseSegment()
{
	const auto* p = m_data.data();

	const auto segmentMagic = VOLUME_READ_NEXT_SELF(p, uint32_t);
	if (segmentMagic != SEGMENT_MAGIC) {
		return false;
	}

	m_nameTreeOffset = VOLUME_READ_NEXT_SELF(p, uint32_t);
	m_extTreeOffset = VOLUME_READ_NEXT_SELF(p, uint32_t);
	m_nodeTreeOffset = VOLUME_READ_NEXT_SELF(p, uint32_t);
	m_entryTreeCount = VOLUME_READ_NEXT_SELF(p, uint32_t);

	m_entryTreeOffsets.resize(m_entryTreeCount);
	for (auto& offset: m_entryTreeOffsets) {
		offset = VOLUME_READ_NEXT_SELF(p, uint32_t);
	}

	return true;
}

unsigned int VolumeFile::getNodeByPath(const std::string& filePath, NodeKey& nodeKey) const
{
	if (m_entryTreeCount == 0) {
		return NodeBTree::INVALID_INDEX;
	}
	
	const auto normalizedFilePath = normalizeFilePath(filePath);
	std::vector<std::string> parts;
	boost::algorithm::split(
		parts,
		normalizedFilePath,
		boost::algorithm::is_any_of("/"),
		boost::algorithm::token_compress_on
	);

	if (parts.empty()) {
		return NodeBTree::INVALID_INDEX;
	}
	
	const StringBTree nameBtree(
		advancePointer(m_data.data(), nameTreeOffset())
	);
	const StringBTree extBtree(
		advancePointer(m_data.data(), extTreeOffset())
	);

	auto nodeIndex = NodeBTree::INVALID_INDEX;
	for (auto i = 0u, entryTreeIndex = 0u; i < parts.size(); ++i) {
		const auto& part = parts[i];
		const auto dotPos = part.find_last_of('.');
		
		const auto nameLength = (dotPos != std::string::npos) ? dotPos : part.size();
		StringKey nameKey(part.c_str(), static_cast<uint32_t>(nameLength));
		const auto nameIndex = nameBtree.searchByKey(nameKey);
		if (nameIndex == StringBTree::INVALID_INDEX) {
			break;
		}
		
		auto extIndex = 0u;
		if (dotPos != std::string::npos) {
			const auto extPart = part.substr(dotPos);
			const auto extLength = extPart.size();
			StringKey extKey(extPart.c_str(), static_cast<uint32_t>(extLength));
			extIndex = extBtree.searchByKey(extKey);
			if (extIndex == StringBTree::INVALID_INDEX) {
				break;
			}
		}
		
		const EntryBTree entryBtree(
			advancePointer(m_data.data(), entryTreeOffset(entryTreeIndex))
		);
		EntryKey entryKey(nameIndex, extIndex);
		const auto entryIndex = entryBtree.searchByKey(entryKey);
		if (entryIndex == EntryBTree::INVALID_INDEX) {
			break;
		}
		
		if (entryKey.isDirectory()) {
			entryTreeIndex = entryKey.linkIndex();
		} else {
			nodeIndex = entryKey.linkIndex();
			break;
		}
	}
	if (nodeIndex == NodeBTree::INVALID_INDEX) {
		return NodeBTree::INVALID_INDEX;
	}
	
	const NodeBTree nodeBtree(
		advancePointer(m_data.data(), nodeTreeOffset()),
		hasMultipleVolumes()
	);
	nodeKey = NodeKey(nodeIndex);
	nodeIndex = nodeBtree.searchByKey(nodeKey);
	if (nodeIndex == NodeBTree::INVALID_INDEX) {
		return NodeBTree::INVALID_INDEX;
	}
	
	return nodeIndex;
}

class EntryUnpacker
{
public:
	explicit EntryUnpacker(VolumeFile& volume, const std::string& outDirectory, const std::string& parentDirectory = std::string())
		: m_volume(volume)
		, m_outDirectory(outDirectory)
		, m_parentDirectory(parentDirectory)
	{
	}
	
	bool operator ()(const EntryKey& entryKey) const
	{
		const auto entryPath = m_volume.getEntryPath(entryKey, m_parentDirectory);
		if (entryPath.empty()) {
			std::cerr << "Cannot determine entry path." << std::endl;
			return false;
		}
		
		const auto fullEntryPath = boost::filesystem::path(m_outDirectory) / entryPath;
		
		if (entryKey.isDirectory()) {
			std::cout << "DIR:" << entryPath << std::endl;
			//entryKey.dump();
			
			boost::filesystem::create_directories(fullEntryPath);
			
			const EntryBTree childEntryBtree(
				advancePointer(m_volume.data().data(), m_volume.entryTreeOffset(entryKey.linkIndex()))
			);
			const EntryUnpacker childUnpacker(m_volume, m_outDirectory, entryPath);
			childEntryBtree.traverse(childUnpacker);
		} else {
			std::cout << "FILE:" << entryPath << std::endl;
			//entryKey.dump();
			
			const NodeBTree nodeBtree(
				advancePointer(m_volume.data().data(), m_volume.nodeTreeOffset()),
				m_volume.hasMultipleVolumes()
			);
			NodeKey nodeKey(entryKey.linkIndex());
			const auto nodeIndex = nodeBtree.searchByKey(nodeKey);
			bool unpacked = false;
			if (nodeIndex != NodeBTree::INVALID_INDEX) {
				if (m_volume.unpackNode(nodeKey, fullEntryPath.string())) {
					unpacked = true;
				}
			}
			if (!unpacked) {
				std::cerr << boost::format("Cannot unpack node: %s") % fullEntryPath.string() << std::endl;
				return false;
			}
		}

		return true;
	}

private:
	VolumeFile& m_volume;
	const std::string& m_outDirectory;
	std::string m_parentDirectory;
};

bool VolumeFile::unpackNode(const NodeKey& nodeKey, const std::string& filePath)
{
	const auto volumeIndex = nodeKey.volumeIndex();
	if (volumeIndex >= m_dataStreams.size()) {
		return false;
	}
	auto& streamDesc = m_dataStreams[volumeIndex];
	
	const auto offset = dataOffset() + static_cast<uint64_t>(nodeKey.sectorIndex()) * streamDesc.sectorSize;
	const auto uncompressedSize = nodeKey.size2();
	
	std::vector<uint8_t> data;
	if (!readDataAt(streamDesc.stream, data, offset, nodeKey.size1())) {
		return false;
	}

	decryptData(data.data(), data.size(), nodeKey.nodeIndex());
	inflateDataIfNeeded(data, uncompressedSize);
	
	if (FileExpand::checkIfExpanded(data)) {
		std::vector<uint8_t> unexpandedData;
		if (FileExpand::unexpand(data, unexpandedData)) {
			saveToFile(filePath, unexpandedData.data(), unexpandedData.size());
		} else {
			std::cerr << "Error whilst unexpanding file: " << filePath << std::endl;
		}
	} else {
		saveToFile(filePath, data.data(), data.size());
	}
	
	return true;
}

bool VolumeFile::unpackAll(const std::string& outDirectory)
{
	if (m_entryTreeCount == 0) {
		return false;
	}
	
	const EntryBTree rootEntryBtree(
		advancePointer(m_data.data(), entryTreeOffset(0))
	);
	const EntryUnpacker unpacker(*this, outDirectory);
	rootEntryBtree.traverse(unpacker);

	return true;
}

bool VolumeFile::decryptHeader(uint8_t* header, uint64_t headerSize) const
{
	if (!decryptData(header, headerSize, 1)) {
		return false;
	}

	const auto& keyset = getKeyset();

	auto* beg = reinterpret_cast<uint32_t*>(header);
	auto* end = beg + headerSize / sizeof(*beg);

	if (!needSwapEndian()) { // TODO: rewrite
		keyset.cryptBlocksWithSwapEndian(beg, end, beg);
	} else {
		keyset.cryptBlocks(beg, end, beg);
	}

	return true;
}

bool VolumeFile::decryptData(uint8_t* data, uint64_t dataSize, uint32_t seed) const
{
	if (!data) {
		return false;
	}

	if (dataSize > 0) {
		const auto& keyset = getKeyset();
		keyset.cryptBytes(data, data + dataSize, data, seed);
	}

	return true;
}

bool VolumeFile::inflateDataIfNeeded(std::vector<uint8_t>& in, uint64_t outSize) const {
	if (outSize > UINT32_MAX)
		return false;

	const auto* p = in.data();

	// XXX: inflated data use little-endian always.
	const auto magic = readNext<uint32_t>(p);
	const auto sizeComplement = readNext<uint32_t>(p);

	if (magic != Z_MAGIC || (static_cast<uint32_t>(outSize) + sizeComplement) != 0) { // not compressed?
		return false;
	}

	const auto headerSize = sizeof(magic) + sizeof(sizeComplement);
	if (in.size() < headerSize) {
		return false;
	}

	std::vector<uint8_t> out;
	FileExpand::inflate(out, p, in.size() - headerSize);
	in.swap(out);

	return true;
}

std::string VolumeFile::getEntryPath(const EntryKey& entryKey, const std::string& prefix) const
{
	std::string path(prefix);

	StringBTree nameBtree(
		advancePointer(m_data.data(), nameTreeOffset())
	);
	StringKey nameKey;
	if (nameBtree.searchByIndex(entryKey.nameIndex(), nameKey)) {
		path.append(nameKey.value(), nameKey.value() + nameKey.length());
	}

	if (entryKey.isFile()) {
		StringBTree extBtree(
			advancePointer(m_data.data(), extTreeOffset())
		);
		StringKey extKey;
		if (extBtree.searchByIndex(entryKey.extIndex(), extKey)) {
			if (extKey.length() > 0) {
				path.append(extKey.value(), extKey.value() + extKey.length());
			}
		}
	} else if (entryKey.isDirectory()) {
		path += '/';
	}

	return path;
}

const Keyset& GT5VolumeFile::getKeyset() const
{
	static const Keyset keyset({
		"KALAHARI-37863889", {{ 0x2DEE26A7, 0x412D99F5, 0x883C94E9, 0x0F1A7069 }}
	});
	return keyset;
}

bool GT5VolumeFile::parseHeader(const uint8_t* header, uint64_t headerSize)
{
	const auto* p = header;

	const auto headerSizeAligned = SEGMENT_SIZE;

	const auto magic = VOLUME_READ_NEXT_SELF(p, uint32_t);
	if (magic != HEADER_MAGIC) {
		return false;
	}

	const auto seed = VOLUME_READ_NEXT_SELF(p, uint32_t); // TODO: segment index actually?
	const auto zDataSize = VOLUME_READ_NEXT_SELF(p, uint32_t); // with header
	const auto dataSize = VOLUME_READ_NEXT_SELF(p, uint32_t);
	const auto unk = VOLUME_READ_NEXT_SELF(p, uint64_t);
	const auto fileSize = VOLUME_READ_NEXT_SELF(p, uint64_t);
	UNUSED(fileSize);

	char titleId[128];
	VOLUME_READN_NEXT_SELF(p, char, titleId, sizeof(titleId));
	m_titleId = titleId;

	std::vector<uint8_t> data;
	if (!readDataAt(data, headerSizeAligned, zDataSize)) {
		return false;
	}
	decryptData(data.data(), data.size(), seed);
	if (!inflateDataIfNeeded(data, dataSize)) {
		return false;
	}

	m_dataOffset = alignUp(headerSizeAligned + zDataSize, SEGMENT_SIZE);
	m_data.swap(data);

	m_dataStreams.emplace_back();
	auto& streamDesc = m_dataStreams.back();
	{
		streamDesc.filePath = m_origPath.string();
		if (!prepareStream(streamDesc.stream, streamDesc.filePath, &streamDesc.fileSize)) {
			return false;
		}
		std::cout << boost::format("Data file size: %1%") % streamDesc.fileSize << std::endl;
	}

	return true;
}

const Keyset& GT6VolumeFile::getKeyset() const
{
	static const Keyset keyset({
		"PISCINAS-323419048", {{ 0xAA1B6A59, 0xE70B6FB3, 0x62DC6095, 0x6A594A25 }}
	});
	return keyset;
}

const Keyset& GT7VolumeFile::getKeyset() const
{
	static const Keyset keyset({
		"KYZYLKUM-873068469", {{ 0xC9DA80A5, 0x050DA9A1, 0x9EB1FE65, 0xB651F2FB }}
	});
	return keyset;
}

bool GT7VolumeFile::decryptHeader(uint8_t* header, uint64_t headerSize) const
{
	const bool result = VolumeFile::decryptHeader(header, headerSize);
	if (result) {
		const auto blocks = reinterpret_cast<uint32_t*>(header);
		blocks[0] ^= UINT32_C(0x9AEFDE67);
	}
	return result;
}

bool GT7VolumeFile::parseHeader(const uint8_t* header, uint64_t headerSize)
{
	const auto* p = header;

	const auto headerSizeAligned = SEGMENT_SIZE;

	const auto magic = VOLUME_READ_NEXT_SELF(p, uint32_t);
	if (magic != HEADER_MAGIC) {
		return false;
	}

	// TODO: figure out what are these fields
	const auto hdr_0x1CC = VOLUME_READ_NEXT_SELF(p, uint32_t); // 0x04
	const auto hdr_0x1D0 = VOLUME_READ_NEXT_SELF(p, uint32_t); // 0x08
	const auto hdr_0x1D4 = VOLUME_READ_NEXT_SELF(p, uint32_t); // 0x0C
	const auto hdr_0x1D8 = VOLUME_READ_NEXT_SELF(p, uint32_t); // 0x10

	advancePointerInplace(p, 0xDC);

	const auto seed = VOLUME_READ_NEXT_SELF(p, uint32_t);
	const auto zDataSize = VOLUME_READ_NEXT_SELF(p, uint32_t); // with header
	const auto dataSize = VOLUME_READ_NEXT_SELF(p, uint32_t);
	const auto volumeCount = VOLUME_READ_NEXT_SELF(p, uint32_t);

	m_volumes.resize(volumeCount);
	std::copy_n(reinterpret_cast<const VolumeInfo*>(p), m_volumes.size(), m_volumes.begin());
	for (auto& volumeInfo: m_volumes) {
		volumeInfo.fileSize = (volumeInfo.fileSize >> 32) | ((volumeInfo.fileSize & 0xFFFFFFFF) << 32);
	}

	std::vector<uint8_t> data;
	if (!readDataAt(data, headerSizeAligned, zDataSize)) {
		return false;
	}
	decryptData(data.data(), data.size(), seed);
	if (!inflateDataIfNeeded(data, dataSize)) {
		return false;
	}

	m_dataOffset = 0;
	m_data.swap(data);

	for (auto& volumeInfo: m_volumes) {
		m_dataStreams.emplace_back();
		auto& streamDesc = m_dataStreams.back();
		{
			streamDesc.filePath = (m_basePath / volumeInfo.fileName).string();
			if (!prepareStream(streamDesc.stream, streamDesc.filePath, &streamDesc.fileSize)) {
				return false;
			}
			if (!parseExtendedHeader(streamDesc)) {
				return false;
			}
		}
	}

	return true;
}

bool GT7VolumeFile::parseExtendedHeader(StreamDesc& streamDesc)
{
	streamDesc.extHeader.clear();

	if (!readDataAt(streamDesc.stream, streamDesc.extHeader, 0, sizeof(ExtHeader))) {
		return false;
	}

	const auto& header = *reinterpret_cast<ExtHeader*>(streamDesc.extHeader.data());
	if (header.magic != EXT_HEADER_MAGIC) {
		return false;
	}
	if (header.sectorSize == 0 || (header.sectorSize % EXT_ALIGNMENT != 0)) {
		return false;
	}
	if (header.segmentSize == 0 || (header.segmentSize % EXT_ALIGNMENT != 0)) {
		return false;
	}
	
	streamDesc.sectorSize = header.sectorSize;
	streamDesc.segmentSize = header.segmentSize;
	
	return true;
}

std::string GT7VolumeFile::normalizeFilePath(const std::string& path) const
{
	return boost::to_upper_copy(
		boost::algorithm::trim_left_copy(path)
	);
}
