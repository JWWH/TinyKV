#pragma once

#include <stdint.h>
#include <string>
#include <memory>
#include "../cache/cache.h"
#include "../table/data_block.h"


namespace tinykv {

class FilterPolicy;
class Comparator;

enum BlockCompressType {
	kNonCompress = 0x0,
	kSnappyCompression = 0x1
};

// DB的配置信息，如是否开启同步、缓存池等
struct Options {
	// 单个block的大小
	uint32_t block_size = 4 * 1024;
	// 16个entry来构建一个restart
	uint32_t block_restart_interval = 16;
	// 最多的层数，默认是7
	uint32_t max_level_num = 7;
	// kv分离的阈值(默认1024K)
	uint32_t max_key_value_split_threshold = 1024;
	// 默认不会进行压缩
	BlockCompressType block_compress_type = BlockCompressType::kNonCompress;

	std::shared_ptr<FilterPolicy> filter_policy = nullptr;
	std::shared_ptr<Comparator> comparator = nullptr;

	Cache<uint64_t, DataBlock>* block_cache = nullptr;
};
struct ReadOptions {
	
};

}