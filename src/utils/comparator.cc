#include "../include/tinykv/comparator.h"
#include "../include/tinykv/slice.h"

namespace tinykv {

Comparator::~Comparator() = default;

// Bytewise顾名思义按照字节比较，所以原理比较简单，下面就是具体实现了
class BytewiseComparatorImpl : public Comparator {
public:
    // 空的构造函数，也是，这种对象也不需要啥成员变量
    BytewiseComparatorImpl() { }
    // 实现获取名字的接口
    virtual const char* Name() const {
        return "tinykv.BytewiseComparator";
    }
    // 比较本身就依赖Slice.compare()函数就可以了，本身Slice.compare()就是用memcmp()实现的，符合按字节比较的初心
    virtual int Compare(const Slice& a, const Slice& b) const {
        return a.compare(b);
    }
    // 在Comparator定义的时候就感觉就比较模糊，这回看看是怎么实现的？
    virtual void FindShortestSeparator(std::string* start, const Slice& limit) const {
        // 二者取最小的长度，避免越界访问
        size_t min_length = (std::min)(start->size(), limit.size());
        // 名字比较明确，就是第一个字节值不同的索引值，初始为0，这一点应该比较好理解，如果和limit相同，+1肯定就比limit大了
        // 所以要找到第一个和limit不同的字节，从理论上讲，应该*start<=limit，第一个不同的字符就是第一个比limit字节小的数
        // 当然也不排除有传错参数的情况
        size_t diff_index = 0;
        // 找到第一个不相同的字节值的位置
        while ((diff_index < min_length) && ((*start)[diff_index] == limit[diff_index])) {
            diff_index++;
        }
        // 也就是说*start和limit是相同的或者说limit是以*start为前缀的，所以肯定找不到一个字符串比*start大还比limit小
        if (diff_index >= min_length) {
        } else {
            // 取出这个字节值
            uint8_t diff_byte = static_cast<uint8_t>((*start)[diff_index]);
            // 这个字节不能是0xff，同时+1后也也要比limit相应字节小，至少能看出来0xff对于leveldb是有特殊意义的
            if (diff_byte < static_cast<uint8_t>(0xff) && diff_byte + 1 < static_cast<uint8_t>(limit[diff_index])) {
                // 把找到的那个字符+1，同时把字符串缩短到那个不同字符的位置，这样就是[*start,limit)区间最短的字符串了
                (*start)[diff_index]++;
                start->resize(diff_index + 1);
                assert(Compare(*start, limit) < 0);
            }
            // 其他情况就不对*start做任何修改，认为*start本身就是这个字符串了，那我们举一个例子：
            // *start=['a', 'a', 'a', 'c', 'd', 'e']
            //  limit=['a', 'a', 'a', 'b', 'd', 'e']
            // 上面这种情况是我认为输出*start=['a', 'a', 'a', 'c', 'e']可能会更好，为什么此处不这么做呢?
            // 除非就是用在列举以某个字符串为前缀的对象时候使用，找到第一个比前缀大的字符串
        }
    }
    // 找到第一个比*key大的最短字符串
    virtual void FindShortSuccessor(std::string* key) const {
        // 获取键的长度
        size_t n = key->size();
        // 遍历键的每个字节
        for (size_t i = 0; i < n; i++) {
            // 找到第一个不是0xff的字节
            const uint8_t byte = (*key)[i];
            if (byte != static_cast<uint8_t>(0xff)) {
                // 把这个位置的字节+1，然后截断就可以了，算是比较简单。
                (*key)[i] = byte + 1;
                key->resize(i+1);
                return;
            }
        }
        // 能到这里说明*key全部都是0xff，也就找不到相应的字符串了
    }
};
}