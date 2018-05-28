#pragma once

#include "btree.hpp"
#include "crypto.hpp"

#include <fstream>
#include <vector>

#include <boost/filesystem.hpp>

// TODO: refactor this shit
#define VOLUME_READ(volumePtr, dataPtr, type) ((volumePtr)->needSwapEndian() ? readWithByteSwap<type>(dataPtr) : read<type>(dataPtr))
#define VOLUME_READ_SELF(dataPtr, type) VOLUME_READ(this, dataPtr, type)

#define VOLUME_READ_NEXT(volumePtr, dataPtr, type) ((volumePtr)->needSwapEndian() ? readNextWithByteSwap<type>(dataPtr) : readNext<type>(dataPtr))
#define VOLUME_READ_NEXT_SELF(dataPtr, type) VOLUME_READ_NEXT(this, dataPtr, type)

#define VOLUME_READ_AT(volumePtr, dataPtr, type, offset) ((volumePtr)->needSwapEndian() ? readAtWithByteSwap<type>(dataPtr, (offset)) : readAt<type>(dataPtr, (offset)))
#define VOLUME_READ_AT_SELF(dataPtr, type, offset) VOLUME_READ_AT(this, dataPtr, type, offset)

#define VOLUME_READN(volumePtr, dataPtr, type, ptr, count) readN<type>((dataPtr), (ptr), (count))
#define VOLUME_READN_SELF(dataPtr, type, ptr, count) VOLUME_READN(this, dataPtr, type, ptr, count)

#define VOLUME_READN_NEXT(volumePtr, dataPtr, type, ptr, count) readNextN<type>((dataPtr), (ptr), (count))
#define VOLUME_READN_NEXT_SELF(dataPtr, type, ptr, count) VOLUME_READN_NEXT(this, dataPtr, type, ptr, count)

#define VOLUME_READN_AT(volumePtr, dataPtr, type, offset, ptr, count) readAtN<type>((dataPtr), (offset), (ptr), (count))
#define VOLUME_READN_AT_SELF(dataPtr, type, offset, ptr, count) VOLUME_READN_AT(this, dataPtr, type, offset, ptr, count)

class VolumeFile
	: private boost::noncopyable
{
public:
	static const auto SEGMENT_SIZE = UINT64_C(0x800);

	VolumeFile(bool swapEndian)
		: m_swapEndian(swapEndian)
	{
		reset();
	}

	virtual ~VolumeFile() {}

	bool load(const std::string& filePath);

	bool unpackNode(const NodeKey& nodeKey, const std::string& filePath);
	bool unpackAll(const std::string& outDirectory);

	std::string getEntryPath(const EntryKey& entryKey, const std::string& prefix) const;

	const auto& data() const { return m_data; }

	bool needSwapEndian() const { return m_swapEndian; }
	bool hasMultipleVolumes() const { return m_dataStreams.size() > 1; }

	auto nameTreeOffset() const { return m_nameTreeOffset; }
	auto extTreeOffset() const { return m_extTreeOffset; }
	auto entryTreeCount() const { return m_entryTreeCount; }
	auto entryTreeOffset(uint32_t index) const { return m_entryTreeOffsets[index]; }
	auto nodeTreeOffset() const { return m_nodeTreeOffset; }
	auto dataOffset() const { return m_dataOffset; }

protected:
	static const auto HEADER_MAGIC = UINT32_C(0x5B745162);
	static const auto SEGMENT_MAGIC = UINT32_C(0x5B74516E);
	static const auto Z_MAGIC = UINT32_C(0xFFF7EEC5);

	static const auto DEFAULT_SECTOR_SIZE = UINT32_C(0x800);
	static const auto DEFAULT_SEGMENT_SIZE = UINT32_C(0x10000);

	struct StreamDesc
	{
		StreamDesc()
			: fileSize(0)
			, sectorSize(DEFAULT_SECTOR_SIZE)
			, segmentSize(DEFAULT_SEGMENT_SIZE)
		{
		}

		std::ifstream stream;
		std::vector<uint8_t> extHeader;
		std::string filePath;

		uint64_t fileSize;
		uint32_t sectorSize;
		uint32_t segmentSize;
	};

	virtual const Keyset& getKeyset() const = 0;

	virtual size_t getHeaderSize() const = 0;

	virtual void reset()
	{
		m_origPath.clear();
		m_basePath.clear();
		m_baseName.clear();

		if (m_mainStream.is_open()) {
			m_mainStream.close();
		}

		for (auto& dataStream: m_dataStreams) {
			if (!dataStream.stream.is_open()) {
				continue;
			}
			dataStream.stream.close();
		}
		m_dataStreams.clear();

		m_mainFileSize = 0;

		m_data.clear();
		m_entryTreeOffsets.clear();

		m_nameTreeOffset = m_extTreeOffset = 0;
		m_nodeTreeOffset = m_entryTreeCount = 0;

		m_dataOffset = 0;
	}

	bool readDataAt(std::vector<uint8_t>& data, uint64_t offset, uint64_t size)
	{
		return readDataAt(m_mainStream, data, offset, size);
	}

	unsigned int getNodeByPath(const std::string& filePath, NodeKey& nodeKey) const;

	bool decryptData(uint8_t* data, uint64_t dataSize, uint32_t seed) const;

	bool inflateDataIfNeeded(std::vector<uint8_t>& in, uint64_t outSize) const;

	virtual std::string normalizeFilePath(const std::string& path) const
	{
		return path;
	}

	bool readDataAt(std::ifstream& stream, std::vector<uint8_t>& data, uint64_t offset, uint64_t size);

	virtual bool parseHeader(const uint8_t* header, uint64_t headerSize)
	{
		return false;
	}

	bool parseSegment();

	virtual bool decryptHeader(uint8_t* header, uint64_t headerSize) const;

	boost::filesystem::path m_origPath;
	boost::filesystem::path m_basePath, m_baseName;

	std::ifstream m_mainStream;
	std::vector<StreamDesc> m_dataStreams;

	uint64_t m_mainFileSize;

	std::vector<uint8_t> m_data;
	std::vector<uint32_t> m_entryTreeOffsets;

	uint32_t m_nameTreeOffset;
	uint32_t m_extTreeOffset;
	uint32_t m_nodeTreeOffset;
	uint32_t m_entryTreeCount;
	uint64_t m_dataOffset;

	bool m_swapEndian;
};

class GT5VolumeFile
	: public VolumeFile
{
public:
	GT5VolumeFile()
		: VolumeFile(true)
	{
	}

	const auto& titleId() const { return m_titleId; }

protected:
	const Keyset& getKeyset() const override;

	size_t getHeaderSize() const override { return 0xA0; }

	void reset() override
	{
		VolumeFile::reset();

		m_titleId.clear();
	}

	bool parseHeader(const uint8_t* header, uint64_t headerSize) override;

	std::string m_titleId;
};

class GT6VolumeFile
	: public GT5VolumeFile
{
protected:
	const Keyset& getKeyset() const override;
};

class GT7VolumeFile
	: public VolumeFile
{
public:
	GT7VolumeFile()
		: VolumeFile(false)
	{
	}

protected:
	struct ExtHeader
	{
		uint64_t magic;
		uint32_t sectorSize;
		uint32_t segmentSize;
		uint64_t fileSize;
		uint32_t flags;
		uint32_t unk; // TODO: seed?
	};

	static const auto MAX_FILE_NAME_LENGTH = 16;

	static const auto EXT_HEADER_MAGIC = UINT64_C(0x2B26958523AD);
	static const auto EXT_ALIGNMENT = 0x400;

	static const auto UNK_SEED = UINT32_C(0xD265FF5C);

	struct VolumeInfo
	{
		char fileName[MAX_FILE_NAME_LENGTH];
		uint64_t fileSize;
	};

	const Keyset& getKeyset() const override;

	size_t getHeaderSize() const override { return 0xA60; }

	bool decryptHeader(uint8_t* header, uint64_t headerSize) const override;

	void reset() override
	{
		VolumeFile::reset();

		m_volumes.clear();
	}

	bool parseHeader(const uint8_t* header, uint64_t headerSize) override;
	bool parseExtendedHeader(StreamDesc& streamDesc);

	std::string normalizeFilePath(const std::string& path) const override;

	std::vector<VolumeInfo> m_volumes;
};
