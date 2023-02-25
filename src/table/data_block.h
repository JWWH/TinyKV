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
	explicit DataBlock(const std::string_view& contents);

	DataBlock(const DataBlock&) = delete;
	DataBlock& operator=(const DataBlock&) = delete;
	~DataBlock();
	size_t size() const { return size_; }
	Iterator* NewIterator(std::shared_ptr<Comparator> comparator);

private:
	// 为了实现在block内查找target entry，block定义了一个Iter的嵌套类，继承自虚基类Iterator
	class Iter;
	/**
	 * 这里有一个关键点：重启点。首先，你要了解，一份KV数据作为block内的一个entry（条目），
	 * 考虑节省空间，leveldb对key的存储进行前缀压缩，每个entry中会记录key与前一个key前缀相同的字节（shared_bytes）
	 * 和自己独有的字节（unshared_bytes），读取时，对block进行遍历，每一个key根据前一个key可以构造出来，entry存储形式如下：
	 * +------------+------------+-------------+-----------+-------------+-----------+
	 * | Record i   | key共享长度 ｜ key非共享长度 | value长度 ｜key非共享内容 ｜ value内容 ｜
	 * +------------+------------+-------------+-----------+-------------+-----------+
	 * | Record i+1 | key共享长度 ｜ key非共享长度 | value长度 ｜key非共享内容 ｜ value内容 ｜
	 * +------------+------------+-------------+-----------+-------------+-----------+
	 * 然后，如果完全按照上面所述的处理，对每个key的查找，都要从block的头开始遍历，所以进一步细化粒度，
	 * 对 block 内的前缀压缩分区段进行。 若干个 key 做前缀压缩之后，就重新开始下一轮。
	 * restart_offset_ 就是记录重启点信息在block的偏移量。Block::NumRestarts()返回重启点的个数。
	 * 所以整个block的格式应该是（trailer先不管，放置压缩标志位和CRC校验）：
	 * sstable中的数据以block为单位存储，有利于IO和解析的粒度， 整体如下图：
	 * -+--------+--------+------+--------------------------------------------+--------------------------+---------+
	 * ｜ entry0 | entry1 | .... | restarts(sizeof(uint32_t)*num_of_restarts) | num_of_restarts(uint32_t)| trailer |
	 * -+--------+--------+------+--------------------------------------------+--------------------------+---------+
	 */

	uint32_t NumRestarts() const;

	const char* data_;	// 包含了entrys、重启点数组和写在最后4bytes的重启点个数
	size_t size_;	// 大小
	uint32_t restart_offset_;    // offset in data_ of restart array
	bool owned_;	// block是否存有数据的标志位，析构函数delete data时会判断
};
} // namespace tinykv