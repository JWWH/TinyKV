#include "../include/tinykv/filter_policy.h"

namespace tinykv {
class BloomFilter final : public FilterPolicy {
public:
    BloomFilter(int32_t bits_per_key);
    BloomFilter(int32_t entries_nums, float positive);

    // ~BloomFilter() = default;

    // 当前过滤器的名字
    const char* Name() override;
    // 创建过滤器
    void CreateFilter(const std::string* keys, int n) override;
    // 判断key是否在过滤器中
    bool MayMatch(const std::string& key, int32_t start_pos,
			int32_t len) override;
    const std::string& Data() override { return bloomfilter_data_; };
    // 返回当前过滤器底层对象的空间占用
    uint32_t Size() override { return bloomfilter_data_.size(); };

private:
    void CalcBloomBitsPerKey(int32_t entries_num, float positive = 0.01);
    void CalcHashNum();
    // 每个key占用的bit位数
    int32_t bits_per_key_;
    // 哈希函数的个数
    int32_t k_;
    std::string bloomfilter_data_;
};
}