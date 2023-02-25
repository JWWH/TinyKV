#pragma once

#include "../file/file_reader.h"
#include "../file/file_writer.h"
#include "../db/options.h"
#include "offset_info.h"
#include "data_block_builder.h"
#include "filter_block_builder.h"

#include <string>

namespace tinykv {

struct Options;

class TableBuilder final {
public:
	TableBuilder(const Options& options, FileWriter* file_handler);
	TableBuilder(const TableBuilder&) = delete;
	TableBuilder& operator=(const TableBuilder&) = delete;

	void Add(const std::string& key, const std::string& value);
	void Finish();
	bool Success() { return status_ == Status::kSuccess; }
	uint32_t GetFileSize() { return block_offset_; }
	uint32_t GetEntryNum() { return entry_count_; }

private:
	void Flush();
	void WriteDataBlock(DataBlockBuilder& data_block, OffSetInfo& offset_info);
	void WriteBytesBlock(const std::string& datas, BlockCompressType block_compress_type, OffSetInfo& offset_info);

private:
	Options options_;	// 构造TableBuilder需要的元数据
	// index block部分不需要进行差值压缩，因为本身数据就很少
	// 也就是把block_restart_interval设置为1
	// 别的选项和options_(DataBlock的元数据)共享
	Options index_options_;	
	DataBlockBuilder data_block_builder_;
	DataBlockBuilder index_block_builder_;
	FilterBlockBuilder filter_block_builder_;
	OffsetBuilder index_block_offset_info_builder_;
	FileWriter* file_handler_ = nullptr;
	// 该成员变量用于索引的构建
	// 因为每个索引记录的内容都是要能够分割两个DataBlock的最短key
	// 为了获取这个最短key，需要记录上一个BlockData的最后一个key
	// 因为DataBlock中的记录都是按key从大到小的顺序排列的，所以最后一个key也就是最大key
	std::string pre_block_last_key_;
	// 记录前一个DataBlock相比于sst文件开头的偏移量和大小
	OffSetInfo pre_block_offset_info_;
	// 因为构建sst文件是一个按顺序构建的过程
	// 所以需要一个指针记录当前构造到什么位置
	uint32_t block_offset_ = 0;
	// 记录一共有多少个记录了
	uint32_t entry_count_ = 0;
	// 是否创建index block
	// 因为TableBuilder的Add操作是一个循环操作，
	// 但是并不是每一次add数据的时候都会创建index block
	// 只有当DataBlock到一定容量后才会创建index block
	bool need_create_index_block_ = false;
	DBStatus status_;
};

}