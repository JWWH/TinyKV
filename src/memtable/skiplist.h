#pragma once

#include "../utils/random_util.h"

#include <atomic>
#include <assert.h>
#include <stdint.h>
#include <iostream>

namespace tinykv{
struct SkipListOption {
  static const int32_t kMaxHeight = 20;
  //有多少概率被选中, 空间和时间的折中
  static const unsigned int kBranching = 4;
};
template <typename _Key, typename _KeyComparator, typename _Allocator>
class SkipList final{
private: 
	struct Node;
public:
	explicit SkipList(_KeyComparator comparator);

	SkipList(const SkipList&) = delete;
	SkipList& operator=(const SkipList&) = delete;

	void Insert(const _Key& key);

	bool Contains(const _Key& key) const;

	bool Equal(const _Key& a, const _Key& b) const { return (comparator_(a,b) == 0); }

	// 迭代skiplist，主要给MemTable中的MemIterator使用
	class Iterator {
		public:
		// Initialize an iterator over the specified list.
		// The returned iterator is not valid.
		explicit Iterator(const SkipList* list);

		// Returns true iff the iterator is positioned at a valid node.
		bool Valid() const;

		// Returns the key at the current position.
		// REQUIRES: Valid()
		const _Key& key() const;

		// Advances to the next position.
		// REQUIRES: Valid()
		void Next();

		// Advances to the previous position.
		// REQUIRES: Valid()
		void Prev();

		// Advance to the first entry with a key >= target
		void Seek(const _Key& target);

		// Position at the first entry in list.
		// Final state of iterator is Valid() iff list is not empty.
		void SeekToFirst();

		// Position at the last entry in list.
		// Final state of iterator is Valid() iff list is not empty.
		void SeekToLast();

		private:
		const SkipList* list_;
		Node* node_;
		// Intentionally copyable
	};

private:
	Node* NewNode(const _Key& key, int32_t height);
	int32_t RandomHeight();
	inline int32_t GetMaxHeight() { return cur_height_.load(std::memory_order_relaxed); }
	bool KeyIsAfterNode(const _Key& key, Node* n) {
		return (nullptr != n && comparator_.Compare(n->key, key) < 0);
	}
	/**
	 * 该函数的含义为：在跳表中查找不小于给定 Key 的第一个值，如果没有找到，则返回 nullptr。
	 * 如果参数 prev 不为空，在查找过程中，记下待查找节点在各层中的前驱节点。
	 * 如果是查找操作，则指定 prev = nullptr 即可；
	 * 若要插入数据，则需传入一个合适尺寸的 prev 参数。
	 */
	Node* FindGreaterOrEqual(const _Key& key, Node**prev);
	// 找到小于key中最大的key
	Node* FindLessThan(const _Key& key) const;
	// 返回skiplist的最后一个节点
	Node* FindLast() const;

private: 
	_KeyComparator comparator_;	// 比较器
	_Allocator arena_;		// 内存管理对象
	Node* head_ = nullptr;		// skiplist头节点
	std::atomic<int32_t> cur_height_;// 当前有效的层数
	RandomUtil rnd_;
};

// 实现SkipList的Node结构
template <typename _Key, typename _KeyComparator, typename _Allocator>
struct SkipList<_Key, _KeyComparator, _Allocator>::Node
{
	explicit Node(const _Key& k) : key(k) {}

	const _Key key;

	Node* Next(int n) {
		assert(n >= 0);
		// std::memory_order_acquire: 用在 load 时，保证同线程中该 load 之后的对相关内存读写语句不会被重排到 load 之前，
		// 并且其他线程中对同样内存用了 store release 都对其可见。
		return next_[n].load(std::memory_order_acquire);
	}

	void SetNext(int n, Node* x) {
		assert(n >= 0);
		// std::memory_order_release：用在 store 时，保证同线程中该 store 之后的对相关内存的读写语句不会被重排到 store 之前，
		// 并且该线程的所有修改对用了 load acquire 的其他线程都可见。
		next_[n].store(std::memory_order_release);
	}

	// 不带内存屏障版本的访问器。内存屏障（Barrier）是个形象的说法，也即加一块挡板，阻止重排/进行同步
	// std::memory_order_relaxed：不对重排做限制，只保证相关共享内存访问的原子性。
	Node* NoBarrier_Next(int n) {
		assert(n >= 0);
		return next_[n].load(std::memory_order_relaxed);
	}
	void NoBarrier_SetNext(int n, Node* x) {
		assert(n >= 0);
		next_[n].store(x, std::memory_order_relaxed);
	}
private:
	// 指针数组的长度即为该节点的 level，next_[0] 是最低层指针.
	std::atomic<Node*> next_[1];

};

template <typename _Key, typename _KeyComparator, typename _Allocator>
typename SkipList<_Key, _KeyComparator, _Allocator>::Node* 
	SkipList<_Key, _KeyComparator, _Allocator>::NewNode(const _Key& key, int32_t height)
{
	// TODO: 将Allocate替换成AllocateAligned
	char* node_memory = (char*)arena_.Allocate(
		sizeof(Node) + sizeof(std::atomic<Node*>) * (height - 1));
	// 使用定位new，在刚分配好的空间node_memory处构造一个Node对象
	return new (node_memory) Node(key);
}
/**
 * 跳表实现的关键点在于每个节点插入时，如何确定新插入节点的层数，以使跳表满足概率均衡，进而提供高效的查询性能
 * 确定新插入节点的层数是由RandomHeight实现的
 */
template <typename _Key, typename _KeyComparator, typename _Allocator>
int32_t SkipList<_Key, _KeyComparator, _Allocator>::RandomHeight()
{
	// // 每次以 1/SkipListOption::kBranching 的概率增加层数
	int32_t height = 1;
	while (height < SkipListOption::kMaxHeight &&
		((rnd_.GetSimpleRandomNum() % SkipListOption::kBranching) == 0)) {
	height++;
	}
	return height;
}

template <typename _Key, typename _KeyComparator, typename _Allocator>
typename SkipList<_Key, _KeyComparator, _Allocator>::Node* 
	SkipList<_Key, _KeyComparator, _Allocator>::FindGreaterOrEqual(const _Key& key, Node**prev)
{
	Node* cur = head_;		// 从头节点开始查找
	int level = GetMaxHeight() - 1; //从最高层开始查找
	while (true) {
		Node* next = cur->Next(level);	// 该层中下一个节点
		if(KeyIsAfterNode(key, next)) {
			cur = next;		// 待查找 key 比 next 大， 则在该层继续查找
		} else {
			if(prev != nullptr) prev[level] = x;

			if(level == 0){
				return next;	// 待查找 key 不大于 next， 则到底返回
			} else {
				level--;	// 待查找 key 不大于 next，且没到底，则往下查找
			}
		}
	}
}


template <typename _Key, typename _KeyComparator, typename _Allocator>
typename SkipList<_Key, _KeyComparator, _Allocator>::Node* 
	SkipList<_Key, _KeyComparator, _Allocator>::FindLessThan (const _Key& key) const
{
	Node* cur = head_;
	int level = GetMaxHeight() - 1;
	while (true) {
		Node* next = cur->Next(level);
		int cmp = (next == nullptr) ? 1 : comparator_.Compare(next->key, key);
		if(cmp >= 0){
			if(level == 0){
				return cur;
			} else {
				level--;
			}
		} else {
			cur = next;
		}
	}
}

template <typename _Key, typename _KeyComparator, typename _Allocator>
typename SkipList<_Key, _KeyComparator, _Allocator>::Node* 
	SkipList<_Key, _KeyComparator, _Allocator>::FindLast() const
{
	Node* cur = head_;
	int level = GetMaxHeight() - 1;
	while (true) {
		Node* next = cur->Next(level);
		if (next == nullptr) {
			if (level == 0) {
				return cur;
			} else {
				level--;
			}
		} else {
			cur = next;
		}
	}
}

template <typename _Key, typename _KeyComparator, typename _Allocator>
	SkipList<_Key, _KeyComparator, _Allocator>::SkipList(_KeyComparator comparator)
	: comparator_(comparator)
	, cur_height_(1)
	, head_(NewNode(0, SkipListOption::kMaxHeight)) {
		for(int i = 0; i < SkipListOption::kMaxHeight; i++) {
			head_->SetNext(i, nullptr);
		}
}
template <typename _Key, typename _KeyComparator, typename _Allocator>
void SkipList<_Key, _KeyComparator, _Allocator>::Insert(const _Key& key)
{
	// 该对象记录的是要节点插入位置的前一个对象，本质上是链表的插入
	Node* prev[SkipListOption::kMaxHeight] = {nullptr};
	// 在key的构造过程中，有一个持续递增的序号，因此理论上不会有重复的key
	Node* node = FindGreaterOrEqual(key, prev);
	if(nullptr != node) {
		if (Equal(key, node->key)){
			// TODO: 此处应该使用格式化日志输出错误信息，logger还没有实现
			std::cout<<"WARN: key "<<key<< "has existed"<<std::endl;
			return;
		}
	}

	int new_level = RandomHeight();
	int cur_max_level = GetMaxHeight();
	if( new_level > cur_max_level) {
		// 因为skiplist存在多层， 而刚开始的时候只是分配kMaxHeight个空间， 每一层的next并没有真正使用
		for (int index = cur_max_level; index < new_level; ++index)
		{
			prev[index] = head_;
		}
		// 更新当前的最大值
		// 此处不用为并发读加锁（出现并发读的情况是，在另外的进程中，通过FindGreaterOrEqual中的GetMaxHeight）
		// 并发读在读取到更新以后的跳表层数而该节点还没有插入时也不会出错，因为此时回读取到nullptr
		// 而在存储引擎的比较器comparator设定中， nullptr比所有key都大，而并发读的情况是在另外的进程中，通过FindGreaterOrEqual中的GetMaxHeight访问cur_height_，实际上不影响FindGreaterOrEqual的调用结果
		cur_height_.store(new_level, std::memory_order_relaxed);
	}
	Node* new_node = NewNode(key, new_level);
	for(int index = 0; index < new_level; ++index) {
		// 此句 NoBarrier_SetNext() 版本就够用了，因为后续 prev[i]->SetNext(i, x) 语句会进行强制同步。
    		// 并且为了保证并发读的正确性，一定要先设置本节点指针，再设置原条表中节点（prev）指针
		new_node->NoBarrier_SetNext(index, prev[index]->NoBarrier_Next(index));
		prev[index]->SetNext(index, new_node);
	}
}

template <typename _Key, typename _KeyComparator, typename _Allocator>
bool SkipList<_Key, _KeyComparator, _Allocator>::Contains(const _Key& key) const
{
	Node* node = FindGreaterOrEqual(key, nullptr);
	return nullptr != node && Equal(key, node->key);
}

template <typename _Key, typename _KeyComparator, typename _Allocator>
inline SkipList<_Key, _KeyComparator, _Allocator>::Iterator::Iterator(const SkipList* list)
	: list_(list)
	, node_(nullptr)
{}

template <typename _Key, typename _KeyComparator, typename _Allocator>
inline bool SkipList<_Key, _KeyComparator, _Allocator>::Iterator::Valid() const
{
	return node_ != nullptr;
}

template <typename _Key, typename _KeyComparator, typename _Allocator>
inline _Key& SkipList<_Key, _KeyComparator, _Allocator>::Iterator::key() const 
{
	assert(Valid());
	return node_->key;
}

template <typename _Key, class _KeyComparator, typename _Allocator>
inline void SkipList<_Key, _KeyComparator, _Allocator>::Iterator::Next() {
  assert(Valid());
  node_ = node_->Next(0);
}

template <typename _Key, class _KeyComparator, typename _Allocator>
inline void SkipList<_Key, _KeyComparator, _Allocator>::Iterator::Prev(){
	// 相比在节点中额外增加一个 prev 指针，我们使用从头开始的查找定位其 prev 节点
	assert(Valid());
	node_ = list_->FindLessThan(node_->key);
	if (node_ == list_->head_) {
		node_ = nullptr;
	}
}

template <typename _Key, class _KeyComparator, typename _Allocator>
inline void SkipList<_Key, _KeyComparator, _Allocator>::Iterator::Seek(const _Key& target) {
	node_ = list_->FindGreaterOrEqual(target, nullptr);
}

template <typename _Key, class _KeyComparator, typename _Allocator>
inline void SkipList<_Key, _KeyComparator, _Allocator>::Iterator::SeekToFirst()
{
	node_ = list_->head_->Next(0);
}

template <typename _Key, class _KeyComparator, typename _Allocator>
inline void SkipList<_Key, _KeyComparator, _Allocator>::Iterator::SeekToLast() {
	node_ = list_->FindLast();
	if (node_ == list_->head_) {
		node_ = nullptr;
	}
}
}

