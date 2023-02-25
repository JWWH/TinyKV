#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "../db/options.h"
namespace tinykv{
    /*
     * DataBlock包含三个部分，分别是Records、Restart Points、Restart Point Count
     * 数据排布如下:
     *               +---------------+
     *               |   Record_1    |
     *               +---------------+
     *               |     ...       |
     *               +---------------+
     *               |   Record_N    |
     *               +---------------+
     *               |   Restart_1   |
     *               +---------------+
     *               |     ...       |
     *               +---------------+
     *               |   Restart_K   |
     *               +---------------+
     *               |  Restart_Num  |
     *               +---------------+
     *               | Restart_Offset|
     *               +---------------+
     * 1. [Record_1 ~ Record_N] Records包含N个Record，其中单个Record的schema如下所示：
     *     Record的schema：
     *           +--------------------+----------------------+---------------+----------------------+---------------+
     *           | shared_key_len(4B) | unshared_key_len(4B) | value_len(4B) | unshared_key_content | value_content |
     *           +--------------------+----------------------+---------------+----------------------+---------------+
     *     Record的大小(单位: B) = 4 + 4 + 4 + unshared_key_len + value_len
     *     参数的解释：
     *          shared_key_len: 本条 Record 和对应Restart Point的fullkey的共享头的长度
     *          其他参数意义不言自明
     *
     * 2. [Restart_1 ~ Restart_K] Restart Points
     *     DataBlock中的重启点，保存对应的 Record Group 的OffsetInfo【据此可以分析出第一个key】，用于二分查找
     *
     *     Restart_Point的schema如下：
     *            +----------------+----------------+
     *            | record_num(4B) | OffsetInfo(8B) |
     *            +----------------+----------------+
     *      参数的解释：
     *          record_num: 重启点对应的 Record Group 中的 Record数量
     *          OffsetInfo: 重启点对应的 Record Group 的size和offset
     *
     * 3. [Restart_Num、Restart_Offset]
     *      意义不言自明
     *
     *
     * */
class DataBlockBuilder final {
public:
	DataBlockBuilder(const Options* options);

	void Add(const std::string_view& key, const std::string_view& value);
	void Finish();

	// 获取当前DataBlock的大小
	// 一个DataBlock包括三个部分
	// 1、所有的record，记录在buffer_中
	// 2、所有的重启点，记录在restarts_数组中
	// 3、重启点的数量，记录在restart_pointer_counter_ 中
	const uint64_t CurrentSize() {
		return buffer_.size() + restarts_.size() * sizeof(uint32_t) + sizeof(uint32_t);
	}

	const std::string& Data() { return buffer_; }
	void Reset() {
		restarts_.clear();
		restarts_.emplace_back(0);
		is_finished_ = false;
		buffer_.clear();
		pre_key_ = "";
		restart_pointer_counter_ = 0;
	}

private:
	void AddRestartPointers();
private:
	bool is_finished_ = false;
	const Options* options_;
	std::string buffer_;
	std::vector<uint32_t> restarts_;
	uint32_t restart_pointer_counter_ = 0;
	std::string pre_key_;
};
}