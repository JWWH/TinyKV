#pragma once

#include "../include/tinykv/iterator.h"
#include "../db/options.h"

namespace tinykv {
// 设置二级迭代器时传入的回调函数
// typedef Iterator* (*BlockFunction)(void*, const ReadOptions&, const std::string&);

Iterator* NewTwoLevelIterator(
    Iterator* index_iter,
    Iterator* (*block_function)(void* arg, const ReadOptions& options,
                                const std::string& index_value),
    void* arg, const ReadOptions& options);
}