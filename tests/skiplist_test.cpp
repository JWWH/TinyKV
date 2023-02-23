#include "db/skiplist.h"

#include <gtest/gtest.h>

#include <iostream>
#include <string>
#include <vector>

#include "db/comparator.h"
#include "logger/log.h"
#include "memory/area.h"

using namespace std;
using namespace tinykv;
static  vector<string> kTestKeys = {"tinykv", "tinykv1", "tinykv2", "tinykv3", "tinykv4", "tinykv5"};
TEST(skiplistTest, Insert) {
  tinykv::LogConfig log_config;
  log_config.log_type = tinykv::LogType::CONSOLE;
  log_config.rotate_size = 100;
  tinykv::Log::GetInstance()->InitLog(log_config);
  using Table = SkipList<const char*, ByteComparator, SimpleVectorAlloc>;
  ByteComparator byte_comparator;
  Table tb(byte_comparator);
  for (int i = 0; i < 100; i++) {
    kTestKeys.emplace_back(std::to_string(i));
  }
  for (auto item : kTestKeys) {
    tb.Insert(item.c_str());
  }
  for (auto& item : kTestKeys) {
    cout << "[ key:" << item << ", has_existed:" << tb.Contains(item.c_str())
         << " ]" << endl;
  }
}