#pragma once
#include "offset_info.h"
#include <string>

namespace tinykv {
class FooterBuilder final {
public:
	void EncodeTo(std::string* dst);
	DBStatus DecodeFrom(std::string* input);
	void SetFilterBlockMetaData(const OffSetInfo& filter_block) {
		filter_block_ = filter_block;
	}
	void SetIndexBlockMetaData(const OffSetInfo& index_block) {
		index_block_ = index_block;
	}
	const OffSetInfo& GetFilterBlockMetaData() const { return filter_block_; }
	const OffSetInfo& GetIndexBlockMetaData() const { return index_block_; }

	std::string DebugString();
private:
	// filter block部分在整个sst文件中的偏移量和大小
	OffSetInfo filter_block_;
	// index block部分在整个sst文件中的偏移量和大小
	OffSetInfo index_block_;
	OffsetBuilder offset_builder_;
};

}