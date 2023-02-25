#pragma once

#include <functional>

namespace tinykv {
template <typename KeyType, typename ValueType>
struct CacheNode {
	// 保证key是深度复制的
	KeyType key;
	ValueType* value;
	// 引用计数
	uint32_t refs = 0;	// 用户使用cache中的数据会增加引用计数，只有当引用计数为0时才可以淘汰cache中的数据
	uint32_t hash = 0;
	// 默认不在缓存中
	bool in_cache = false;
	// 最近一次访问的时间
	uint64_t last_access_time = 0;
	// 有效周期
	uint64_t ttl = 0;
};

}