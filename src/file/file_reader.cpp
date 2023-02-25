#include "file_reader.h"
#include "../logger/log.h"

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <cassert>
#include <cmath>
#include <cstring>

namespace tinykv {
FileReader::FileReader(const std::string& file_name) {
	if(::access(file_name.c_str(), F_OK) != 0) {
		LOG(tinykv::LogLevel::ERROR, "path_name:%s don't existed!", file_name.data());
	} else {
		fd_ = open(file_name.data(), O_RDONLY);
	}
}

FileReader::~FileReader() {
	if (fd_ > -1) {
		close(fd_);
		fd_ = -1;
	}
}

DBStatus FileReader::Read(uint64_t offset, size_t n,
                          void* result) const {
	if (!result) {
		return Status::kInvalidObject;
	}
	if (fd_ == -1) {
		LOG(tinykv::LogLevel::ERROR, "Invalid Socket");
		return Status::kInterupt;
	}
	// 原子读 
	// 线程安全
	pread(fd_, result, n, static_cast<off_t>(offset));
	return Status::kSuccess;
}


}