#pragma once
#include <string>
// 用于过滤
namespace tinykv {
class FilterPolicy {
 public:
  FilterPolicy() = default;
  virtual ~FilterPolicy() = default;
  // 当前过滤器的名字
  virtual const char* Name() = 0;
  // 创建过滤器
  virtual void CreateFilter(const std::string* keys, int n) = 0;
  // 判断key是否在过滤器中
  virtual bool MayMatch(const std::string& key, int32_t start_pos,
                        int32_t len) = 0;
  virtual const std::string& Data() = 0;
  // 返回当前过滤器底层对象的空间占用
  virtual uint32_t Size() = 0;
};
}  // namespace tiny