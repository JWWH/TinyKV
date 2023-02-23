#pragma once

#include <stdint.h>
#include <string>

namespace tinykv{
	namespace util {
		//单位是ms
		uint64_t GetCurrentTime();
		void GetCurrentTimeString(std::string&output);
		int64_t GetCurrentTid();
		int64_t GetCurrentPid();
		bool CheckLittleEndian();
		uint32_t DecodeFixed32(const char* ptr);
	}
}