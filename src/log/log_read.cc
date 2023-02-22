#include "log_read.h"
#include "../include/tinykv/env.h"
#include "../utils/codec.h"
#include "../utils/crc32c.h"

namespace tinykv {
Reader::Reporter::~Reporter() = default;

/**
 *  
 */
Reader::Reader(SequentialFile* file, Reporter* reporter, bool checksum,
               uint64_t initial_offset)
    : file_(file),
      reporter_(reporter),
      checksum_(checksum),
      backing_store_(new char[kBlockSize]),
      buffer_(),
      eof_(false),
      last_record_offset_(0),
      end_of_buffer_offset_(0),
      initial_offset_(initial_offset) {
}
 
Reader::~Reader() {
  delete[] backing_store_;
}
 
bool Reader::SkipToInitialBlock() {
  // 构造时传入的initial_offset大于等于kBlockSize，则block_start_location
  // 是第（initial_offset_ / kBlockSize）+1个Block起始位置的偏移。
  // 当initial_offset比kBlockSize小时，则block_start_location是第1个Block
  // 起始位置的偏移
  size_t offset_in_block = initial_offset_ % kBlockSize;
  uint64_t block_start_location = initial_offset_ - offset_in_block;
 
  // offset_in_block > kBlockSize - 6，说明已经到了一个Block的尾部，
  // 尾部填充的是6个空字符。此时只能定位到下一个Block的开头。
  if (offset_in_block > kBlockSize - 6) {
    offset_in_block = 0;
    block_start_location += kBlockSize;
  }
 
  end_of_buffer_offset_ = block_start_location;
 
  // 如果block_start_location大于0，则文件中应该跳过block_start_location
  // 个字节，到达目标Block的开头。否则将数据损坏信息打印到LOG文件。
  if (block_start_location > 0) {
    Status skip_status = file_->Skip(block_start_location);
    if (!skip_status.ok()) {
      ReportDrop(block_start_location, skip_status);
      return false;
    }
  }
 
  return true;
}
 
bool Reader::ReadRecord(Slice* record, std::string* scratch) {
  if (last_record_offset_ < initial_offset_) {
    if (!SkipToInitialBlock()) {
      return false;
    }
  }
 
  scratch->clear();
  record->clear();
  // 是否是分段的记录
  bool in_fragmented_record = false;
  // 当前读取的记录的逻辑偏移
  uint64_t prospective_record_offset = 0;
 
  Slice fragment;
  while (true) {
	// buffer_会在ReadPhysicalRecord中自偏移，实际上buffer_中存储的是当前Block
	// 还未解析的记录，而end_of_buffer_offset_是当前Block的结束位置的偏移
    uint64_t physical_record_offset = end_of_buffer_offset_ - buffer_.size();
    const unsigned int record_type = ReadPhysicalRecord(&fragment);
    switch (record_type) {
      case kFullType:
        if (in_fragmented_record) {
          // Handle bug in earlier versions of log::Writer where
          // it could emit an empty kFirstType record at the tail end
          // of a block followed by a kFullType or kFirstType record
          // at the beginning of the next block.
          if (scratch->empty()) {
            in_fragmented_record = false;
          } else {
            ReportCorruption(scratch->size(), "partial record without end(1)");
          }
        }
		// 当为kFullType时，物理记录和逻辑记录1:1的关系，所以offset也是一样的
        prospective_record_offset = physical_record_offset;
        scratch->clear();
        *record = fragment;
        last_record_offset_ = prospective_record_offset;
        return true;
 
      case kFirstType:
        if (in_fragmented_record) {
          // Handle bug in earlier versions of log::Writer where
          // it could emit an empty kFirstType record at the tail end
          // of a block followed by a kFullType or kFirstType record
          // at the beginning of the next block.
          if (scratch->empty()) {
            in_fragmented_record = false;
          } else {
            ReportCorruption(scratch->size(), "partial record without end(2)");
          }
        }
		// 因为是第一分段，所以物理记录的offset，也是逻辑记录的offset 
		// 注意第一个分段用的是assign添加到scratch
        prospective_record_offset = physical_record_offset;
        scratch->assign(fragment.data(), fragment.size());
        in_fragmented_record = true;
        break;
 
      case kMiddleType:
        if (!in_fragmented_record) {
          ReportCorruption(fragment.size(),
                           "missing start of fragmented record(1)");
        } else {
          scratch->append(fragment.data(), fragment.size());
        }
        break;
 
      case kLastType:
        if (!in_fragmented_record) {
          ReportCorruption(fragment.size(),
                           "missing start of fragmented record(2)");
        } else {
          scratch->append(fragment.data(), fragment.size());
          *record = Slice(*scratch);
		  // 逻辑记录结束，更新最近一条逻辑记录的offset
          last_record_offset_ = prospective_record_offset;
          return true;
        }
        break;
 
      case kEof:
        if (in_fragmented_record) {
          ReportCorruption(scratch->size(), "partial record without end(3)");
          scratch->clear();
        }
        return false;
 
      case kBadRecord:
        if (in_fragmented_record) {
          ReportCorruption(scratch->size(), "error in middle of record");
          in_fragmented_record = false;
          scratch->clear();
        }
        break;
 
      default: {
        char buf[40];
        snprintf(buf, sizeof(buf), "unknown record type %u", record_type);
        ReportCorruption(
            (fragment.size() + (in_fragmented_record ? scratch->size() : 0)),
            buf);
        in_fragmented_record = false;
        scratch->clear();
        break;
      }
    }
  }
  return false;
}
 
uint64_t Reader::LastRecordOffset() {
  return last_record_offset_;
}
 
void Reader::ReportCorruption(size_t bytes, const char* reason) {
  ReportDrop(bytes, Status::Corruption(reason));
}
 
void Reader::ReportDrop(size_t bytes, const Status& reason) {
  if (reporter_ != NULL &&
      end_of_buffer_offset_ - buffer_.size() - bytes >= initial_offset_) {
    reporter_->Corruption(bytes, reason);
  }
}
 
unsigned int Reader::ReadPhysicalRecord(Slice* result) {
  while (true) {
	// 两种情况下该条件成立
	// 1.出现在第一次read，因为buffer_在reader的构造函数里是初始化空
	// 2.当前buffer_的内容为Block尾部的6个空字符，这时实际上当前Block
	//   以及解析完了，准备解析下一个Block
    if (buffer_.size() < kHeaderSize) {
      if (!eof_) {
        // 清空buffer_，存储下一个Block
        buffer_.clear();
		// 从文件中每次读取一个Block，Read内部会做偏移，保证按顺序读取
        Status status = file_->Read(kBlockSize, &buffer_, backing_store_);
		// 当前Block结束位置的偏移
        end_of_buffer_offset_ += buffer_.size();
		// 读取失败，打印LOG信息，并将eof_设置为true，终止log文件的解析
        if (!status.ok()) {
          buffer_.clear();
          ReportDrop(kBlockSize, status);
          eof_ = true;
          return kEof;
		// 如果读到的数据小于kBlockSize，也说明到了文件结尾，eof_设为true
        } else if (buffer_.size() < kBlockSize) {
          eof_ = true;
        }
		// 跳过后面的解析，因为buffer_.size() < kHeaderSize时，buffer是无法解析的
        continue;
      } else if (buffer_.size() == 0) {
        // 如果eof_为false，但是buffer_.size，说明遇到了Bad Record，也应该终止log文件的解析
        return kEof;
      } else {
		// 如果最后一个Block的大小刚好为kBlockSize，且结尾为6个空字符
        size_t drop_size = buffer_.size();
        buffer_.clear();
        ReportCorruption(drop_size, "truncated record at end of file");
        return kEof;
      }
    }
 
    // Parse the header
    const char* header = buffer_.data();
    const uint32_t a = static_cast<uint32_t>(header[4]) & 0xff;
    const uint32_t b = static_cast<uint32_t>(header[5]) & 0xff;
    const unsigned int type = header[6];
    const uint32_t length = a | (b << 8);
	// 一个Block里放不下一条记录，显示是Bad Record
    if (kHeaderSize + length > buffer_.size()) {
      size_t drop_size = buffer_.size();
      buffer_.clear();
      ReportCorruption(drop_size, "bad record length");
      return kBadRecord;
    }
	// 长度为0的记录，显然也是Bad Record
    if (type == kZeroType && length == 0) {
      // Skip zero length record without reporting any drops since
      // such records are produced by the mmap based writing code in
      // env_posix.cc that preallocates file regions.
      buffer_.clear();
      return kBadRecord;
    }
 
    // 如果校验失败，也是Bad Record
    if (checksum_) {
      uint32_t expected_crc = crc32c::Unmask(DecodeFixed32(header));
      uint32_t actual_crc = crc32c::Value(header + 6, 1 + length);
      if (actual_crc != expected_crc) {
        // Drop the rest of the buffer since "length" itself may have
        // been corrupted and if we trust it, we could find some
        // fragment of a real log record that just happens to look
        // like a valid log record.
        size_t drop_size = buffer_.size();
        buffer_.clear();
        ReportCorruption(drop_size, "checksum mismatch");
        return kBadRecord;
      }
    }
	// buffer_的自偏移
    buffer_.remove_prefix(kHeaderSize + length);
 
    // 这样的记录也是Bad Record，不解释了，太明显
    if (end_of_buffer_offset_ - buffer_.size() - kHeaderSize - length <
        initial_offset_) {
      result->clear();
      return kBadRecord;
    }
	// 取出记录中的数据字段
    *result = Slice(header + kHeaderSize, length);
    return type;
  }
}
}