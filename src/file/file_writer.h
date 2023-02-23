#pragma once

#include <stdint.h>
#include <string>
#include <string_view>
#include <vector>

#include "../include/tinykv/status.h"

namespace tinykv {
// 封装顺序写文件的接口
// 用于预写日志wal_file和sst_file的持久化
class FileWriter final {
public:
	FileWriter(const std::string& file_name, bool append = false);
	~FileWriter();

	// 追加长度为len的数据data到文件中
	// 默认追加到buffer缓冲区中，如果设置了立即刷盘，则立刻更新到磁盘中
	DBStatus Append(const char* data, int32_t len);

	// Flush底层是write，也就是将buffer写到C库的缓冲区
	// C库中的flush指的是将C库的缓冲区刷到内核缓冲区中
	DBStatus Flush();

	// Sync底层是fsync，从内核缓冲区刷到磁盘
	void Sync();
	
	void Close();
private:
	ssize_t Writen(const char* data, int len);

	// 设置文件缓冲区，目的是实现批量写，提高磁盘效率
	// 默认以64KB为单位进行批量写
	static const uint32_t kMaxFileBufferSize = 65536;
	char buffer_[kMaxFileBufferSize];	// 缓冲区
	int32_t current_pos_ = 0;	// 写入缓冲区位置

	int fd_ = -1;	// 文件描述符
	std::string file_name_;

};

class FileTool final {
public:
	static uint64_t GetFileSize(const std::string_view& path);
	static bool Exist(std::string_view path );
	static bool Rename(std::string_view from, std::string_view to);
	static bool RemoveFile(const std::string& file_name);
	static bool RemoveDir(const std::string& dirname);
	static bool CreateDir(const std::string& dirname);
};
}