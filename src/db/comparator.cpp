#include "../include/tinykv/comparator.h"
#include "../include/tinykv/slice.h"

#include <cmath>

namespace tinykv {


const char* ByteComparator::Name() const {
	static const std::string kByteComparator = "tinykv.ByteComparator";
	return kByteComparator.data();
}

int32_t ByteComparator::Compare(const Slice& a, const Slice& b) const {
	return strcmp(a.data(), b.data());
}

void ByteComparator::FindShortestSeparator(std::string* start, const Slice& limit) const {
	if(limit.empty()) {
		return;
	}
	uint32_t first_diff_pos = 0;
	const auto& start_size = start->size();
	const auto& limit_size = limit.size();
	const auto& min_len = std::min(start_size, limit_size);
	while (first_diff_pos < min_len && (*start)[first_diff_pos]==limit[first_diff_pos]) {
		++first_diff_pos;
	}
	if (first_diff_pos < min_len) {
		char diff_ch = (*start)[first_diff_pos];
		if (diff_ch < 0xff && (diff_ch + 1) < limit[first_diff_pos]) {
			(*start)[first_diff_pos]++;
			// diff_pos+1是因为diff_pos从0开始计算
			start->resize(first_diff_pos + 1);
		}
	}

	
	

}
}