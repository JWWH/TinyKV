#pragma once

#include "../include/tinykv/iterator.h"
#include "../include/tinykv/slice.h"
#include "two_level_iterator.h"

namespace tinykv {

// 设置二级迭代器时传入的回调函数
typedef Iterator* (*BlockFunction)(void*, const ReadOptions&, const std::string&);

class TwoLevelIterator : public Iterator {
public:
	//构造。对于SSTable来说：
	//1、index_iter是指向index block的迭代器；
	//2、block_function是Table::BlockReader,即读取一个block;
	//3、arg是指向一个SSTable;
	  TwoLevelIterator(Iterator* index_iter, BlockFunction block_function,
                   void* arg, const ReadOptions& options);
	~TwoLevelIterator() override;

	void Seek(const Slice& target) override;
	void SeekToFirst() override;
	void SeekToLast() override;
	void Next() override;
	void Prev() override;

	bool Valid() const override { return data_iter_->Valid(); }
	Slice key() const override {
		assert(Valid());
		return data_iter_->key();
	}
	Slice value() const override {
		assert(Valid());
		return data_iter_->value();
 	}
	DBStatus status() const override{

	}

private:
	void SaveError(const DBStatus& s) {
		if (status_ == Status::kSuccess && s != Status::kSuccess) status_ = s;
	}
	void SkipEmptyDataBlocksForward();
	void SkipEmptyDataBlocksBackward();
	void SetDataIterator(Iterator* data_iter);
	void InitDataBlock();
	BlockFunction block_function_;
	void* arg_;
	DBStatus status_;
	const ReadOptions options_;
	//一级迭代器，对于SSTable来说就是指向index block
	Iterator* index_iter_; 
	//二级迭代器，对于SSTable来说就是指向DataBlock
	Iterator* data_iter_;  // May be nullptr
	std::string data_block_handle_;
};

TwoLevelIterator::TwoLevelIterator(Iterator* index_iter,
                                   BlockFunction block_function, void* arg,
                                   const ReadOptions& options)
    : block_function_(block_function),
      arg_(arg),
      options_(options),
      index_iter_(index_iter),
      data_iter_(nullptr) {}

TwoLevelIterator::~TwoLevelIterator() = default;

//1、seek到target对应的一级迭代器位置;
//2、初始化二级迭代器;
//3、跳过当前空的DataBlock。
void TwoLevelIterator::Seek(const Slice& target) {
  index_iter_->Seek(target);
  InitDataBlock();
  if (data_iter_ != nullptr) data_iter_->Seek(target);
  SkipEmptyDataBlocksForward();
}

void TwoLevelIterator::SeekToFirst() {
	index_iter_->SeekToFirst();
	InitDataBlock();
	if (data_iter_ != nullptr) data_iter_->SeekToFirst();
	SkipEmptyDataBlocksForward();
}

void TwoLevelIterator::SeekToLast() {
	index_iter_->SeekToLast();
	InitDataBlock();
	if (data_iter_ != nullptr) data_iter_->SeekToLast();
	SkipEmptyDataBlocksBackward();
}

//二级迭代器的下一个元素，
//对SSTable来说就是DataBlock中的下一个元素。
//需要检查跳过空的DataBlck。
void TwoLevelIterator::Next() {
	assert(Valid());
	data_iter_->Next();
	SkipEmptyDataBlocksForward();
}

//二级迭代器的前一个元素，
//对SSTable来说就是DataBlock中的前一个元素。
//需要检查跳过空的DataBlck。
void TwoLevelIterator::Prev() {
	assert(Valid());
	data_iter_->Prev();
	SkipEmptyDataBlocksBackward();
}

//针对二级迭代器。
//如果当前二级迭代器指向为空或者非法;
//那就向后跳到下一个非空的DataBlock。
void TwoLevelIterator::SkipEmptyDataBlocksForward() {
  while (data_iter_ == nullptr || !data_iter_->Valid()) {
    // Move to next block
    if (!index_iter_->Valid()) {
      SetDataIterator(nullptr);
      return;
    }
    index_iter_->Next();
    InitDataBlock();
    if (data_iter_ != nullptr) data_iter_->SeekToFirst();
  }
}

//针对二级迭代器。
//如果当前二级迭代器指向为空或者非法;
//那就向前跳到下一个非空的DataBlock。
void TwoLevelIterator::SkipEmptyDataBlocksBackward() {
  while (data_iter_ == nullptr || !data_iter_->Valid()) {
    // Move to next block
    if (!index_iter_->Valid()) {
      SetDataIterator(nullptr);
      return;
    }
    index_iter_->Prev();
    InitDataBlock();
    if (data_iter_ != nullptr) data_iter_->SeekToLast();
  }
}

//设置二级迭代器
void TwoLevelIterator::SetDataIterator(Iterator* data_iter) {
  if (data_iter_ != nullptr) SaveError(data_iter_->status());
  data_iter_ = data_iter;
}

//初始化二级迭代器指向。
//对SSTable来说就是获取DataBlock的迭代器赋值给二级迭代器。
void TwoLevelIterator::InitDataBlock() {
  if (!index_iter_->Valid()) {
    SetDataIterator(nullptr);
  } else {
    Slice handle = index_iter_->value();
    if (data_iter_ != nullptr &&
        handle.compare(data_block_handle_) == 0) {
      // data_iter_ is already constructed with this iterator, so
      // no need to change anything
    } else {
      Iterator* iter = (*block_function_)(arg_, options_, handle.ToString());
      data_block_handle_.assign(handle.data(), handle.size());
      SetDataIterator(iter);
    }
  }
}

Iterator* NewTwoLevelIterator(Iterator* index_iter,
                              BlockFunction block_function, void* arg,
                              const ReadOptions& options) {
  return new TwoLevelIterator(index_iter, block_function, arg, options);
}

}