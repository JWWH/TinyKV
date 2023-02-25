#pragma once

#include <stdint.h>

#include <functional>
#include <string>
#include <memory>
#include <vector>

#include "lru.h"
#include "cache_policy.h"

namespace tinykv {

#define constexpr const
template <typename KeyType, typename ValueType>
class Cache {
public:
	Cache(uint32_t capacity) {
		cache_.resize(kSharedNum);
		for (int32_t index = 0; index < kSharedNum; index++) {
			cache_[index] = std::make_shared<LruCachePolicy<KeyType, ValueType, MutexLock>>(capacity);
		}
	}

	~Cache() = default;

	const char* Name() const {
		return "shared.cache";
	}
	void Insert(const KeyType& key, ValueType* value, uint32_t ttl = 0) {
		uint64_t shard_num = std::hash<KeyType>{}(key) % kShardNum;
		cache_[shard_num]->Insert(key, value, ttl);
	}
	CacheNode<KeyType, ValueType>* Get(const KeyType& key) {
		uint64_t shard_num = std::hash<KeyType>{}(key) % kShardNum;
		return cache_[shard_num]->Get(key);
	}
	void Release(CacheNode<KeyType, ValueType>* node) {
		uint64_t shard_num = std::hash<KeyType>{}(node->key) % kShardNum;
		return cache_[shard_num]->Release(node);
	}
	void Prune() {
		for (int32_t index = 0; index < kSharedNum; ++index) {
			cache_[index]->Prune();
		}
	}
	void Erase(const KeyType& key) {
		uint64_t shard_num = std::hash<KeyType>{}(key) % kShardNum;
		return cache_[shard_num]->Erase(key);
	}
	void RegistCleanHandle(
      		std::function<void(const KeyType& key, ValueType* value)> destructor) {
    		for (int32_t index = 0; index < kSharedNum; ++index) {
      			cache_[index]->RegistCleanHandle(destructor);
    		}
  	}

private:
	// 设置5个分片， 也就是5个LRU Holder， 一定程度上可以减少碰撞
	// 此外分片还可以减少锁的粒度（将锁的范围减少到原来的1/kSharedNum），提高了并发性
	static constexpr uint64_t kSharedNum = 5;
	std::vector<std::shared_ptr<CachePolicy<KeyType, ValueType> > > cache_;

};

}