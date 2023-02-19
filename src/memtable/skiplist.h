#pragma once

#include "../utils/random_util.h"

#include <atomic>
#include <assert.h>
#include <stdint.h>
#include <iostream>

namespace tinykv{
struct SkipListOption {
  static const int32_t kMaxHeight = 20;	// 跳跃表最高高是20
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
	// 判断两个键是否相等，实现比较简单
	bool Equal(const _Key& a, const _Key& b) const { return (comparator_(a,b) == 0); }

	// 迭代skiplist，主要给MemTable中的MemIterator使用
	// 跳表的迭代器通过跳跃表的高度为0的链表形成了一个单向链表，只要获取头节点就可以正向遍历所有的节点
	// 但是如果要反向遍历或者定位到某一个节点，就需要依赖跳表的查找能力
	class Iterator {
		public:
		// Initialize an iterator over the specified list.
		// The returned iterator is not valid.
		// 构造函数，传入跳表指针
		explicit Iterator(const SkipList* list);

		// Returns true iff the iterator is positioned at a valid node.
		// 判断当前迭代器是否有效的接口
		bool Valid() const;

		// Returns the key at the current position.
		// REQUIRES: Valid()
		// 返回迭代器当前指向的节点的键，这个键不是用户指定的键，而是把key和value编码到一个内存块的值
		const _Key& key() const;

		// Advances to the next position.
		// REQUIRES: Valid()
		// 指向下一个节点，同样的使用者要保证Valid()返回true
		void Next();

		// Advances to the previous position.
		// REQUIRES: Valid()
		// 指向前一个节点，使用者要保证Valid()返回true
		void Prev();

		// Advance to the first entry with a key >= target
		// 根据key定位到指定的节点
		void Seek(const _Key& target);

		// Position at the first entry in list.
		// Final state of iterator is Valid() iff list is not empty.
		// 定位到第一个节点，直接访问跳跃表的表头的下一个节点就是第一个节点
		void SeekToFirst();

		// Position at the last entry in list.
		// Final state of iterator is Valid() iff list is not empty.
		// 定位到最后一个节点
		void SeekToLast();

		private:
		const SkipList* list_;	// 指向跳表
		Node* node_;		// 迭代器当前指向的节点
		// Intentionally copyable
	};

private:
	// 节点的构造函数
	/**
	 * 为什么只有构造Node的函数却没有析构Node的函数，主要原因如下：
	 * SkipList只有添加没有删除操作，这个我们在前面提到过，自然在过程中没有释放Node的过程；
	 * 即便SkipList整体释放掉，只需要把arena释放掉就可以了，因为Node内部本身没有内存申请操作，所以也就没必要执行析构函数了； 
	 */
	Node* NewNode(const _Key& key, int32_t height);
	// 随机的获取一个高度，本文不打算对这个"随机"做分析，所以暂时做到了解就可以
	int32_t RandomHeight();
	// 获取当前跳跃表的当前最大高度
	inline int32_t GetMaxHeight() { return cur_height_.load(std::memory_order_relaxed); }
	// 判断key是不是大于节点n的key，也就意味着如果存在key的节点，那么就会在节点n的后面
	bool KeyIsAfterNode(const _Key& key, Node* n) {
		// 所以实现方式就是键的比较
		return (nullptr != n && comparator_.Compare(n->key, key) < 0);
	}
	/**
	 * 该函数的含义为：在跳表中查找不小于给定 Key 的第一个值，如果没有找到，则返回 nullptr。
	 * 如果参数 prev 不为空，在查找过程中，记下待查找节点在各层中的前驱节点。
	 * 如果是查找操作，则指定 prev = nullptr 即可；
	 * 若要插入数据，则需传入一个合适尺寸的 prev 参数。
	 */
	// 找到第一个大于等于给定的键的节点，通过跳跃的方式查找
	Node* FindGreaterOrEqual(const _Key& key, Node**prev);
	// 返回第一个比key小的节点，通过跳跃的方式查找
	Node* FindLessThan(const _Key& key) const;
	// 返回skiplist的最后一个节点
	Node* FindLast() const;

private: 
	_KeyComparator comparator_;	// 比较器
	_Allocator arena_;		// 内存管理对象
	Node* head_ = nullptr;		// skiplist头节点
	std::atomic<int32_t> cur_height_;// 跳跃表的当前最大高度
	RandomUtil rnd_;		// 随机数生成器
};

// 实现SkipList的Node结构
template <typename _Key, typename _KeyComparator, typename _Allocator>
struct SkipList<_Key, _KeyComparator, _Allocator>::Node
{
	// 构造函数只有_Key类型，这个类型是const char*(定义在MemTable中)，其实包含了key和value，
	// 只存储指针，内存谁管理？
    	// 肯定是Arena啊，MemTable通过Arena申请内存存储key和value,在把指针交给SkipList
	explicit Node(const _Key& k) : key(k) {}
	// 一个Node负责一条记录，这个记录就是通过key指向
	const _Key key;
	// 采用内存屏障的方式获取下一个Node，其中n为高度
	Node* Next(int n) {
		assert(n >= 0);
		// std::memory_order_acquire: 用在 load 时，保证同线程中该 load 之后的对相关内存读写语句不会被重排到 load 之前，
		// 并且其他线程中对同样内存用了 store release 都对其可见。
		return next_[n].load(std::memory_order_acquire);
	}
	// 采用内存屏障的方式设置节点高度为n的下一个Node
	void SetNext(int n, Node* x) {
		assert(n >= 0);
		// std::memory_order_release：用在 store 时，保证同线程中该 store 之后的对相关内存的读写语句不会被重排到 store 之前，
		// 并且该线程的所有修改对用了 load acquire 的其他线程都可见。
		next_[n].store(std::memory_order_release);
	}

	// 不带内存屏障版本的访问器。内存屏障（Barrier）是个形象的说法，也即加一块挡板，阻止重排/进行同步
	// std::memory_order_relaxed：不对重排做限制，只保证相关共享内存访问的原子性。
	// 无内存屏障的方式获取下一个Node，其中n为高度
	Node* NoBarrier_Next(int n) {
		assert(n >= 0);
		return next_[n].load(std::memory_order_relaxed);
	}
	// 无内存屏障的方式设置节点高度为n的下一个Node
	void NoBarrier_SetNext(int n, Node* x) {
		assert(n >= 0);
		next_[n].store(x, std::memory_order_relaxed);
	}
private:
	// 指针数组的长度即为该节点的 level，next_[0] 是最低层指针.
	// 很多人看到这里懵逼了把？怎么只有一个元素的数组，上面的访问可都是按照最高高度访问的，这个是一个非常有意思的地方了
	// 在C++中，new一个对象其实就是malloc(sizeof(type))大小的内存，然后再执行构造函数的过程，delete先执行析构函数再free内存
	// 有没有发现这是一个结构体？next_[1]正好在结构体的尾部，那么申请内存的时候如果多申请一些内存
	// 那么通过索引的方式&next_[n]的地址就是多出来的那部分空间，所以可知Node不是通过普通的new出来的
	std::atomic<Node*> next_[1];

};
// 节点的构造函数
// Node的创建不是一个普通的new，在SkipList中提供了专门的构造函数
// 构造Node需要传入最大高度，这样申请的内存大小就会略有不同
template <typename _Key, typename _KeyComparator, typename _Allocator>
typename SkipList<_Key, _KeyComparator, _Allocator>::Node* 
	SkipList<_Key, _KeyComparator, _Allocator>::NewNode(const _Key& key, int32_t height)
{
	// TODO: 将Allocate替换成AllocateAligned
	// 首先内存申请不是malloc，而是通过arena申请的，每个Node的大小很小，非常适合arena
    	// sizeof(port::AtomicPointer) * (height - 1)就是为了扩展指针数组用的
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
// 找到第一个大于等于给定的键的节点，通过跳跃的方式查找
template <typename _Key, typename _KeyComparator, typename _Allocator>
typename SkipList<_Key, _KeyComparator, _Allocator>::Node* 
	SkipList<_Key, _KeyComparator, _Allocator>::FindGreaterOrEqual(const _Key& key, Node**prev)
{
	Node* cur = head_;		// 从头节点开始查找
	int level = GetMaxHeight() - 1; //从最高层开始查找
	while (true) {
		// 如果节点的key小于指定的key，那就从这节点继续往后逐渐向目标逼近，因为节点是从小到大有序的
		Node* next = cur->Next(level);	// 该层中下一个节点
		if(KeyIsAfterNode(key, next)) {
			cur = next;		// 待查找 key 比 next 大， 则在该层继续查找
		} else {
			// 走到这里，当前高度的下一个节点已经比指定的键小了，所以要降低一个一个高度继续搜索，输出当前高度的前一个节点指针
			if(prev != nullptr) prev[level] = cur;
			// 到了最底层了，那就直接返回下一个节点就可了，因为当前节点x的key要比给定的key小
			if(level == 0){
				return next;	// 待查找 key 不大于 next， 则到底返回
			} else {
				// 否则就下降一个高度继续逼近
				level--;	// 待查找 key 不大于 next，且没到底，则往下查找
			}
		}
	}
}
// 返回第一个比key小的节点，通过跳跃的方式查找
template <typename _Key, typename _KeyComparator, typename _Allocator>
typename SkipList<_Key, _KeyComparator, _Allocator>::Node* 
	SkipList<_Key, _KeyComparator, _Allocator>::FindLessThan (const _Key& key) const
{
	Node* cur = head_;		// 从表头开始
	int level = GetMaxHeight() - 1;	// 从最高高度开始，逐渐降低高度，减少跨度
	while (true) {
		// 相应高度的下一个节点
		Node* next = cur->Next(level);
		// 如果没有节点或者节点比给定的键大，那就降低一个高度
		int cmp = (next == nullptr) ? 1 : comparator_.Compare(next->key, key);
		if(cmp >= 0){
			// 如果已经是最低高度了，那当前的节点就是要找的节点了
			if(level == 0){
				return cur;
			} else {
				level--;
			}
		} else {
			// 如果当前节点比指定的键小，那么就从当前节点继续向指定的键逼近
			cur = next;
		}
	}
}
// 找到最后一个节点，通过跳跃的方式查找
template <typename _Key, typename _KeyComparator, typename _Allocator>
typename SkipList<_Key, _KeyComparator, _Allocator>::Node* 
	SkipList<_Key, _KeyComparator, _Allocator>::FindLast() const
{
	Node* cur = head_;		// 从表头开始
	int level = GetMaxHeight() - 1;	// 从最高高度开始
	while (true) {
		// 下一个节点如果为空，就代表当前这层的到结尾了
		Node* next = cur->Next(level);
		if (next == nullptr) {
			// 最底层的话当前节点就是最后一个了
			if (level == 0) {
				return cur;
			} else {
				// 否则的话就继续下降高度
				level--;
			}
		} else {
			// 继续完后找，直到当前层的结尾
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

// 向跳跃表中插入一条记录，我更倾向于叫记录而不是key，因为这个key是用户指定的key和value的编码后的值
template <typename _Key, typename _KeyComparator, typename _Allocator>
void SkipList<_Key, _KeyComparator, _Allocator>::Insert(const _Key& key)
{
	// 该对象记录的是要节点插入位置的前一个对象，本质上是链表的插入,这个临时变量用于记录所找到节点在各个高度的前向节点的指针
	Node* prev[SkipListOption::kMaxHeight] = {nullptr};
	// 在key的构造过程中，有一个持续递增的序号，因此理论上不会有重复的key
	// 找到第一个大于等于key的节点，因为我们要把新的记录插入到这个节点前面
	Node* node = FindGreaterOrEqual(key, prev);
	if(nullptr != node) {
		if (Equal(key, node->key)){
			// TODO: 此处应该使用格式化日志输出错误信息，logger还没有实现
			std::cout<<"WARN: key "<<key<< "has existed"<<std::endl;
			return;
		}
	}
	// 这个是leveldb比较有意思的地方，插入一个节点的高度是一个随机值，当然不是一个想象的随机值
        // 否则高度为10的节点和高度为1的节点数量相同，这本身就不符合跳跃表的特性，该随机函数高度越高
        // 生成的概率越低，近似成跳跃表对于节点高度的要求，我没有对随机算法做深入研究，这种用计算实现的策略算是比较好的
        // 但凡能用计算解决的就不要用各种if else，因为这样代码更优雅，就是可读性差一点
        // 虽然算法上实现比较优雅，但是可能存在一些风险，比如高度比较高的节点可能并没有分散开
	int new_level = RandomHeight();
	int cur_max_level = GetMaxHeight();
	// 是否已经超过了当前最大高度，由于跳跃表初始没有节点，所以最大高度可能为0，随着记录增多，高度慢慢提升
	if( new_level > cur_max_level) {
		// 因为skiplist存在多层， 而刚开始的时候只是分配kMaxHeight个空间， 每一层的next并没有真正使用
		// 超过了最大高度的话，具备此高度的节点只有头结点，所以把高出来的那部分前一个节点都指向头结点
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
	// 创建新的节点对象
	Node* new_node = NewNode(key, new_level);
	// 把节点连接到跳跃表中，当然是每个高度都是单独连接的
	for(int index = 0; index < new_level; ++index) {
		// 此句 NoBarrier_SetNext() 版本就够用了，因为后续 prev[i]->SetNext(i, x) 语句会进行强制同步。
    		// 并且为了保证并发读的正确性，一定要先设置本节点指针，再设置原条表中节点（prev）指针
		// 按照单项链表的方式指向：curr->prev.next， prev->curr
		new_node->NoBarrier_SetNext(index, prev[index]->NoBarrier_Next(index));
		prev[index]->SetNext(index, new_node);
	}
}
 // 判断跳跃表中是否有指定的数据，等同于std::map.find()
template <typename _Key, typename _KeyComparator, typename _Allocator>
bool SkipList<_Key, _KeyComparator, _Allocator>::Contains(const _Key& key) const
{
	// 实现方式是利用查找方式，找到的如果等于就返回true，否则返回false
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
	// 唯一能判断的只有node_指针了，迭代器每次操作都会更新node_指针
	return node_ != nullptr;
}

template <typename _Key, typename _KeyComparator, typename _Allocator>
inline _Key& SkipList<_Key, _KeyComparator, _Allocator>::Iterator::key() const 
{
	// 从这里来看，需要使用者再使用前必须通过Valid()判断一下，否则就要接受崩溃的后果了
	assert(Valid());
	return node_->key;
}

// 指向下一个节点，同样的使用者要保证Valid()返回true
template <typename _Key, class _KeyComparator, typename _Allocator>
inline void SkipList<_Key, _KeyComparator, _Allocator>::Iterator::Next() {
  assert(Valid());
  // 跳跃表的第0层所有节点的距离是1，所以通过第0层找下一个节点
  node_ = node_->Next(0);
}

// 指向前一个节点，使用者要保证Valid()返回true
template <typename _Key, class _KeyComparator, typename _Allocator>
inline void SkipList<_Key, _KeyComparator, _Allocator>::Iterator::Prev(){
	// 相比在节点中额外增加一个 prev 指针，我们使用从头开始的查找定位其 prev 节点
	assert(Valid());
	node_ = list_->FindLessThan(node_->key);
	if (node_ == list_->head_) {
		node_ = nullptr;
	}
}

// 根据key定位到指定的节点
template <typename _Key, class _KeyComparator, typename _Allocator>
inline void SkipList<_Key, _KeyComparator, _Allocator>::Iterator::Seek(const _Key& target) {
	node_ = list_->FindGreaterOrEqual(target, nullptr);
}

// 定位到第一个节点，直接访问跳跃表的表头的下一个节点就是第一个节点
template <typename _Key, class _KeyComparator, typename _Allocator>
inline void SkipList<_Key, _KeyComparator, _Allocator>::Iterator::SeekToFirst()
{
	node_ = list_->head_->Next(0);
}

// 定位到最后一个节点
template <typename _Key, class _KeyComparator, typename _Allocator>
inline void SkipList<_Key, _KeyComparator, _Allocator>::Iterator::SeekToLast() {
	// 通过SkipList找到最后一个节点
	node_ = list_->FindLast();
	// 如果返回的是表头的指针，那就说明链表中没有数据
	if (node_ == list_->head_) {
		node_ = nullptr;
	}
}
}

