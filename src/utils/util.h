#pragma once

#include <stdint.h>

namespace tinykv{
	namespace util {

		uint32_t DecodeFixed32(const char* ptr);
	}
}