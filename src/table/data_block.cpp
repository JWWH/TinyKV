#include "../include/tinykv/comparator.h"
#include "../utils/codec.h"
#include "data_block.h"

#include <memory>

namespace tinykv {

// 反解析出来的实际restarts offset个数
uint32_t DataBlock::NumRestarts() const {
	assert(size_ >= sizeof(uint32_t));
	return DecodeFixed32(data_ + size_ - sizeof(uint32_t));
}

DataBlock::DataBlock(const std::string_view& contents) 
	: data_(contents.data())
	, size_(contents.size())
	, owned_(false) {
	if (size_ < sizeof(uint32_t)) {
		size_ = 0; // Error marker
	} else {
		// 最后一个保存的是restart总个数，因此最多保留的restart个数(剩余所有的都是restarts offset)
		size_t max_restart_allowed = (size_ - sizeof(uint32_t)) / sizeof(uint32_t);
		// restart个数
		uint32_t num_restart_size = NumRestarts();
		if (num_restart_size > max_restart_allowed) {
			size_ = 0;
		} else {
			// 重启点开始的位置，也是数据部分的总长度
			restart_offset_ = size_ - (1 + num_restart_size) * sizeof(uint32_t);
		}
	}
}

DataBlock::~DataBlock() {};

static inline const char* DecodeEntry(const char* p, const char* limit,
					uint32_t* shared, uint32_t* non_shared, uint32_t* value_length) {
	/** 
	 * DataBlock中的每一条record的格式是：
	 * +------------+-------------+-----------+-------------+-----------+
	 * | key共享长度 ｜ key非共享长度 | value长度 ｜key非共享内容 ｜ value内容 ｜
	 * +------------+-------------+-----------+-------------+-----------+
	 */
	/**
	 * 每一个entry写入block是先写入key的共享长度（shared），非共享长度（non_shared）和value长度（value _length），
	 * 而对于表示长度的整型变量，前面说过leveldb是编码成varint存储的，
	 * 所以首先要对这三个数解码。
	 * 函数一开始假设这三个长度两都很小，编码成varint都只有一个字节，所以取出三个字节，
	*/
	if (limit - p < 3) return nullptr;
	*shared = reinterpret_cast<const uint8_t*>(p)[0];
	*non_shared = reinterpret_cast<const uint8_t*>(p)[1];
  	*value_length = reinterpret_cast<const uint8_t*>(p)[2];
	/**
	 * 然后通过:if ((*shared | *non_shared | *value_length) < 128)判断三个数的最高位是否有置为1，
	 * 因为在varint编码中，最高位为1代表后面的字节也属于这个数，
	 * 而如果有一个varint的长度大于1，自然假设也就不成立了。只好一一解码：
	 * 最后返回p代表此entry的key-value值首地址。
	*/
	if ((*shared | *non_shared | *value_length) < 128) {
		p+=3;
	} else {
		if ((p = GetVarint32Ptr(p, limit, shared)) == nullptr) return nullptr;
		if ((p = GetVarint32Ptr(p, limit, non_shared)) == nullptr) return nullptr;
		if ((p = GetVarint32Ptr(p, limit, value_length)) == nullptr) return nullptr;
	}
	if (static_cast<uint32_t>(limit - p) < (*non_shared + *value_length)) {
		return nullptr;
	}
	return p;
}

class DataBlock::Iter : public Iterator {
private:
	std::shared_ptr<Comparator> comparator_; // 比较器	
	const char* const data_;  //数据起始点 Block的字节流存储在data_中
	uint32_t const restarts_;  // 重启数组在block的偏移量
	uint32_t const num_restarts_;  	// 重启点的个数

	uint32_t current_;	//迭代器指向block内的数据偏移量，真实位置data_ + current_
	// 迭代器指向的数据所在重启区的索引，
	// 该区的重启点（起点）位置对应data_ + DecodeFixed32(data_ + restarts_ + restart_index_* sizeof(uint32_t))，
	// 如你所见，迭代器的大部分操作都是这样的底层字节移动，没有用易于明白的数组索引来实现
	uint32_t restart_index_; // restart_index_ 存储 current_ 前面最近的复活点偏移
	uint32_t offset_ = 0; // 下一个entry的起始位置相比与数据起始位置的offset
	// key_ 和 value_ 存储键值对。注意 key_ 是 std::string，
	// 因为有共享前缀，需要存储中间恢复的 Key，而 value_ 可以直接从 data_ 中截取。
	std::string key_;
	std::string value_;	
	DBStatus status_;

	inline int Compare(const std::string_view& a, const std::string_view& b) {
		return comparator_->Compare(a.data(), b.data());
	}

	// Return the offset in data_ just past the end of the current entry.
	inline uint32_t NextEntryOffset() const {
		return offset_;
		// return (value_.data() + value_.size()) - data_;
	}
	// data+restarts_表示整体restart的起点，根据偏移量来计算出所对应的restart offset 反解析出对应数据的位置
	uint32_t GetRestartPoint(uint32_t index) {
		assert(index < num_restarts_);
		return DecodeFixed32(data_ + restarts_ + index * sizeof(uint32_t));
	}
	void SeekToRestartPoint(uint32_t index) {
		key_.clear();
		// 起始点开始位置
		restart_index_ = index;
		// current_ will be fixed by ParseNextKey();

		// 重启点的位置
		offset_ = GetRestartPoint(index);
		value_ = std::string(data_ + offset_, 0);
	}
public:
	Iter(std::shared_ptr<Comparator> comparator, const char* data,
		uint32_t restarts, uint32_t num_restarts)
		: comparator_(comparator),
		data_(data),
		restarts_(restarts),
		num_restarts_(num_restarts),
		current_(restarts_),
		restart_index_(num_restarts_) {
			assert(num_restarts_ > 0);
	}
	~Iter() {}
	// 获取迭代器当前是否正常，比如到了结束为止该函数就会返回false
	bool Valid() const override { return current_ < restarts_;}
	DBStatus status() const override { return status_; }
	// 获取迭代器当前定位对象的键，前提是Valid()返回true
	Slice key() const override {
		assert(Valid());
		return key_;
	}
	// 获取迭代器当前定位对象的值，前提是Valid()返回true
	Slice value() const override {
		assert(Valid());
		return value_;
	}
	void Next() override {
		assert(Valid());
		ParseNextKey();
	}

	void Prev() override {
		assert(Valid());

		// Scan backwards to a restart point before current_
		const uint32_t original = current_;
		while (GetRestartPoint(restart_index_) >= original) {
			if (restart_index_ == 0) {
				// No more entried
				current_ = restarts_;
				restart_index_ = num_restarts_;
				return;
			}
			restart_index_--;
		}

		SeekToRestartPoint(restart_index_);
		do {
			// Loop until end of current entry hits the start of original entry
		} while (ParseNextKey() && NextEntryOffset() < original);
	}

	// 根据key二分查找
	void Seek(const Slice& target) override {
		// Binary search in restart array to find the last restart point
		uint32_t left = 0;
		// num_restarts_: 表示当前总个数
		uint32_t right = num_restarts_ - 1;
		// 二分法
		while (left < right)
		{
			uint32_t mid = (left + right + 1) / 2;
			// 获取对应的重启点的位置
			uint32_t region_offset = GetRestartPoint(mid);
			uint32_t shared, non_shared, value_length;
			// 反解析出完整的key
			const char* key_ptr =
				DecodeEntry(data_ + region_offset, data_ + restarts_, &shared,
					&non_shared, &value_length);
			if (key_ptr == nullptr || (shared != 0)) {
				CorruptionError();
				return;
			}
			// restart_point处的entry保存的是完整的key，所以shard字段是0
			std::string mid_key(key_ptr, non_shared);
			// 比较两个key的大小
			if (Compare(mid_key, target.ToString()) < 0) {
				// Key at "mid" is smaller than "target".  Therefore all
        			// blocks before "mid" are uninteresting.
				left = mid;
			} else {
				// Key at "mid" is >= "target".  Therefore all blocks at or
        			// after "mid" are uninteresting.
				right = mid - 1;
			}
		}
		// 定位到当前key所在的重启点的位置， 当然有可能不存在
		// 需要找到第一个key大于等于target的数据
		// Linear search (within restart block) for first key >= target
		SeekToRestartPoint(left);

		while(true) {
			if(!ParseNextKey()) {
				return;
			}
			// 找到当前第一个大于等于目标key的实体
			if (Compare(key_, target.ToString()) >= 0) {
				return;
			}
		}
	}

	void SeekToFirst() override {
		SeekToRestartPoint(0);
		ParseNextKey();
	}

	void SeekToLast() override {
		SeekToRestartPoint(num_restarts_ - 1);
		while (ParseNextKey() && NextEntryOffset() < restarts_) {
			// keep skipping
		}
	}

private:
	void CorruptionError() {
		current_ = restarts_;
		restart_index_ = num_restarts_;
		status_ = Status::kInterupt;
		key_.clear();
		value_.clear();
	}
	bool ParseNextKey() {
		// 获取下一个entry距离数据起始位置的offset
		current_ = NextEntryOffset();
		// 获取下一个entry的起始位置
		const char* p = data_ + current_;
		// limit是整个数据部分(也就是所有recode部分，不包括重启点)的结束位置距离数据起始位置的offset
		const char* limit = data_ + restarts_;
		// 如果超出了限制，那说明前面的二分查找没找到
		if (p >= limit) {
			// No more entries to return.  Mark as invalid.
			current_ = restarts_;
			restart_index_ = num_restarts_;
			return false;
		}
		// Decode next entry
		uint32_t shared, non_shared, value_length;
		// DecodeEntry返回的p代表此entry的key-value值首地址。
		p = DecodeEntry(p, limit, &shared, &non_shared, &value_length);
		if (p == nullptr || key_.size() < shared) {
			// 目前key_还是上一个entry的key
			// 如果上一个entry的key小于最小共享前缀的长度(也就是shared)，这种情况是错误的
			CorruptionError();
			return false;
		} else {
			// 此时key_还是上一个entry的key
			// key_.resize(shared) 就是获取最长前缀 
			key_.resize(shared);
			// 把当前key不与前一个key共享的部分加到后面
			key_.append(p, non_shared);
			value_ = std::string(p + non_shared, value_length);
			// 下一个entry的起始偏移量
			offset_ = (p - data_) + non_shared + value_length;
			// 更新restart_index_指针，到当前value所在的重启点数据的前一个
			while (restart_index_ + 1 < num_restarts_ &&
				GetRestartPoint(restart_index_ + 1) < current_) {
				++restart_index_;
			}
			return true;
		}
	}
};

Iterator* DataBlock::NewIterator(std::shared_ptr<Comparator> comparator) {
	if (size_ < sizeof(uint32_t)) {
		return NewErrorIterator(Status::kInterupt);
	}
	const auto num_restarts = NumRestarts();
	if (num_restarts == 0) {
		return NewEmptyIterator();
	} else {
		return new Iter(comparator, data_, restart_offset_, num_restarts);
	}
}
}
