#include "table_builder.h"
#include "../utils/crc32c.h"
#include "../utils/codec.h"
#include "../include/tinykv/comparator.h"
#include "footer_builder.h"

namespace tinykv {
TableBuilder::TableBuilder(const Options& options, FileWriter* file_handler) 
	: options_(options)
	, index_options_(options)
	, data_block_builder_(&options)
	, index_block_builder_(&index_options_)
	, filter_block_builder_(options) 
{
	// index block部分不需要进行差值压缩，因为本身数据就很少
	// 也就是把block_restart_interval设置为1
	// 别的选项和options_(DataBlock的元数据)共享
	index_options_.block_restart_interval = 1;
	file_handler_ = file_handler;
}

void TableBuilder::Add(const std::string& key,
			const std::string& value) {
	if (key.empty()) {
		return;
	}
	// 需要构建index block的时候才构建
	// 并不是每次插入新数据都会构建index block
	if (need_create_index_block_ && options_.comparator) {
		// index中key做了优化，尽可能短
		options_.comparator->FindShortestSeparator(&pre_block_last_key_, key);
		std::string output;
		// index中的value保存的是当前key在block中的偏移量和对应的block大小
		index_block_offset_info_builder_.Encode(pre_block_offset_info_, output);
		index_block_builder_.Add(pre_block_last_key_, output);
		need_create_index_block_ = false;
	}
	// 构建bloom filter(整个sst就构建一个)
	if (filter_block_builder_.Available()) {
		filter_block_builder_.Add(key);
	}
	pre_block_last_key_ = key;
	++entry_count_;
	// 写入data block
	data_block_builder_.Add(key, value);
	// 超过block的大小之后，需要进行一个刷盘操作
	if (data_block_builder_.CurrentSize() >= options_.block_size) {
		Flush();
	}
}

// Flush()只是开启新的DataBlock，并没有真的进行刷盘操作
void TableBuilder::Flush() {
	if (data_block_builder_.CurrentSize() == 0) {
		return;
	}
	// 先写data block数据
	WriteDataBlock(data_block_builder_, pre_block_offset_info_);
	// 如果写入数据成功
	if (status_ == Status::kSuccess) {
		// 在下一轮循环中时，需要更新index block数据
		need_create_index_block_ = true;
		// 针对剩余的还未刷盘的数据需要手动进行刷盘
		status_ = file_handler_->Flush();
	}
}

void TableBuilder::WriteDataBlock(DataBlockBuilder& data_block_builder, OffSetInfo& offset_size) {
	// 就是把restart_pointer加到DataBlock中
	// 也就是追加到所有的record后面
	data_block_builder.Finish();

	// 打包data_block中现有的所有数据
	// data中是所有的record和restart_pointers
	const std::string& data = data_block_builder.Data();
	WriteBytesBlock(data, options_.block_compress_type, offset_size);
	// 一个DataBlock构造完毕之后调用reset操作
	data_block_builder.Reset();
}

void TableBuilder::WriteBytesBlock(const std::string& datas, BlockCompressType block_compress_type, OffSetInfo& offset_size) {
	std::string compress_data = datas;
	bool compress_success = false;
	BlockCompressType type = block_compress_type;
	switch (block_compress_type) {
		case kSnappyCompression: {
			compress_data = datas;
			compress_success = true;
		} break;
		default:
			type = kNonCompress;
			break;
	}

	offset_size.offset = block_offset_;
	offset_size.length = datas.size();
	// 追加我们的block数据
	status_ = file_handler_->Append(datas.data(), datas.size());
	char trailer[kBlockTrailerSize];
	trailer[0] = static_cast<uint8_t>(type);
	uint32_t crc = crc32c::Value(datas.data(), datas.size());
	crc = crc32c::Extend(crc, trailer, 1);  // Extend crc to cover block type
	EncodeFixed32(trailer + 1, crc32c::Mask(crc));
	status_ = file_handler_->Append(trailer, kBlockTrailerSize);
	if (status_ == Status::kSuccess) {
		block_offset_ += offset_size.length + kBlockTrailerSize; 
	}
}

void TableBuilder::Finish() {
	if (!Success()) {
		return; 
	}
	// 因为我们在Append数据的时候，只有当超过一个DataBlock的大小时，才进行Flush刷盘操作
	// 假如到了最后一个DataBlock，但是数据并没有将这个 Block填满，这部分数据就不会在Append的时候进行刷盘了
	// 为了将这部分数据刷到磁盘中，我们需要在Finish中手动进行刷盘
	// 因为Finish中主要是把metablock(布隆过滤器)、indexblock、Footer刷到磁盘中
	// 而这些数据在刷盘之前必须把所有的DataBlock刷到盘中，所以在Finish函数的最开始执行Flush()
	Flush();
	OffSetInfo filter_block_offset;	// 布隆过滤器的offset和size，需要记录在meta_index_block中
	OffSetInfo meta_filter_block_offset; // meta_index_block的offset和size，需要记录在footer中
	OffSetInfo  index_block_offset;	// index block 的offset和 size，需要记录在footer中
	// 开始构建meta_block和meta_index_block
	if(filter_block_builder_.Available()) {
		filter_block_builder_.Finish();	// 构建布隆过滤器，并将得到的结果和哈希函数的数量序列化到buffer中
		const auto& filter_block_data = filter_block_builder_.Data();

		// 将布隆过滤器的结果写入文件、
		// 不需要进行压缩
		WriteBytesBlock(filter_block_data, BlockCompressType::kNonCompress, filter_block_offset);
		// 这部分是获取布隆过滤器部分的数据在整个sst中的位置，然后将这部分数据写入sst文件
		// 这部分的目的是针对不同的块可以使用不同的filter_policy
		DataBlockBuilder meta_filter_block(&options_);
		OffsetBuilder filter_block_offset_builder;
		std::string handle_encoding_str;
		filter_block_offset_builder.Encode(filter_block_offset, handle_encoding_str);
		meta_filter_block.Add(options_.filter_policy->Name(), handle_encoding_str);
		WriteDataBlock(meta_filter_block, meta_filter_block_offset);
	}
	// 处理index_block
	if (need_create_index_block_ && options_.comparator) {
		// 最后一个key这里我们就不做优化了，直接使用(leveldb中是FindShortSuccessor(std::string*
		// key)函数)
		// index中的value保存的是当前key在block中的偏移量和对应的block大小
		std::string output;
		index_block_offset_info_builder_.Encode(pre_block_offset_info_, output);
		index_block_builder_.Add(pre_block_last_key_, output);
		need_create_index_block_ = false;
  	}
	WriteDataBlock(index_block_builder_, index_block_offset);
	// 把footer加入到sst文件中
	FooterBuilder footer_builder;
	footer_builder.SetFilterBlockMetaData(meta_filter_block_offset);
	footer_builder.SetIndexBlockMetaData(index_block_offset);
	std::string footer_output;
	footer_builder.EncodeTo(&footer_output);
	file_handler_->Append(footer_output.data(), footer_output.size());
	block_offset_ += footer_output.size();
	file_handler_->Close();
}

}