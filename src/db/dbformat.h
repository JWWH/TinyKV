#pragma once

#include "../include/tinykv/slice.h"
#include "../include/tinykv/comparator.h"
#include "../utils/codec.h"

#include <string>

namespace tinykv {

// SequenceNumber是一个无符号64位整型的值，我们这里用“顺序号”这个名字。
// tinykv每添加/修改一次记录都会触发顺序号的+1。
typedef uint64_t SequenceNumber;

// We leave eight bits empty at the bottom so a type and sequence#
// can be packed together into 64-bits.
static const SequenceNumber kMaxSequenceNumber = ((0x1ull << 56) - 1);

/**
 * @brief 
 * ValueType我们直译成“值类型”，在leveldb中，值的类型只有两种，一种是有效数据，一种是删除数据。
 * 因为值类型主要和对象键配合使用，这样就可以知道该对象是有值的还是被删除的。
 * 在leveldb中更新和删除都不会直接修改数据，而是新增一条记录，后期合并会删除老旧数据。
 */
// 代码源自leveldb/db/dbformat.h
enum ValueType {
    kTypeDeletion = 0x0,                               // 删除
    kTypeValue = 0x1                                   // 数据
};
// 在查找对象时，对象不能是被删除的，所以kValueTypeForSeek等于kTypeValue。
static const ValueType kValueTypeForSeek = kTypeValue; // 用于查找

struct ParsedInternalKey {
  Slice user_key;
  SequenceNumber sequence;
  ValueType type;

  ParsedInternalKey() {}  // Intentionally left uninitialized (for speed)
  ParsedInternalKey(const Slice& u, const SequenceNumber& seq, ValueType t)
      : user_key(u), sequence(seq), type(t) {}
  std::string DebugString() const;
};

// Append the serialization of "key" to *result.
void AppendInternalKey(std::string* result, const ParsedInternalKey& key);

// Returns the user key portion of an internal key.
// | klength(varint32) | internal key[klength] | vlength(varint32) | value[vlength] |
// internal key[klength] : | user key[klength-8] | sequence(7 bytes) | type(1 byte) |             
inline Slice ExtractUserKey(const Slice& internal_key) {
  assert(internal_key.size() >= 8);
  return Slice(internal_key.data(), internal_key.size() - 8);
}

/**
 * @brief 
 * 用户在使用tinykv的时候用Slice作为key,但是在tinykv内部是以InternalKey作为Key的
 * 内部键格式是在用户指定的键(Slice)基础上追加了按照小端方式存储的(顺序号<<8+值类型)，
 * 即便是用std::string类型存储的，但是已经不再是纯粹的字符串了。
 */
class InternalKey {
private:
    // 只有一个私有成员变量，也就是说把Slice变成了std::string类型
    std::string rep_;
public:
    // 构造函数，这个没参数，但是可以通过DecodeFrom()再设置
    InternalKey() { }
    // 内部键包含:用户指定的键Slice、顺序号、以及值类型
    InternalKey(const Slice& user_key, SequenceNumber s, ValueType t) {
        // 最终的内部键的格式为:[Slice]+[littleendian(SequenceNumber<<8 + ValueType)],代码实现读者自己看吧 
        AppendInternalKey(&rep_, ParsedInternalKey(user_key, s, t));
    }
    // 从一个Slice中解码内部键，就是直接按照字符串赋值，所以此处的Slice非user_key
    // 而是别的InternalKey.Encode()接口输出的
    void DecodeFrom(const Slice& s) { rep_.assign(s.data(), s.size()); }
    // 把内部键编码成Slice格式，其实就是用Slice封装一下，注意调用这个函数后InternalKey对象不能析构
    // 因为Slice不负责内存管理
    Slice Encode() const {
        assert(!rep_.empty());
        // 直接返回std::string类型？上面我们介绍Slice时候说过，Slice有一个构造函数参数就是std::string类型
        // 并且没有声明为explicit，所以可以将std::string赋值给Slice
        return rep_;
    }
    // 返回用户键值，所以需要按照上面构造函数的格式提取出来，ExtractUserKey()读者自己看就行，很简单
    Slice user_key() const { return ExtractUserKey(rep_); }
    // ParsedInternalKeyd的类型下面会有介绍，比较简单
    void SetFrom(const ParsedInternalKey& p) {
        rep_.clear();
        // 这个函数调用和构造函数里面调用的是一个函数，所以就不多说了
        AppendInternalKey(&rep_, p);
    }
    // 清空接口
    void Clear() { rep_.clear(); }
};


/**
 * @brief 
 * 顾名思义，主要用在内部的Key比较器，范围也确定了，功能也确定了。
 * 其实InternalKeyComparator的主要功能还是通过BytewiseComparatorImpl实现，
 * 只是在BytewiseComparatorImpl基础上做了一点扩展。
 */
class InternalKeyComparator : public Comparator {
private:
    // 这个是最关键的了，有一个用户定义的比较器，不用多说了，肯定是BytewiseComparatorImpl的对象啦
    // 所以我前面说InternalKeyComparator绝大部分是通过BytewiseComparatorImpl实现的
    const Comparator* user_comparator_;
public:
    // 构造函数需要提供比较器对象
    explicit InternalKeyComparator(const Comparator* c) : user_comparator_(c) { }
    // 返回比较器的名字
    const char* Name() const override;
    // 比较两个Slice
    int Compare(const Slice& a, const Slice& b) const override;
    // 获取一个最短的字符串在[*start，limit)范围内
    void FindShortestSeparator(std::string* start,
                             const Slice& limit) const override;
    // 获取一个比*key大的最短字符串，原理和FindShortestSeparator很相似，我就不做重复注释了
    void FindShortSuccessor(std::string* key) const override;
    // 获取用户传入的比较器指针
    const Comparator* user_comparator() const { return user_comparator_; }
    // 比较内部键，内部键存储格式就是[用户键][顺序号<<8|值类型]，所以重新封装成Slice后复用上面提到的比较函数
    int Compare(const InternalKey& a, const InternalKey& b) const {
	return Compare(a.Encode(), b.Encode());
    }
    
};

/**
 * @brief 
 * 当需要在leveldb查找对象的时候，查找顺序是从第0层到第n层遍历查找，
 * 找到为止(最新的修改或者删除的数据会优先被找到，所以不会出现一个键有多个值的情况)。
 * 由于不同层的键值不同，所以LookupKey提供了不同层所需的键值。
 */
// 因为查找可能需要查找memtable和sst，所以存储的内容要包含多种存储结构的键值
/**
 * Memtable的查询接口传入的是LookupKey，它也是由User Key和Sequence Number组合而成的，从其构造函数：LookupKey(const Slice& user_key, SequenceNumber s)中分析出LookupKey的格式为：

| Size (int32变长)| User key (string) | sequence number (7 bytes) | value type (1 byte) |

注意：

这里的Size是user key长度+8，也就是整个字符串长度了；
value type是kValueTypeForSeek，它等于kTypeValue。
由于LookupKey的size是变长存储的，因此它使用kstart_记录了user key string的起始地址，否则将不能正确的获取size和user key；
 */
class LookupKey {
public:
    // 构造函数需要用户指定的键以及leveldb内部
    LookupKey(const Slice& user_key, SequenceNumber sequence) {
        // 获取用户指定的键的长度
        size_t usize = user_key.size();
        // 为什么扩展了13个字节，其中8字节(64位整型顺序号<<8+值类型)+5字节(内部键的长度Varint32)
        size_t needed = usize + 13;  // A conservative estimate
         // 内部有一个固定大小的空间，200个字节，这样可以避免频繁的内存分配，因为200个字节可以满足绝大部分需求
        char* dst;
        if (needed <= sizeof(space_)) {
            dst = space_;
        } else {
            dst = new char[needed];
        }
        // 记录一下起始地址，在对象析构的时候需要释放空间(如果是从堆上申请的空间)
        start_ = dst;
        // 起始先存储内部键的长度，这个长度是Varint32类型
        dst = EncodeVarint32(dst, usize + 8);
        // 接着就是用户指定的键值
        kstart_ = dst;
        memcpy(dst, user_key.data(), usize);
        dst += usize;
        // 最后是64位的(顺序号<<8|值类型)，此处值类型是kValueTypeForSeek，和类名LookupKey照应上了
        EncodeFixed64(dst, PackSequenceAndType(s, kValueTypeForSeek));
        dst += 8;
        // 记录结束位置，可以用于计算各种类型键值长度
        end_ = dst;
        // 整个存储空间的结构为[内部键大小(Varint32)][用户指定键][顺序号<<8|值类型]
    }
    // 析构函数，因为有申请内存的可能性，所以析构函数还是要有的
    ~LookupKey() {
        // 要判断一下内存是否为堆上申请的，如果是就释放内存
        if (start_ != space_) delete[] start_;
    }
    // 获取memtable需要的键值，从这里我们知道memtable需要的是内存中全部内容
    Slice memtable_key() const { return Slice(start_, end_ - start_); }
    // 获取内部键
    Slice internal_key() const { return Slice(kstart_, end_ - kstart_); }
    // 获取用户指定键
    Slice user_key() const { return Slice(kstart_, end_ - kstart_ - 8); }
 
private:
    const char* start_;  // 指向存储空间的起始位置
    const char* kstart_; // 指向用户指定键/内部键的起始位置
    const char* end_;    // 指向键值的结尾
    char space_[200];    // 这样可以避免频繁申请内存
};
}