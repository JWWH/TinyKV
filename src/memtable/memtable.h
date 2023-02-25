#pragma once

// #include "memtable_iterator.h"
#include "../db/dbformat.h"
#include "../memory/alloc.h"
#include "../include/tinykv/iterator.h"
#include "skiplist.h"

namespace tinykv {
class MemTable {
public:
	// 构造函数，需要提供IternalKeyComparator的对象，
	// 这说明在MemTable中是通过InternalKey进行排序的
	explicit MemTable(const InternalKeyComparator& Comparator);
	MemTable(MemTable&) = delete;
	MemTable& opertor=(const MemTable&) = delete;
	// 自己实现智能指针 => 此处注意面试
	void Ref() { ++refs_; }
	void Unref() {
		// 此处没有任何同步操作，说明MemTable的使用者必须做同步！！！！
		--refs_;
		assert(refs_ >=0 );
		if (refs_ <= 0) {
			delete this;
		}
	}
	// 评估一下当前的内存使用量， 不能无限制的使用下去， 到了一定量就要写入sst了
	size_t ApproximateMemoryUsage();
	// 创建迭代器，用来遍历MemTable中的对象
	Iterator* NewIterator();
	// 向MemTable中添加对象，提供了用户指定的键和值，同时还提供了顺序号和值类型，说明顺序号是上级别产生的
	// 如果是删除操作，value应该没有任何值
	void Add(SequenceNumber seq, ValueType type, const Slice& key, const Slice& value);
	// 有写就得有读，提供的是查询键，输出对象值和状态，并返回是否成功
	bool Get(const LookupKey& key, std::string* value, DBStatus* s);

private:
	// 设计模式，迭代器模式，C++ STL中容器和迭代器就是使用了迭代器模式，参考https://blog.csdn.net/weixin_45465612/article/details/118076401
	friend class MemTableIterator;
	// 私有的析构函数，要求使用者只能通过Unref()释放对象
	~MemTable();
	// 自定义比较器， 说明在InternalKey基础上又进行了扩展，但最终还是通过InternalKeyComparator实现的比较
	struct KeyComparator {
		const InternalKeyComparator comparator;
		explicit KeyComparator(const InternalKeyComparator& c) : comparator(c) {}
		// 重载operator()，说明KeyComparator是一个函数对象
		int operator()(const char* a, const char* b) const;
	};
	// 表是用SkipList(跳表)实现的
	typedef SkipList<const char*, KeyComparator, SimpleFreeListAlloc> Table;
	// 成员变量包括：比较器、引用计数、内存管理和跳表
	KeyComparator comparator_;
	int refs_;
	SimpleFreeListAlloc alloc_;
	Table table_;
};

}