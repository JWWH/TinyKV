#include "dbformat.h"
#include "../utils/codec.h"
#include "../utils/logging.h"

#include <cstdio>
#include <sstream>

namespace tinykv {

static uint64_t PackSequenceAndType(uint64_t seq, ValueType t) {
  assert(seq <= kMaxSequenceNumber);
  assert(t <= kValueTypeForSeek);
  return (seq << 8) | t;
}

void AppendInternalKey(std::string* result, const ParsedInternalKey& key) {
  result->append(key.user_key.data(), key.user_key.size());
  PutFixed64(result, PackSequenceAndType(key.sequence, key.type));
}

std::string ParsedInternalKey::DebugString() const {
  std::ostringstream ss;
  ss << '\'' << EscapeString(user_key.ToString()) << "' @ " << sequence << " : "
     << static_cast<int>(type);
  return ss.str();
}

const char* InternalKeyComparator::Name() const {
  return "leveldb.InternalKeyComparator";
}

// 比较两个Slice
int InternalKeyComparator::Compare(const Slice& akey, const Slice& bkey) const {
	// 采用BytewiseComparatorImpl.compare()进行比较，但是比较的是刨除顺序ID的用户提供的键
        // 为什么要刨除顺序ID呢？看看下面的注释就知道了
        int r = user_comparator_->Compare(ExtractUserKey(akey), ExtractUserKey(bkey));
        // 用户提供的键相等，意味着遇到了对象删除或者修改操作，需要再比较一下序列号。
        if (r == 0) {
            // 要返序列化(解码)顺序号
            const uint64_t anum = DecodeFixed64(akey.data() + akey.size() - 8);
            const uint64_t bnum = DecodeFixed64(bkey.data() + bkey.size() - 8);
            // 顺序ID越大，说明插入时间越晚，数据就越新，如果排序的话应该排在前面，这个在LSM算法有说明
            // 这就是为什么要针对顺序ID要特殊处理一下
            if (anum > bnum) {
                r = -1;
            } else if (anum < bnum) {
                r = +1;
            }
        }
        return r;
}

// 获取一个最短的字符串在[*start，limit)范围内
void InternalKeyComparator::FindShortestSeparator(std::string* start,
                                                  const Slice& limit) const {
	// 先把顺序号去了，把用户传入的key提取出来
        Slice user_start = ExtractUserKey(*start);
        Slice user_limit = ExtractUserKey(limit);
        // 调用比较器的FindShortestSeparator()会修改传入参数，所以先存在临时变量中
        std::string tmp(user_start.data(), user_start.size());
        // 调用BytewiseComparatorImpl.FindShortestSeparator()
        user_comparator_->FindShortestSeparator(&tmp, user_limit);
        // 看看是否找到了字符串，因为只要找到了那个字符串，长度肯定会被截断的。
        if (tmp.size() < user_start.size() && user_comparator_->Compare(user_start, tmp) < 0) {
            // 追加顺序ID，此时的顺序ID没什么大作用，所以用最大顺序ID就可以
            PutFixed64(&tmp, PackSequenceAndType(kMaxSequenceNumber,kValueTypeForSeek));
            assert(this->Compare(*start, tmp) < 0);
            assert(this->Compare(tmp, limit) < 0);
            start->swap(tmp);
        }
}

// 获取一个比*key大的最短字符串，原理和FindShortestSeparator很相似，我就不做重复注释了
void InternalKeyComparator::FindShortSuccessor(std::string* key) const {
Slice user_key = ExtractUserKey(*key);
        std::string tmp(user_key.data(), user_key.size());
        user_comparator_->FindShortSuccessor(&tmp);
        if (tmp.size() < user_key.size() && user_comparator_->Compare(user_key, tmp) < 0) {
            PutFixed64(&tmp, PackSequenceAndType(kMaxSequenceNumber,kValueTypeForSeek));
            assert(this->Compare(*key, tmp) < 0);
            key->swap(tmp);
	}
}

}