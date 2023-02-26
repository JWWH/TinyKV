#include "format.h"
#include "table_options.h"
#include "../utils/crc32c.h"
#include "../logger/log.h"
#include "../utils/codec.h"

namespace tinykv {
DBStatus ReadBlock(const FileReader* file, const ReadOptions& options, const OffSetInfo& offset_info, std::string& buf) {
	// ReadBlock就是根据OffsetInfo中的offset和size读取数据到buf中
	// 同时也要读取type和crc到buf中
	buf.resize(offset_info.length + kBlockTrailerSize);
	// kBlockTrailerSize就是每个block末端的五字节信息，包括压缩标志位和用于CRC校验的开销。
	file->Read(offset_info.offset, offset_info.length + kBlockTrailerSize, &buf);
	const char* data = buf.data();
	const uint32_t crc = crc32c::Unmask(DecodeFixed32(data + offset_info.length + 1));
	const uint32_t actual = crc32c::Value(data, offset_info.length + 1);
	if (crc != actual) {
		LOG(tinykv::LogLevel::ERROR, "Invalid Block");
   		return Status::kInvalidObject;
	}
	switch (data[offset_info.length]) {
    		case kSnappyCompression:
      			LOG(tinykv::LogLevel::ERROR, "kSnappyCompression");
      			break;
    		default:
     			LOG(tinykv::LogLevel::ERROR, "kNonCompress");
      			break;
  	}
	return Status::kSuccess;
}
}