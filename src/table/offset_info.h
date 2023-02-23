#pragma once

#include <stdint.h>
#include <string>
#include <string_view>

#include "../include/tinykv/status.h"

namespace tinykv {
struct OffSetInfo {
	// 记录数据的起点
	uint64_t offset = 0;
	// 记录数据的长度
	uint64_t length = 0;
};

class OffsetBuilder final {
public:
	// 按照variant编解码
	void Encode(const OffSetInfo& offset_info, std::string& output);
	DBStatus Decode(const char* input, OffSetInfo& offset_info);
	std::string DebugString(const OffSetInfo& offset_info);
};
}
