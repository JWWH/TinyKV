#include "memtable_iterator.h"

namespace tinykv {
// 为“target”编码一个合适的internal key目标并返回它。使用*scratch作为暂存空间，返回的指针将指向这个暂存空间
static const char* EncodeKey(std::string* scratch, const Slice& target) {
	scratch->clear();
	PutVarint32(scratch, target.size());
	scratch->append(target.data(), target.size());
	return scratch->data();
}

// static 只在当前文件可见
static Slice GetLengthPrefixedSlice(const char* data) {
  uint32_t len;
  const char* p = data;
  p = GetVarint32Ptr(p, p + 5, &len);  // +5: we assume "p" is not corrupted
  return Slice(p, len);
}

void MemTableIterator::Seek(const Slice& k)  { 
	iter_.Seek(EncodeKey(&tmp_, k)); 
}

// 获取值
Slice MemTableIterator::key() const { 
	return GetLengthPrefixedSlice(iter_.key()); 
}
	// 获取值
Slice MemTableIterator::value() const {
	Slice key_slice = GetLengthPrefixedSlice(iter_.key());
	return GetLengthPrefixedSlice(key_slice.data() + key_slice.size());
}
}