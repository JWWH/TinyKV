#pragma once
#include <stdint.h>
namespace tinykv {
namespace hash_util {

uint32_t SimMurMurHash(const char *data, uint32_t len);

}
}