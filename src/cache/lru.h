#pragma once

#include <functional>
#include <list>
#include <string>
#include <type_traits>
#include <unordered_map>

#include "../utils/hash_util.h"
#include "../utils/lock.h"
#include "../utils/util.h"
#include "cache_node.h"
#include "cache_policy.h"

namespace tinykv {
template <typename KeyType, typename ValueType, typename LockType = NullLock>
class LruCachePolicy final : public CacheNode<KeyType, ValueType> {
public:
	LruCachePolicy(uint32_t capacity) : capacity_(capacity) {}
	~LruCachePolicy() = default;	// 程序结束，LRU析构，内存会被系统回收

	// 插入节点
	void Insert(const KeyType& key, ValueType* value, uint32_t ttl = 0) {
		ScopedLockImple<LockType> lock_guard(cache_lock_);
		CacheNode<KeyType,ValueType>* new_node = new CacheNode<KeyType, ValueType>();
		new_node->key = key;
		new_node->value = value;
		new_node->hash = std::hash<KeyType>{}(key);
		new_node->in_cache = true;
		new_node->refs = 1;
		new_node->ttl = ttl;
		if(ttl > 0) {
			new_node->last_access_time = util::GetCurrentTime();
		}
		typename std::unordered_map<KeyType, ListIter>::iterator iter = index_.find(key);
		// 判断cache中是否已经有这个key
		if (iter == index_.end()) {	// 如果没有
			// 如果cache已经达到最大容量
			if (nodes_.size() == capacity_) {
				// 淘汰最后一个节点，并把新插入的节点插入到链表头
				CacheNode<KeyType, ValueType>* node = nodes_.back();
				index_.erase(node->key);
				nodes_.pop_back();
				FinishErase(node);
			}
			nodes_.push_front(new_node);
			index_[key] = nodes_.begin();
		} else {	// 说明cache中已经存在值为key的节点
			// 更新节点的值， 并将其加到链表头部
			nodes_.splice(nodes_.begin(), nodes_, index[key]);
			index_[key] = nodes_.begin();
		}
	}

	// 查询
	CacheNode<KeyType, ValueType>* Get(const KeyType& key) {
		ScopedLockImple<LockType> lock_guard(cache_lock_);
		typename std::unordered_map<KeyType, ListIter>::iterator iter = index_.find(key);
		if(iter == index_.end()) {
			return nullptr;
		}	
		CacheNode<KeyType, ValueType>* node = *(iter->second);
		nodes_.erase(iter->second);
		// 将node移动到头部
		nodes_.push_front(node);
		index_[node->key] = nodes_.begin();
		Ref(node);
		return node;
	}

	// 注册销毁节点的回调函数
	void RegistCleanHandle(std::function<void(const KeyType& key, ValueType* value)> destructor) {
		destructor_ = destructor;
	}

	// 释放节点
	// 也就是外部不用这个节点了
	void Release(CacheNode<KeyType, ValueType>* node) {
		ScopedLockImple<LockType> lock_guard(cache_lock_);
		Unref(node);
	}

	// 定期进行回收
	void Prune() {
		ScopedLockImple<LockType> lock_guard(cache_lock_);
		for (auto it = wait_erase_.begin(); it != wait_erase_.end(); ++it) {
			Unref((it->second));
		}
	}

	// 删除某个key对应的节点
	void Erase(const KeyType& key) {
		ScopedLockImple<LockType> lock_guard(cache_lock_);
		typename std::unordered_map<KeyType, ListIter>::iterator iter = index_.find(key);
		if (iter == index_.end()) {
			return;
		}
		CacheNode<KeyType, ValueType>* node = *(iter->second);
		//从user列表中删除该对象
		nodes_.erase(iter->second);
		index_.erase(node->key);
		FinishErase(node);
	}

private:
	void Ref(CacheNode<KeyType, ValueType>* node) {
		if (node) {
			++node->refs;
		}
	}

	void Unref(CacheNode<KeyType, ValueType>* node) {
		if (node) {
			--node->refs;
			if(node->refs <= 0) {
				destructor_(node->key, node->value);
				if (wait_erase_.count(node->key) > 0) {
					wait_erase_.erase(node->key);
				}
				delete node;
				node = nullptr;
			}
		}
	}

	void MoveToEraseContainer(CacheNode<KeyType, ValueType>* node) {
		if (wait_erase_.count(node->key) == 0) {
			wait_erase_[node->key] = node;
		}
	}
	/*
         * (虚幻的)删除节点node的步骤：(这里并不是真的erase节点，而是将node移动到回收站暂存)
         * 0. 从nodes和index中删除
         * 1. 标记node不在缓存中
         * 2. 将node移动到待删除队列
         * 3. node引用计数减一
         *
         * 解释一下为什么需要引用计数？
         * 实际上这里存在两种策略：第一种是一旦LRU满了，就直接删除node，不需要什么引用计数，
         * 但是这种方法性能较低，因为每次查找Get都是直接返回一个node的深拷贝；第二种是采用
         * 引用计数的方法，Get直接返回一个node指针，但是这样的话，如果LRU满了，就不能直接
         * 删除node（此时引用计数不一定为0，表示被上层使用），需要把它暂时移动到回收站（指针移动），
         * 等待其引用计数降为0，才可以删除。
         * */
	void FinishErase(CacheNode<KeyType, ValueType>* node) {
		if (node) {
			node->in_cache = false;
			MoveToEraseContainer(node);
			Unref(node);
		}
	}

private: 
	const uint32_t capacity_;
	uint32_t cur_size_ = 0;
	std::list<CacheNode<KeyType, ValueType>*> nodes_;
	using ListIter = typename std::list<CacheNode<KeyType, ValueType>*>::iterator;
	// 保存底层链表的迭代器，对链表的迭代器来说，删除链表的节点，不会影响其他节点的迭代器
	typename std::unordered_map<KeyType, ListIter> index_;
	// 称为待删除列表
        // 这里的node已经不在缓存中，但是部分节点的引用计数还没有降为0（有上层在使用），
        // 所以不能立即删除，需要开启一个线程定时检查，进行清除。
	std::unordered_map<KeyType, CacheNode<KeyType, ValueType>*> wait_erase_;
	// 销毁节点的回调函数
	std::function<void(const KeyType& key, ValueType* value)> destructor_;
	LockType cache_lock_;

};
}