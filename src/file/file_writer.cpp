#include "file_writer.h"

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

#include "../logger/log.h"

namespace tinykv {
FileWriter::FileWriter(const std::string& path_name, bool append) {
	// rfind 函数，用于从后往前查找字符串，如果查找到，则返回子串第一次一次出现的位置，否则，返回 npos
	std::string::size_type separator_pos = path_name.rfind('/');
	if (separator_pos == std::string::npos) {
		//那说明是当前路径
	} else {
		const auto& dir_path = std::string(path_name.data(), separator_pos);
		if (dir_path != ".") {
			mkdir(dir_path.data(), 0777);
		}
	}
	int32_t mode = O_CREAT | O_WRONLY;
	if (append) {
		mode |= O_APPEND;
	} else {
		mode |= O_TRUNC;
	}
	LOG(WARN,"path=%s", path_name.c_str());
	fd_ = ::open(path_name.data(), mode, 0644);
	assert(::access(path_name.c_str(), F_OK) == 0);	
}

DBStatus FileWriter::Append(const char* data, int32_t len) {
	if(len == 0 || !data) {
		return Status::kSuccess;
	}
	int32_t remain_size =
	std::min<int32_t>(len, kMaxFileBufferSize - current_pos_);
	memcpy(buffer_ + current_pos_, data, remain_size);
	data += remain_size;
	len -= remain_size;
	current_pos_ += remain_size;
	// 如果缓存区足够，我们先不刷盘，尽可能做的批量刷盘
	if (len == 0) {
		return Status::kSuccess;
	}
	// 这里可以保证全部刷盘结束
	int ret = Writen(buffer_, current_pos_);
	current_pos_ = 0;
	if (ret == -1) {
		return Status::kWriteFileFailed;
	}
	if (len < kMaxFileBufferSize) {
		// 如果len小于Buffer缓冲区的最大值
		// 则把剩余的len长度的data拷贝到Buffer缓冲区中
		std::memcpy(buffer_, data, len);
		current_pos_ = len;
		// 因为此时缓冲区还没有满，所以不急着刷盘
		return Status::kSuccess;
	}
	// 如果剩余的数据长度还是大于Buffer缓冲区的大小，就直接刷盘了
	// 如果写入缓冲区肯定会写满，缓冲区还是要刷盘，不值当，所以直接刷盘了
	ret = Writen(data, len);
	if (ret == -1) {
		return Status::kWriteFileFailed;
	}
	return Status::kSuccess;
}

// 剩余的那些需要手动刷盘
// 也就是现在可能等不到数据把缓冲区填满了
// 所以直接把现在缓冲区内的数据刷盘
DBStatus FileWriter::Flush() {
	if (current_pos_ > 0) {
		int ret = Writen(buffer_, current_pos_);
		current_pos_ = 0;
		if (ret == -1) {
			return Status::kWriteFileFailed;
		}
	}
	return Status::kSuccess;
}

ssize_t FileWriter::Writen(const char* data, int len) {
	size_t nleft;		// buffer缓冲区中剩余要写的字节数
	ssize_t nwriten;	// 单次调用write()写入的字节数
	const char* ptr;	// write的缓冲区

	ptr = data;
	nleft = len;
	while(nleft > 0) {
		if ((nwriten = write(fd_, ptr, nleft)) <= 0) {
			if(nwriten < 0 && errno == EINTR) {//在写的过程中遇到了中断，那么write（）会返回-1，同时置errno为EINTR，此时重新调用write
				nwriten = 0;
			} else {
				return (-1);
			}
		}
		nleft -= nwriten; // 还剩余需要写的字节数 = 现在还剩余需要写的字节数 - 这次已经写的字节数
		ptr += nwriten;   // 下次开始写的缓冲区位置 = 缓冲区现在的位置右移已经写了的字节数大小
	}
	return current_pos_;	// 返回已经写了的字节数
}

void FileWriter::Sync() {
	Flush();
	if (fd_ > -1) {
		fsync(fd_);
	}
}

void FileWriter::Close() {
	Flush();
	if (fd_ > -1) {
		close(fd_);
		fd_ = -1;
	}
}

FileWriter::~FileWriter() {
	Sync();	//保证Buffer缓冲区的剩余部分刷盘
}
}