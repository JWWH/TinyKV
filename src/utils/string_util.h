#ifndef UTILS_STRING_UTIL_H_
#define UTILS_STRING_UTIL_H_
#include <string>
#include <vector>
namespace tinykv {
namespace string_util {
void Split(const std::string& input, const char delimiter,
           std::vector<std::string>& output);
}  // namespace string_util

} 

#endif