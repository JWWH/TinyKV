#include "util.h"
#include <stdint.h>

namespace tinykv {
namespace util {



uint32_t DecodeFixed32(const char* ptr)
{
	if(!ptr)
	{
		return 0;
	}
	return ((static_cast<uint32_t>(static_cast<unsigned char>(ptr[0]))) |
		(static_cast<uint32_t>(static_cast<unsigned char>(ptr[1])) << 8) |
		(static_cast<uint32_t>(static_cast<unsigned char>(ptr[2])) << 16) |
		(static_cast<uint32_t>(static_cast<unsigned char>(ptr[3])) << 24));
}
	
}
}