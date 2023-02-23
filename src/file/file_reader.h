#pragma once

#include "../include/tinykv/status.h"

#include <string>

namespace tinykv {
// 封装顺序读文件的接口
class FileReader final {
public:
	explicit FileReader(const std::string& file_name);
	~FileReader();

	// 从offset处开始，读取长度为n的内容到result中
	DBStatus Read(uint64_t offset, size_t n, void* result) const;

private:
	int fd_ = -1;
};
}
