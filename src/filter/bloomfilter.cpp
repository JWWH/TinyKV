#include "bloomfilter.h"
#include "../utils/hash_util.h"
#include "../utils/util.h"
#include "../utils/codec.h"
#include <cmath>
#include <stdint.h>

namespace tinykv{
BloomFilter::BloomFilter(int32_t bits_per_key)
	: bits_per_key_(bits_per_key)
{
	CalcHashNum();
}
BloomFilter::BloomFilter(int32_t entries_nums, float positive)
{
	if(entries_nums > 0)
	{
		CalcBloomBitsPerKey(entries_nums, positive);
	}
	CalcHashNum();
}

void BloomFilter::CalcBloomBitsPerKey(int32_t entries_num, float positive)
{
	float size = -1 * entries_num * logf(positive) / powf(0.69314718056, 2.0);
	bits_per_key_ = static_cast<int32_t>(ceilf(size / entries_num));
}
void BloomFilter::CalcHashNum()
{
	k_ = static_cast<int32_t>(bits_per_key_ * 0.69314718056);

	k_ = k_ < 1 ? 1 : k_;
	k_ = k_ > 30 ? 30 : k_;
}
// 当前过滤器的名字
const char* BloomFilter::Name()
{
	return "general_bloomfilter";
}
// 创建过滤器
void BloomFilter::CreateFilter(const std::string* keys, int n)
{
	if(n<=0 || !keys)
	{
		return;
	}

	int32_t bits = n * bits_per_key_;
	bits = bits < 64 ? 64 : bits;
	// bits向上取整
	const int32_t bytes = (bits + 7) / 8;
	bits = bytes * 8;

	const int32_t init_size = bloomfilter_data_.size();
	bloomfilter_data_.resize(init_size + bytes, 0);

	// 将bloomfilter_data_转成数组方便使用
	char* array = &(bloomfilter_data_)[init_size];

	// 对于每个key，计算哈希值，给相应的位置1
	for(int i = 0; i < n; i++)
	{
		uint32_t hash_val = hash_util::SimMurMurHash(keys[i].data(), keys[i].size());
		const uint32_t delta = (hash_val >> 17) | (hash_val << 15);
		for(int j = 0; j < k_; j++)
		{
			const uint32_t bitpos = hash_val % bits;
			array[bitpos / 8] |= (1 << (bitpos % 8));
			hash_val += delta;
		}
	}
}
// 判断key是否在过滤器中
bool BloomFilter::MayMatch(const std::string& key, int32_t start_pos,
		int32_t len)
{
	if(key.empty() || bloomfilter_data_.empty())
	{
		return false;
	}
	// 将bloomfilter_data_转成数组形式
	const char* array = bloomfilter_data_.data();
	// bloomfilter_data_的长度
	const size_t total_len = bloomfilter_data_.size();
	if(start_pos >= total_len)
	{
		return false;
	}
	if(len == 0)
	{
		len = total_len - start_pos;
	}

	std::string current_bloomfilter_data(array + start_pos, len);
	const char* cur_array = current_bloomfilter_data.data();
	const int32_t bits = len * 8;

	if(k_ > 30)
	{
		return true;
	}

	uint32_t hash_val = hash_util::SimMurMurHash(key.data(), key.size());
	const uint32_t delta = (hash_val >> 17) | (hash_val << 15);
	for(int32_t j = 0; j < k_; j++)
	{
		const uint32_t bitpos = hash_val % bits;
		if((cur_array[bitpos / 8] & (1 << (bitpos % 8)) == 0))
		{
			return false;
		}
		hash_val += delta;
	}
	return true;
}
bool BloomFilter::MayMatch(const std::string_view& key,
                           const std::string_view& bf_datas) {
  static constexpr uint32_t kFixedSize = 4;
  // 先恢复k_
  const auto& size = bf_datas.size();
  if (size < kFixedSize || key.empty()) {
    return false;
  }
  uint32_t k = util::DecodeFixed32(bf_datas.data() + size - kFixedSize);
  if (k > 30) {
    return true;
  }
  const int32_t bits = (size - kFixedSize) * 8;
  std::string_view bloom_filter(bf_datas.data(), size - kFixedSize);
  const char* cur_array = bloom_filter.data();
  uint32_t hash_val = hash_util::SimMurMurHash(key.data(), key.size());
  const uint32_t delta =
      (hash_val >> 17) | (hash_val << 15);  // Rotate right 17 bits
  for (int32_t j = 0; j < k; j++) {
    const uint32_t bitpos = hash_val % bits;
    if ((cur_array[bitpos / 8] & (1 << (bitpos % 8))) == 0) {
      return false;
    }
    hash_val += delta;
  }
  return true;
}
}