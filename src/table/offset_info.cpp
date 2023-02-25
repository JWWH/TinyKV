#include "offset_info.h"
#include "../utils/codec.h"

namespace tinykv {
void OffsetBuilder::Encode(const OffSetInfo& offset_info, std::string& output) {
	PutFixed64(&output, offset_info.offset);
	PutFixed64(&output, offset_info.length);
}
DBStatus OffsetBuilder::Decode(const char* input, OffSetInfo& offset_info) {
	offset_info.offset = DecodeFixed64(input);
	offset_info.length = DecodeFixed64(input + 8);
	return Status::kSuccess;
}


}
