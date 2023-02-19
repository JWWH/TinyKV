#pragma once

#include <string>
#include <cstdint>

#include <cassert>
#include <cstddef>
#include <cstring>

namespace tinykv {
class Slice {
public:
	// 没有任何参数的构造函数，默认数据指向一个空字符串，而不是NULL
    Slice() : data_(""), size_(0) { }
    // 通过字符串指针和长度构造Slice，此方法可以用于截取部分字符串，也可以用于binary类型数据
    // 我好奇的是为什么没有用const void*作为d的参数类型，这样会更通用一点
    Slice(const char* d, size_t n) : data_(d), size_(n) { }
    // 通过std::string构造Slice，因为没有用explicit声明，说明可以Slice s = std::string()方式赋值
    Slice(const std::string& s) : data_(s.data()), size_(s.size()) { }
    // 通过字符串指针构造Slice，大小就是字符串长度
    Slice(const char* s) : data_(s), size_(strlen(s)) { }
    // 返回数据指针，返回类型是const char*而不是void*需要注意一下
    const char* data() const { return data_; }
    // 返回数据长度
    size_t size() const { return size_; }
    // 判断Slice是否为空，仅通过size_等于0，因为默认构造函数data=""，不是NULL,所以不能用空指针判断
    bool empty() const { return size_ == 0; }
    // 重载了[]运算符，那么就可以像数组一样访问Slice了，返回的类型是char型
    char operator[](size_t n) const {
        assert(n < size());
        return data_[n];
    }
    // 清空Slice
    void clear() { data_ = ""; size_ = 0; }
    // 删除前面一些数据
    void remove_prefix(size_t n) {
        assert(n <= size());
        // 对于Slice来说就是指针偏移
        data_ += n;
        size_ -= n;
    }
    // 把Slice转换为std::string
    std::string ToString() const { return std::string(data_, size_); }
    // 比较函数，基本上和std::string是相同的比较方法
    int compare(const Slice& b) const {
        // 取二者最小的长度，避免越界
        const size_t min_len = (size_ < b.size_) ? size_ : b.size_;
        // 直接使用memcmp()函数实现
        int r = memcmp(data_, b.data_, min_len);
        // 二者相等还有其他情况，那就是连个Slice的长度不同时，谁的更长谁就更大
        if (r == 0) {
            if (size_ < b.size_) r = -1;
            else if (size_ > b.size_) r = +1;
        }
        return r;     
    }
    // 判断Slice是不是以某个Slice开头的，这个普遍是拿Slice作为key使用的情况，因为leveldb的key是字符串型的，而且经常以各种前缀做分类
    bool starts_with(const Slice& x) const {
        return ((size_ >= x.size_) && (memcmp(data_, x.data_, x.size_) == 0));
    }

private:
	// 就两个成员变量，一个指向数据，一个记录大小
    const char* data_;
    size_t size_;
};
// 同时还重载了两个全局的运算符==和!=
inline bool operator==(const Slice& x, const Slice& y) {
    return ((x.size() == y.size()) && (memcmp(x.data(), y.data(), x.size()) == 0));
}
inline bool operator!=(const Slice& x, const Slice& y) {
    return !(x == y);
}
}