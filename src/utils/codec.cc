#include "codec.h"

namespace tinykv {
/**
 * Varint是一种比较特殊的整数类型，它包含有Varint32和Varint64两种，它相比于int32和int64最大的特点是长度可变。
 * 我们都知道sizeof(int32)=4，sizeof(int64)=8，但是我们使用的整型数据并不都需要这么长的位数
 * 举个例子，使用leveldb存储的键，很可能长度连一个字节都用不上，但是leveldb又不能确定用户键的大小范围，所以Varint就应运而生了。
 * 因为Varint没法用具体的结构体或者标准类型表达，所以使用的时候需要编码/解码(亦或是序列化/反序列化)过程，我们通过代码就可以清晰的了解Varint的格式了。
 */
/**
 * Varint是一种紧凑的表示数字的方法。它用一个或多个字节来表示一个数字，值越小的数字使用越少的字节数。
 * 这能减少用来表示数字的字节数。比如对于int32类型的数字，一般需要4个byte来表示。
 * 但是采用Varint，对于很小的int32类型的数字，则可以用1个byte来表示。
 * 当然凡事都有好的也有不好的一面，采用Varint表示法，大的数字则需要5个byte来表示。
 * 从统计的角度来说，一般不会所有的消息中的数字都是大数，因此大多数情况下，采用Varint 后，
 * 可以用更少的字节数来表示数字信息。
*/
void PutFixed32(std::string* dst, uint32_t value) {
  char buf[sizeof(value)];
  EncodeFixed32(buf, value);
  dst->append(buf, sizeof(buf));
}

void PutFixed64(std::string* dst, uint64_t value) {
  char buf[sizeof(value)];
  EncodeFixed64(buf, value);
  dst->append(buf, sizeof(buf));
}

// Varint32就是存储在dst中， 按照Vatint32格式封装的数据
// 的编码风格有点类似utf-8，字节从低到高存储的是整型的从低到高指定位数，
// 每个字节的最高位为标志位，为1代表后面(更高位)还有数，直到遇到一个字节最高位为0。
// 所以每个字节的有效位数只有7位，这样做虽然看似浪费了一些空间，
// 如果我们使用的整型数据主要集中在2M以内的话，那么我们反而节省了2个字节的空间。
/**
 * Varint中的每个byte的最高位bit有特殊的含义，如果该位为 1，表示后续的byte也是该数字的一部分，
 * 如果该位为0，则结束。其他的7 个bit都用来表示数字。
 * 因此小于128的数字都可以用一个byte表示。大于 128的数字，比如300，
 * 会用两个字节来表示：1010 1100 0000 0010
 */
char* EncodeVarint32(char* dst, uint32_t v) {
  // Operate on characters as unsigneds
  uint8_t* ptr = reinterpret_cast<uint8_t*>(dst);
  static const int B = 128;
  /**
   * 正常情况下，int需要32位，varint用一个字节的最高为做标识位，
   * 所以，一个字节只能存储7位，如果整数特别大，可能需要5个字节才能存放{5 * 8 - 5(标识位) > 32}，
   * 下面的if语句有5个分支，正好对应varint占用1到5个字节的情况。
   */
  // 小于128，存储空间为一个字节，[v]
  if (v < (1 << 7)) {
    *(ptr++) = v;
  }
  // 大于等于128小于16K用两个字节存储[v(7-13位), v(0-6位)|128]
  else if (v < (1 << 14)) {
    *(ptr++) = v | B;
    *(ptr++) = v >> 7;
  }
  // 大于等于16K小于2M用3个字节存储[v(14-20位)，v(7-13位)|128, v(0-6位)|128]
  else if (v < (1 << 21)) {
    *(ptr++) = v | B;
    *(ptr++) = (v >> 7) | B;
    *(ptr++) = v >> 14;
  }
  // 大于等于2M小于256M用4个字节存储[v(21-27位),v(14-20位)|128，v(7-13位)|128, v(0-6位)|128]
  else if (v < (1 << 28)) {
    *(ptr++) = v | B;
    *(ptr++) = (v >> 7) | B;
    *(ptr++) = (v >> 14) | B;
    *(ptr++) = v >> 21;
  }
  // 大于等于256M用5个字节存储[v(28-32位)，v(21-27位)|128,v(14-20位)|128，v(7-13位)|128, v(0-6位)|128]
  else {
    *(ptr++) = v | B;
    *(ptr++) = (v >> 7) | B;
    *(ptr++) = (v >> 14) | B;
    *(ptr++) = (v >> 21) | B;
    *(ptr++) = v >> 28;
  }
  return reinterpret_cast<char*>(ptr);
}

void PutVarint32(std::string* dst, uint32_t v) {
  char buf[5];
  char* ptr = EncodeVarint32(buf, v);
  dst->append(buf, ptr - buf);
}

char* EncodeVarint64(char* dst, uint64_t v) {
  static const int B = 128;
  uint8_t* ptr = reinterpret_cast<uint8_t*>(dst);
  while (v >= B) {
    *(ptr++) = v | B;
    v >>= 7;
  }
  *(ptr++) = static_cast<uint8_t>(v);
  return reinterpret_cast<char*>(ptr);
}

void PutVarint64(std::string* dst, uint64_t v) {
  char buf[10];
  char* ptr = EncodeVarint64(buf, v);
  dst->append(buf, ptr - buf);
}

void PutLengthPrefixedSlice(std::string* dst, const Slice& value) {
  PutVarint32(dst, value.size());
  dst->append(value.data(), value.size());
}

int VarintLength(uint64_t v) {
  int len = 1;
  while (v >= 128) {
    v >>= 7;
    len++;
  }
  return len;
}

const char* GetVarint32PtrFallback(const char* p, const char* limit,
                                   uint32_t* value) {
  uint32_t result = 0;
  for (uint32_t shift = 0; shift <= 28 && p < limit; shift += 7) {
    uint32_t byte = *(reinterpret_cast<const uint8_t*>(p));
    p++;
    if (byte & 128) {
      // More bytes are present
      result |= ((byte & 127) << shift);
    } else {
      result |= (byte << shift);
      *value = result;
      return reinterpret_cast<const char*>(p);
    }
  }
  return nullptr;
}

bool GetVarint32(Slice* input, uint32_t* value) {
  const char* p = input->data();
  const char* limit = p + input->size();
  const char* q = GetVarint32Ptr(p, limit, value);
  if (q == nullptr) {
    return false;
  } else {
    *input = Slice(q, limit - q);
    return true;
  }
}

const char* GetVarint64Ptr(const char* p, const char* limit, uint64_t* value) {
  uint64_t result = 0;
  for (uint32_t shift = 0; shift <= 63 && p < limit; shift += 7) {
    uint64_t byte = *(reinterpret_cast<const uint8_t*>(p));
    p++;
    if (byte & 128) {
      // More bytes are present
      result |= ((byte & 127) << shift);
    } else {
      result |= (byte << shift);
      *value = result;
      return reinterpret_cast<const char*>(p);
    }
  }
  return nullptr;
}

bool GetVarint64(Slice* input, uint64_t* value) {
  const char* p = input->data();
  const char* limit = p + input->size();
  const char* q = GetVarint64Ptr(p, limit, value);
  if (q == nullptr) {
    return false;
  } else {
    *input = Slice(q, limit - q);
    return true;
  }
}

bool GetLengthPrefixedSlice(Slice* input, Slice* result) {
  uint32_t len;
  if (GetVarint32(input, &len) && input->size() >= len) {
    *result = Slice(input->data(), len);
    input->remove_prefix(len);
    return true;
  } else {
    return false;
  }
}
}