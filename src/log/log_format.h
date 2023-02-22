#pragma once

namespace tinykv {
// 枚举类型
// 写入log的记录可以被拆分为多个段，每个段都有一个header，
// header中有一个type字段用来表示Record的类型
enum RecordType {
	kZeroType = 0,	// Zero is reserved for preallocated files
	kFullType = 1,	// 这是一个完整的user record
	kFirstType = 2,	// 这是user record的第一个record
	kMiddleType = 3, // 这是user record中间的record，如果写入的数据比较大，kMiddleType的record可能有多个
	kLastType = 4	// 这是user record的最后一个record
};

static const int kMaxRecordType = kLastType;

static const int kBlockSize = 32768;	// 一个Block的大小是32KB，也就是32768个字节

// 一个record由一个固定7字节的header(checksum: uint32 + length: uint16 + type: uint8)和实际数据(data:uint8[length])组成
static const int kHeaderSize = 4 + 2 + 1;
} // namespace leveldb