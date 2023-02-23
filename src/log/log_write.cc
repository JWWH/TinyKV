#include "log_write.h"
#include "../file/file_writer.h"
#include "../utils/crc32c.h"
#include "../utils/codec.h"

namespace tinykv {
static void InitTypeCrc(uint32_t* type_crc) {
	for (int i=0; i <= kMaxRecordType; i++) {
		char t = static_cast<char>(i);
		type_crc[i] = crc32c::Value(&t, 1);
	}
}

Writer::Writer(FileWriter* dest)
	: dest_(dest)
	, block_offset_(0)
{
	InitTypeCrc(type_crc_);
} 

Writer::~Writer() = default;

/**
 * AddRecord是提供给外部调用的的接口，参数只有一个Slice对象，一个Slice就是一条记录，AddRecord把Slice的数据写到文件中。
 * AddRecord的主要功能是根据block的写入情况，把要写入的记录进行分片，分为多个fragment。为什么不直接把记录按照header|record的格式进行写入呢？虽然这样实现逻辑更简单，但是意味着每次只能读一条记录，如果每条记录的size比较小，那么一条记录就需要读一次磁盘，虽然page cache能一定程度提高读效率。但是每4KB还是会读一次磁盘，读效率相对于每次读32KB的block就低很多了。

分片是通过do…while循环来实现的。首先计算当前block剩余空间leftover。如果剩余空间小于fragment的header size，说明已经无法写入一个新的fragment了，用0填充空间。把block_offset_设置为0，即开启一个新的block。

每循环一次，就会产生一个分片，left用于保存当前记录还没有写入的size，如果left大于block的可用空间avial(leftover-kHeaderSize)，就先写入avial大小的fragment，并把left减去avail。继续循环，直到剩余可用空间足够写入left。

写入由EmitPhysicalRecord完成，EmitPhysicalRecord的第一个参数RecordType表示当前fragment在当前记录的相对位置。
 */
Status Writer::AddRecord(const Slice& slice) {
	const char* ptr = slice.data();
	size_t left = slice.size();

	// 有必要的情况下， 需要record进行分片写入
	// 如果slice数据为空，仍然会写一次，只是长度为0， 读取的时候会对这种情况进行处理
	/** 
	 * 写文件是以一个Block(32KB)为单元写入的，而写入到Block这是一个个Record，
	每个Record的头长度为7Byte。假设这个Block剩余可写的长度为L，
	要写入的数据为N，则分以下情况进行处理：
	1、L >= N+7，说明Block空间足以容纳下一个Record和7Byte的头，
	则这个数据被定义为一个Type为kFullType的Record。
	2、N + 7 > L >= 7，即当前Block空间大于等于7Byte，但不足以保存全部内容，
	则在当前页生存一个Type为kFirstType的Record，Payload（Block剩余空间）保存
	数据前面L-7字节的内容（可以为0，那就直说一个头），如果数据剩余的长度小于32KB，
	则在下一个页中生成一个Type为kLastType的Record，否则在下一个Block中生成一个
	Type为kMiddleType的Record，依次类推，直至数据被完全保存下来。
	3、L < 7，当前Block的剩余长度小于7Byte，则填充0。      
	以上流程就是整个写流程了。
	 */
	Status s;
	bool begin = true;
	do {
		// 每个block还剩多少空间
		const int leftover = kBlockSize - block_offset_;
		assert(leftover >= 0);
		if (leftover < kHeaderSize) {
			if(leftover > 0) {
				static_assert(kHeaderSize == 7, "");
				dest_->Append(Slice("\x00\x00\x00\x00\x00\x00", leftover));
			}
			block_offset_ = 0;
		}

		assert(kBlockSize - block_offset_ - kHeaderSize >= 0);

		// 当前block还能放多少字节的数据
		const size_t avail = kBlockSize - block_offset_ - kHeaderSize;
		// 如果 left<avail 说明当前块可以放下
		// 否则放不下
		const size_t fragment_length = (left < avail) ? left : avail;

		RecordType type;
		// 如果left==fragment_length 当前块就把这个记录放完了
    		const bool end = (left == fragment_length);
		if (begin && end) {
			type = kFullType;
		} else if (begin) {
			type = kFirstType;
		} else if (end) {
			type = kLastType;
		} else {
			type = kMiddleType;
		}

		s = EmitPhysicalRecord(type, ptr, fragment_length);
		ptr += fragment_length;
		left -= fragment_length;
		begin = false;
	} while(s.ok() && left>0);
	return s;
}

/**
 * @brief 
 * 1、格式化打包头；
  2、CRC校验计算；
  3、先写头、在写Payload，写成功之后flush下；
  4、将block_offset_位置重新计算下。
 */
Status Writer::EmitPhysicalRecord(RecordType t, const char* ptr, size_t length) {
assert(length <= 0xffff);  // Must fit in two bytes
	assert(block_offset_ + kHeaderSize + length <= kBlockSize);

	// Format the header
	char buf[kHeaderSize];
	// 长度的低位放到数组的第五个字节
	// 长度的高位放到数组的第六个字节
	buf[4] = static_cast<char>(length & 0xff);
	buf[5] = static_cast<char>(length >> 8);
	// 类型放到数组的第七个字节
	buf[6] = static_cast<char>(t);

	// Compute the crc of the record type and the payload.
	uint32_t crc = crc32c::Extend(type_crc_[t], ptr, length);
	crc = crc32c::Mask(crc);  // Adjust for storage
	// 1.添加校验码到header中（包括类型字段和数据字段的校验）
	EncodeFixed32(buf, crc);

	// 2.添加header
	// Write the header and the payload
	Status s = dest_->Append(Slice(buf, kHeaderSize));
	if (s.ok()) {
		// 3.添加数据
		s = dest_->Append(Slice(ptr, length));
		if (s.ok()) {
			// 写入到磁盘
			/** 
			 * 通过Flush方法将用户态buffer中写入的内容刷入内核态buffer后便会返回，后续写入通过操作系统实现。
			 * 如果掉电时，操作系统还没有将数据写入到稳定存储，数据仍会丢失。为了确保内核缓冲区中的数据会被写入到稳定存储，
			 * 需要通过系统调用实现，在POSIX系统下常用的系统调用有fsync、fdatasync、msync等。
			 */
			s = dest_->Flush();
		}
	}
	// 偏移的自增
	block_offset_ += kHeaderSize + length;
	return s;
}

}