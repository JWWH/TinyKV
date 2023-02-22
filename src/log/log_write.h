#pragma once

#include "log_format.h"
#include "../include/tinykv/slice.h"
#include "../include/tinykv/status.h"

namespace tinykv {
// WritableFile是对log文件的抽象，是顺序写入文件	
class WritableFile;

class Writer {
public:
	explicit Writer(WritableFile* dest);

	Writer(WritableFile* dest, uint64_t dest_length);

	Writer(const Writer&) = delete;
	Writer& operator=(const Writer&) = delete;

	~Writer();

	Status AddRecord(const Slice& slice);

private:
	Status EmitPhysicalRecord(RecordType type, const char* ptr, size_t length);
	// WritableFile这是一个文件操作的抽象类
	// 是对不同操作系统的文件操作的抽象，不同操作系统实现也不一样，提供Write、Sync等接口。
	WritableFile* dest_;
	// 写入是以Block为单位的，这个表示写入位置在当前block的偏移量，
	// 比如这个block写了100个字节了，那么block_offset_就是100
	int block_offset_;
	// 每个RecordType有一个预先计算好的crc校验值，放在数组中
	// 这个用来辅助计算数据的crc
	uint32_t type_crc_[kMaxRecordType + 1];

};
}