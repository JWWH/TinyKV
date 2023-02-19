#pragma once

#include "memtable.h"
#include "../include/tinykv/iterator.h"

#include <string>

namespace tinykv {
/// static 函数最好不要定义在头文件中
// // 为“target”编码一个合适的internal key目标并返回它。使用*scratch作为暂存空间，返回的指针将指向这个暂存空间
// static const char* EncodeKey(std::string* scratch, const Slice& target) {
// 	scratch->clear();
// 	PutVarint32(scratch, target.size());
// 	scratch->append(target.data(), target.size());
// 	return scratch->data();
// }

// // static 只在当前文件可见
// static Slice GetLengthPrefixedSlice(const char* data) {
//   uint32_t len;
//   const char* p = data;
//   p = GetVarint32Ptr(p, p + 5, &len);  // +5: we assume "p" is not corrupted
//   return Slice(p, len);
// }

class MemTableIterator : public Iterator {
public:
	explicit MemTableIterator(MemTable::Table* table) : iter_(table) {};

	MemTableIterator(const MemTableIterator&) = delete;
	MemTableIterator& operator=(const MemTableIterator&) = delete;

	~MemTableIterator() override = default;

	bool Valid() const override { return iter_.Valid(); }
	void Seek(const Slice& k) override;
	void SeekToFirst() override { iter_.SeekToFirst(); }
	void SeekToLast() override { iter_.SeekToLast(); }
	void Next() override { iter_.Next(); }
	void Prev() override { iter_.Prev(); }
	// 获取值
	Slice key() const;
	// 获取值
	Slice value() const;
	// 这个迭代器永远返回争取是怎么个意思？估计这个接口没用
    	virtual Status status() const { return Status::OK(); }

private:
	MemTable::Table::Iterator iter_;
	std::string tmp_;
};

}