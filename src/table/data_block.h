#pragma once

#include <stdint.h>
#include <string>
#include <memory>

#include "../include/tinykv/iterator.h"

namespace tinykv
{
class Comparator;
class DataBlock {
public:
	explicit DataBlock(const std::string_view& contrnts);

	DataBlock(const DataBlock&) = delete;
	DataBlock& operator=(const DataBlock&) = delete;
	~DataBlock();
	size_t size() const { return size_; }
	Iterator* NewIterator(std::shared_ptr<Comparator> comparator);

private:

	uint32_t NumRestarts() const;

	const char* data_;
	size_t size_;
	uint32_t restart_offset_;
	bool owned_;
};
} // namespace tinykv