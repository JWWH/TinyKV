#include "table.h"
#include "table_options.h"
#include "data_block.h"
#include "../utils/crc32c.h"
#include "../utils/codec.h"
#include "../logger/log.h"
#include "filter_block_builder.h"
#include "../include/tinykv/comparator.h"
#include "../cache/cache.h"
#include "two_level_iterator.h"
#include "format.h"


#include <memory>

namespace tinykv {
Table::Table(const Options* options, const FileReader* file_reader)
	: options_(options)
	, file_reader_(file_reader)
{}

// 打开SSTable时， 首先将index block读取出来
// 用于后期查询key时，先通过内存中的index block来
// 判断key在不在这个SSTable，然后再决定是否去读取对应的data block
// 这样可以明显可减少I/O操作
DBStatus Table::Open(const Options& options, FileReader* file, uint64_t file_size, Table** table) {
	if (file_size < kEncodedLength) {
		return Status::kInterupt;
	}
	std::string footer_space;
	footer_space.resize(kEncodedLength);
	// 将footer读出来， 用于解析其中的metaindex_block_handle和index_block_handle
	auto status = file->Read(file_size - kEncodedLength, kEncodedLength, &footer_space);
	if (status != Status::kSuccess) {
		return status;
	}
	// 1、解析出metaindex_block_handle
	// 2、解析出index_block_handle
	FooterBuilder footer;
	std::string st = footer_space;
	status = footer.DecodeFrom(&st);
	std::string index_meta_data;
	ReadOptions opt;
	ReadBlock(file, opt, footer.GetIndexBlockMetaData(), index_meta_data);
	// std::unique_ptr<DataBlock>index_block = std::make_unique<DataBlock>(index_meta_data);
	*table = new Table(&options, file);
	(*table)->index_block_ = std::make_unique<DataBlock>(index_meta_data);
	(*table)->ReadMeta(&footer);
	return status;
}

// DBStatus Table::ReadBlock(const OffSetInfo& offset_info, std::string& buf) {
// 	// ReadBlock就是根据OffsetInfo中的offset和size读取数据到buf中
// 	// 同时也要读取type和crc到buf中
// 	buf.resize(offset_info.length + kBlockTrailerSize);
// 	// kBlockTrailerSize就是每个block末端的五字节信息，包括压缩标志位和用于CRC校验的开销。
// 	file_reader_->Read(offset_info.offset, offset_info.length + kBlockTrailerSize, &buf);
// 	const char* data = buf.data();
// 	const uint32_t crc = crc32c::Unmask(DecodeFixed32(data + offset_info.length + 1));
// 	const uint32_t actual = crc32c::Value(data, offset_info.length + 1);
// 	if (crc != actual) {
// 		LOG(tinykv::LogLevel::ERROR, "Invalid Block");
//    		return Status::kInvalidObject;
// 	}
// 	switch (data[offset_info.length]) {
//     		case kSnappyCompression:
//       			LOG(tinykv::LogLevel::ERROR, "kSnappyCompression");
//       			break;
//     		default:
//      			LOG(tinykv::LogLevel::ERROR, "kNonCompress");
//       			break;
//   	}
// 	return Status::kSuccess;
// }
// 这个函数的主要作用有两个：
// 1、通过footer读出meta block index 的index
// 2、取出meta block index
// 3、读出meta block的真正内容
void Table::ReadMeta(const FooterBuilder* footer) {
	// 如果没有filter，那么也就不用读取了
	if (options_->filter_policy == nullptr) {
		return;
	}
	ReadOptions opt;
	std::string filter_meta_data;
	// 从meta block index index的位置读出meta block index
	// 并把内容放到filer_meta_data中
	ReadBlock(file_reader_, opt, footer->GetFilterBlockMetaData(), filter_meta_data);
	//ReadBlock(footer->GetFilterBlockMetaData(), filter_meta_data);
	std::string_view real_data(filter_meta_data.data(),
                             footer->GetFilterBlockMetaData().length);
	// 利用filter_meta_data生成meta block index
	// meta block index 的格式是
	// ｜ filter.name | BlockHandle |
	// ｜ 	compresstype 1 byte	|
	// ｜	crc32 4 byte		|
  	std::unique_ptr<DataBlock> meta = std::make_unique<DataBlock>(real_data);

	Iterator* iter = meta->NewIterator(std::make_shared<ByteComparator>());
	std::string key = options_->filter_policy->Name();
	// 这里key就是filter_policy的名字
	// value就是BlockHandle
	iter->Seek(key);
	// 这里必须是iter->key() == key
	if (iter->Valid() && iter->key() == key) {
		// 得到BlockHandle之后，去读出filter block
   		// filter block也就是meta block
		LOG(tinykv::LogLevel::ERROR, "Hit Key=%s", key.data());
		ReadFilter(iter->value().ToString());
	}
	delete iter;
}
// 当得到filter block的offset/size之后， 把filter block 即meta block读出来
void Table::ReadFilter(const std::string& filter_handle_value) {
	// filter_handle_value记录了meta block 的offset/size
	OffSetInfo offset_size;
	OffsetBuilder offset_builder;
	offset_builder.Decode(filter_handle_value.data(), offset_size);
	ReadOptions opt;
	ReadBlock(file_reader_, opt, offset_size, bf_);
	bf_.resize(offset_size.length);
}

/**
 * 这里是三个与Block相关的清理工作。分别会用在不同的地方。
 *  首先来说，对于Block::Iter而言，当一个Iter再也不引用到相关的Block的时候，这个内存就会被销毁掉。
 * 那么在销毁的时候需要分两种情况。
 * 1、这个内存块不在cache中，那么就需要直接销毁掉。这个时候就应该调用DeleteBlock。
 * 2、如果这个内存块是在cache中，那么就需要从cache中移除。
 * 删除掉这个cache中的key/block_index，就需要调用DeleteCacheBlock。
 * 然后在DeleteCacheBlock销毁其中的item的时候，为了释放Block占用的内存，
 * 那么还需要间接地调用ReleaseBlock函数。因为Cache设计的通用性，所有的回调函数都是使用了一个void *arg参数。
 * 这里需要看生成一个Block时的设计。
 * 
 */

// 当生成的block对应的iterator不再引用这个block的时候，希望把这个block销毁掉
// 删除一个block
static void DeleteBlock(void* arg, void* ignored) {
	delete reinterpret_cast<DataBlock*>(arg);
}

// 删除cache中的block内存
// 这个主要是用在当cache中的item被删除的时候，会被自动调用
static void DeleteCachedBlock(const Slice& key, void* value) {
	DataBlock* block = reinterpret_cast<DataBlock*>(value);
	delete block;
}

// 从cache中移出去
// 相当于是从map<x,Y>中移除一个item
static void ReleaseBlock(void* arg, void* h) {
	CacheNode<uint64_t, DataBlock>* node = reinterpret_cast<CacheNode<uint64_t, DataBlock>*>(arg);
	Cache<uint64_t, DataBlock>* cache = reinterpret_cast<Cache<uint64_t, DataBlock>*>(arg);
	cache->Release(node);
}
// Table::NewIterator 中会构造一个二级迭代器，第一级自然是 index_block 的迭代器，并且提供了第二级迭代器的创建函数 Table::BlockReader
Iterator* Table::NewIterator(const ReadOptions& options) const {
	return NewTwoLevelIterator(
		index_block_->NewIterator(options_->comparator),
		&Table::BlockReader, const_cast<Table*>(this), options);
}

/**
 * 该函数的第一个参数实际上为 Table 对象的指针，第三个参数是 index_block 键值对中的 Value，也就是对应的 Data Block Handle。
 * 如果不考虑缓存部分:首先解析对应的 BlockHandle，据此读取 block，创建迭代器并且注册迭代器清理函数 DeleteBlock，当删除迭代器时删除对应的 block。
 * 当考虑缓存时: 使用 cache_id 和 handle.offset 构建一个缓存的 Key，将 block 作为缓存的 Value，后者的清理函数为 DeleteCachedBlock；
 * 	       当前使用 block 创建迭代器增加了 block 的引用计数，当迭代器析构时需要调用 ReleaseBlock 以减少缓存的 block 的引用计数。
 */
// 根据一个Index读取一个Data Block
Iterator* Table::BlockReader(void* arg, const ReadOptions& options, const std::string& index_value) {
	Table* table = reinterpret_cast<Table*>(arg);
	auto* block_cache = table->options_->block_cache;
	DataBlock* block = nullptr;
	CacheNode<Slice, DataBlock>* cache_handle = nullptr;

	OffSetInfo offset_size; // 保存索引项
	OffsetBuilder offset_builder;
	offset_builder.Decode(index_value.data(), offset_size);

	DBStatus s;
	std::string contents;
	// 使用缓存，则先读缓存
	if (block_cache != nullptr) {
		// 构造缓存键，使用chache_id和offset
		char cache_key_buffer[16];
		EncodeFixed64(cache_key_buffer, table->cache_id_);
		EncodeFixed64(cache_key_buffer + 8, offset_size.offset);
		Slice key(cache_key_buffer, sizeof(cache_key_buffer));
		// 查找缓存是否存在
		cache_handle = block_cache->Get(key);
		// 存在则直接获取到block
		if (cache_handle != nullptr) {
			block = cache_handle->value;
		} else {
			// 否则从文件里读取Data Block
			s = ReadBlock(table->file_reader_, options, offset_size, contents);
			if (s == Status::kSuccess) {
				block = new DataBlock(contents);
				{
				block_cache->RegistCleanHandle(DeleteCachedBlock);
				block_cache->Insert(key, block);
				}
			}
		}
	} else {
		// 不使用缓存， 直接读取数据
		s = ReadBlock(table->file_reader_, options, offset_size, contents);
		if (s == Status::kSuccess) {
			block = new DataBlock(contents);
		}
	}
	Iterator* iter;
	if (block != nullptr) {
		iter = block->NewIterator(table->options_->comparator);
		if (cache_handle == nullptr) {
			iter->RegisterCleanup(&DeleteBlock, block, nullptr);
		} else {
			iter->RegisterCleanup(&ReleaseBlock, block_cache, cache_handle);
		}
		} else {
			iter = NewErrorIterator(s);
		}
		return iter;
	}


}