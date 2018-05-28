#include "compression.hpp"

#include <iterator>

#include <boost/iostreams/filtering_stream.hpp>
#include <boost/iostreams/filter/zlib.hpp>
#include <boost/iostreams/write.hpp>

bool FileExpand::inflate(std::vector<uint8_t>& out, const uint8_t* data, size_t dataSize) {
	if (data && dataSize > 0) {
		try {
			boost::iostreams::zlib_params params;
			params.window_bits = -boost::iostreams::zlib::default_window_bits;
			
			boost::iostreams::filtering_ostream os;
			os.push(boost::iostreams::zlib_decompressor(params));
			os.push(std::back_inserter(out));
			
			boost::iostreams::write(os, reinterpret_cast<const char*>(data), dataSize);
		}
		catch (const std::exception& e) {
			return false;
		}
	}
	
	return true;
}

bool FileExpand::checkIfExpanded(const std::vector<uint8_t>& data)
{
	if (data.size() < sizeof(SuperHeader)) {
		return false;
	}
	const auto superHdr = reinterpret_cast<const SuperHeader*>(data.data());
	if (superHdr->magic != MAGIC) {
		return false;
	}
	if (superHdr->segmentSize == 0 || (superHdr->segmentSize % ALIGNMENT != 0)) {
		return false;
	}
	if (data.size() < superHdr->fileSize) {
		return false;
	}
	
	return true;
}

bool FileExpand::unexpand(const std::vector<uint8_t>& in, std::vector<uint8_t>& out)
{
	if (!checkIfExpanded(in)) {
		return false;
	}
	
	const auto superHdr = reinterpret_cast<const SuperHeader*>(in.data());
	const auto segmentCount = (superHdr->fileSize + superHdr->segmentSize - 1) / superHdr->segmentSize;

	out.clear();
	out.reserve(superHdr->decompressedFileSize);
	
	boost::iostreams::zlib_params params;
	params.window_bits = -boost::iostreams::zlib::default_window_bits;
	
	auto status = true;
	for (auto i = 0u; i < segmentCount; ++i) {
		const auto segmentHdr = (i == 0)
			? reinterpret_cast<const SegmentHeader*>(superHdr + 1)
			: reinterpret_cast<const SegmentHeader*>(reinterpret_cast<const uint8_t*>(superHdr) + static_cast<size_t>(superHdr->segmentSize) * i)
		;

		const auto segmentData = reinterpret_cast<const uint8_t*>(segmentHdr + 1);
		if (!inflate(out, segmentData, segmentHdr->zSize)) {
			status = false;
			break;
		}
	}
	
	if (out.size() != superHdr->decompressedFileSize) {
		status = false;
	}
	
	return status;
}
