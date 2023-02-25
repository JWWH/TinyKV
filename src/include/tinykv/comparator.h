#pragma once

#include <string>

namespace tinykv {

class Slice;

class Comparator {
public:
    // 虚析构函数，析构对象的时候会调用实现类的析构函数
    virtual ~Comparator();
    // 比较，这个和Slice的比较是一个意思，所以基本上就是用Slice.compare()实现的，直接用Slice.compare()不就完了么
    // 为什么还要定义这个类呢，后面会有各种实现类读者就知道为什么了
    virtual int Compare(const Slice& a, const Slice& b) const = 0;
    // 获取比较器的名字，这个不是很重要，也就是每个比较器有一个名字，我看基本都是实现类的全名(含namespace)
    virtual const char* Name() const = 0;
    // 通过函数名有点看不出来什么意思，这个函数需要实现的功能是找到一个最短的字符串，要求在[*start，limit)区间
    // 这是很有意思的功能，当需要找到以某些字符串为前缀的所有对象时，就会用到这个接口，等我们看到的时候会重点解释使用方式
    // 如果比较简单的比较器实现可以实现为空函数
    virtual void FindShortestSeparator(std::string* start, const Slice& limit) const = 0;
    // 这个接口和上一个接口很像，只是输出最短的比*key大的字符串，没有区间限制，如果比较简单的比较器可以实现为空函数
    virtual void FindShortSuccessor(std::string* key) const = 0;
};

// 按照字典序列
class ByteComparator final : public Comparator {
public:
	const char* Name() const override;
	int32_t Compare(const Slice& a, const Slice& b) const override;
	void FindShortestSeparator(std::string* start, const Slice& limit) const override;
};
}