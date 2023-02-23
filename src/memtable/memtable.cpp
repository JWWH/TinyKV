#include "memtable.h"
#include "memtable_iterator.h"

#include <stdlib.h>

namespace tinykv {
// static 只在当前文件可见
// GetLengthPrefixedSlice函数的作用就是提取内部键
// const char* data 的格式是:[内部键长度(varint32)][internalkey][值长度(varint32)][value]
// 使用 GetVarint32Ptr获取data中最前面的内部键长度(varint32类型)
// GetVarint32Ptr函数返回值是输入的data中内部键长度部分之后的位置，也就是internalKey的起始位置，len中保存的是internalKey的长度
static Slice GetLengthPrefixedSlice(const char* data) {
  uint32_t len;
  const char* p = data;
  p = GetVarint32Ptr(p, p + 5, &len);  //  +5是因为Varint32最长是5个字节，这样比较保险
  return Slice(p, len);
}
MemTable::MemTable(const InternalKeyComparator& Comparator)
	: comparator_(Comparator), refs_(0), table_(comparator_) {}

MemTable::~MemTable() { assert(refs_ == 0); }

size_t MemTable::ApproximateMemoryUsage() {
	return alloc_.MemoryUsage();
}
// 重载了运算符()，比较的两个对象是const char*类型， 这里一个buf存储一条记录
// 记录的存储格式是[内部键长度(varint32)][internalkey][值长度(varint32)][value]
int MemTable::KeyComparator::operator()(const char* aptr, const char* bptr) const {
	// 比较前就要先提取内部键然后在用InternalKeyComparator比较就可以了
	Slice a = GetLengthPrefixedSlice(aptr);
	Slice b = GetLengthPrefixedSlice(bptr);
	return comparator.Compare(a, b);
}

Iterator* MemTable::NewIterator() {
	return new MemTableIterator(&table_);
}

// 向MemTable中添加记录
void MemTable::Add(SequenceNumber seq, ValueType type, const Slice& key, const Slice& value) {
	// 因为存储到SkipList中的内容是把用户的键和值进行编码后的值, 
	// 格式为[内部键长度(varint32)][internalkey][值长度(varint32)][value]
	// 下面是具体的编码实现
	size_t key_size = key.size();
	size_t value_size = value.size();
	// 组装InternalKey，格式为| user key[klength-8] | sequence(7 bytes) | type(1 byte) |
	// InternalKey的长度是用户指定键+(顺序ID<<8|值类型)，所以长度是用户指定键的长度+8
	size_t internal_key_size = key_size + 8;
	// 编码后的数据[InernalKey长度(Varint32)][InternalKey][value长度(Varint32)][value]
    	// 所以整体编码后需要的内存长度就下面的算法
	const size_t encoded_len = VarintLength(internal_key_size) + internal_key_size + 
				VarintLength(value_size) + value_size;
	// 使用内存分配器分配内存
	char* buf = (char*)alloc_.Allocate(encoded_len);
	// 先存放InternalKey的长度，编码成Varint32
	char* p = EncodeVarint32(buf, internal_key_size);
	// 接着存放内部键
	memcpy(p, key.data(), key_size);
	p += key_size;
	EncodeFixed64(p, (seq << 8) | type);
	p += 8;
	// 存放value的长度
	p = EncodeVarint32(p, value_size);
	// 存放value
	memcpy(p, value.data(), value_size);
	assert(p + value_size == buf + encoded_len);
	table_.Insert(buf);
}
// 从MemTable获取对象，此时的键是LookupKey类型
// 如果能找到key对应的value, 将该value存储到*value参数中，返回值为true。
// 如果这个key中的有删除标识,存放一个NotFound()错误到*status参数中，返回值为true。
// 否则返回值为false
bool MemTable::Get(const LookupKey& key, std::string* value, DBStatus* s) {
	// 获取MemTable的键
	// 得到memkey，memkey中实际上包含了klength|userkey|tag，也就是说它包含了internal_key_size和internal_key
	Slice memKey = key.memtable_key();
	// 构造MemTable的迭代器
	Table::Iterator iter(&table_);
	// 定位到键的位置，那么问题来了，我们知道存储在MemTabled的键是InternalKey，而InternalKey里面包含顺序号
	// 在前面代码中我们知道，InternalKey的比较顺序号是参与比较的，那么获取对象的时候如何知道对象的顺序号的呢？
	// 其实LookupKey里面的保存的顺序号是“顺序号最大值”,而MemTable迭代器Seek定位的是第一个比指定键大或者等于的对象
	// InternalKey的比较顺序号越大越靠前，所以需要找的对象肯定会排在迭代器指的位置，所以需要接下来就要校验用户键
	// 找到SkipList中大于等于memkey的第一个节点
	iter.Seek(memKey.data());
	if (iter.Valid()) {
		// 获取对象值
		// 一个结点的结构如下所示
		// entry format is:
		//    klength  varint32
		//    userkey  char[klength]
		//    tag      uint64
		//    vlength  varint32
		//    value    char[vlength]
		// Check that it belongs to same user key.  We do not check the
		// sequence number since the Seek() call above should have skipped
		// all entries with overly large sequence numbers.
		const char* entry = iter.key();
		// 通过Varint32解码InternalKey的长度
		uint32_t key_length;
		// 取出klength，并将key_ptr指到klength之后
		const char* key_ptr = GetVarint32Ptr(entry, entry+5, &key_length);
		// 接下来就用用户提供的键比较器(BytewiseComparator)比较用户键，因为SkipList的Seek不是准确定位
        	// 毕竟他也没法准确定位，因为他不知道顺序号，所以要比较一下用户键是否相同
		// 比较结点中的userkey和LookupKey中的userkey是否相等，如果相等，说明找到了这个结点
		if (comparator_.comparator.user_comparator()->Compare(
			Slice(key_ptr, key_length-8),
			key.user_key()) == 0) {
			// 获取tag， tag等于(sequence<<8)|type
			const uint64_t tag = DecodeFixed64(key_ptr + key_length - 8);
			// 取出type并判断
			switch (static_cast<ValueType>(tag & 0xff)) {
				case kTypeValue : {
					// 取出value的大小和内容
					Slice v = GetLengthPrefixedSlice(key_ptr + key_length);
					value->assign(v.data(), v.size());
					return true;
				}
				case kTypeDeletion:
					// *s = Status::NotFound(Slice());
					*s = Status::kNotFound;
					return true;
			}
		}
	}
	return false;
}
}