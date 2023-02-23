#pragma once

#include <cstdint>

#include "log_format.h"
#include "../include/tinykv/slice.h"
#include "../include/tinykv/status.h"

namespace tinykv
{
// FileReader是抽象类， 顺序读取文件，定义在env.h中
// 顺序读取文件的抽象封装类
class FileReader;

class Reader {
public:
	// 负责上报错误类
	class Reporter {
	public:
		virtual ~Reporter();

		virtual void Corruption(size_t bytes, const Status& status) = 0;
	};

	/**
	 * 1.file: 要读取的Log文件封装。
	2.reporter: 错误上报类。
	3.checksum: 是否check校验。
	4.initial_offset：开始读取数据偏移位置。
	 */
	Reader(FileReader* file, Reporter* reporter, bool checksum,
		uint64_t initial_offset);

	// 禁止拷贝构造和赋值构造
	Reader(const Reader&) = delete;
	Reader& operator=(const Reader&) = delete;

	~Reader();

	// 1.读取一个Record记录，成功返回true，失败返回false。
  	// 2.读取的数据在*record参数中，传入的*scratch用于临时内部临时存储使用。
	bool ReadRecord(Slice* record, std::string* scratch);
	
	// 返回上一条记录的物理偏移
	// 在第一次调用ReadRecord前调用该函数是无定义的。
	// 因此要在ReadRecord之后调用该函数。
	uint64_t LastRecordOffset();

private:
	FileReader* const file_;
	// 数据损坏报告
	Reporter* const reporter_;
	// 是否进行数据校验
	bool const checksum_;
	// 32kb大小数据存储空间，用于从文件中读取一个Block
	char* const backing_store_;
	// 将从文件读取到的数据封装为一个Slice，用buffer_来表示
	Slice buffer_;
	// 当读取的文件数据大小小于kBlockSize，表示读取到文件尾，将eof_置位true
	bool eof_;   
	
	// 上一条记录的偏移
	uint64_t last_record_offset_;
	// 读取的Buffer尾部的偏移位
	uint64_t end_of_buffer_offset_;
	
	// 初始Offset，从该偏移出查找第一条记录
	uint64_t const initial_offset_;

	//是否重新开始读取Record
  	// 在初始读取位置initial_offset > 0的情况下，resyncing_才为true，
    	// 因为初始位置如果不是从0开始，首次读取到的Record的type是kMiddleType和
    	// kLastType的话，则不是一个完整的record，所以要丢弃重新读取。
  	bool resyncing_;

	/**
	 * 扩展两种类型用于错误表示。
	1.kEof表示到达文件尾。
	2.kBadRecord表示以下三种错误：
	1)CRC校验失败、
	2)读取长度为0、
	3)读取的内存在initial_offset之外，比方说从64位置开始读而Record在31~63之间。 
	* 
	 */
	// 这些特殊值是记录类型的扩展
	enum {
	kEof = kMaxRecordType + 1,
	// Returned whenever we find an invalid physical record.
	// Currently there are three situations in which this happens:
	// * The record has an invalid CRC (ReadPhysicalRecord reports a drop)
	// * The record is a 0-length record (No drop is reported)
	// * The record is below constructor's initial_offset (No drop is reported)
	kBadRecord = kMaxRecordType + 2
	};
	
	// 跳到起始位置initial_offset处开始读取
	bool SkipToInitialBlock();
	
	// 读取一条记录中的数据字段，存储在result中，返回记录类型或者上面的特殊值之一
	unsigned int ReadPhysicalRecord(Slice* result);
	
	// 上报错误和丢弃
	void ReportCorruption(size_t bytes, const char* reason);
	void ReportDrop(size_t bytes, const Status& reason);
};
} // namespace tinykvclass FileReader;
