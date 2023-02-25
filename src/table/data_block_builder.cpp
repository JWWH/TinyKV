#include "data_block_builder.h"
#include "../utils/codec.h"

namespace tinykv {
DataBlockBuilder::DataBlockBuilder(const Options* options) : options_(options) {
	restarts_.emplace_back(0);
}

void DataBlockBuilder::Add(const std::string_view& key, const std::string_view& value) {
	if (is_finished_ || key.empty()) {
		return ;
	}
	// shared用来记录当前key和前一个key的公共部分的长度
	int32_t shared = 0;
	const auto& current_key_size_ = key.size();
	// 规定16个entry构建一个restart
	// 如果restart_pointer_counter小于16，需要进行前缀压缩
	if (restart_pointer_counter_ < options_->block_restart_interval) {
		// 当前key和前一个key的公共部分
		const auto& pre_key_size_ = pre_key_.size();

		while (shared < pre_key_size_ && shared < current_key_size_ && pre_key_[shared]==key[shared]) {
			++shared;
		}
	} else {
		// restart记录的是每一组record开始位置相比于这个DataBlock起始位置的偏移量
		restarts_.emplace_back(buffer_.size());
		restart_pointer_counter_ = 0;
	}
	const auto& non_shared_size = current_key_size_ - shared;
	const auto& value_size = value.size();
	PutVarint32(&buffer_, shared);
	PutVarint32(&buffer_, non_shared_size);
	PutVarint32(&buffer_, value_size);
	// 将当前的key和value序列化到buffer中
	buffer_.append(key.data() + shared, non_shared_size);
	buffer_.append(value.data(), value_size);
	// 更新pre_key，因为下次我们需要使用它
	pre_key_.assign(key.data(), current_key_size_);
	++restart_pointer_counter_;
}

void DataBlockBuilder::Finish() { AddRestartPointers(); }
void DataBlockBuilder::AddRestartPointers() {
	if (is_finished_) return;
	for (const auto& restart : restarts_) {
		PutFixed32(&buffer_, restart);
	}
	PutFixed32(&buffer_, restarts_.size());
	is_finished_ = true;
}
}