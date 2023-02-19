#pragma once

#include "slice.h"
#include "status.h"

namespace tinykv {
// 代码源自leveldb/include/leveldb/iterator.h
class Iterator {
 
public:
    // 构造函数，初始是没有任何需要清理的对象的
    Iterator();
    // 禁止拷贝构造和拷贝赋值
    Iterator(Iterator&) = delete;
    Iterator& operator=(const Iterator&) = delete;
    // 析构函数
    virtual ~Iterator();
    // 获取迭代器当前是否正常，比如到了结束为止该函数就会返回false
    virtual bool Valid() const = 0;
    // 定位到第一个对象为止
    virtual void SeekToFirst() = 0;
    // 定位到最后一个对象位置
    virtual void SeekToLast() = 0;
    // 定位到Slice指定的对象位置，如果没有对象，那么Valid()返回false.
    virtual void Seek(const Slice& target) = 0;
    // 定位到下一个对象，等同于stl容器迭代器的++
    virtual void Next() = 0;
    // 定位到前一个对象，等同于stl容器迭代器的--
    virtual void Prev() = 0;
    // 获取迭代器当前定位对象的键，前提是Valid()返回true
    virtual Slice key() const = 0;
    // 获取迭代器当前定位对象的值，前提是Valid()返回true
    virtual Slice value() const = 0;
    // 返回当前的状态
    virtual Status status() const = 0;
    // 定义清理函数类型
    using CleanupFunction = void (*)(void* arg1, void* arg2);
    // 注册清理对象
    void RegisterCleanup(CleanupFunction function, void* arg1, void* arg2);
private:

    // 定义清理类型
    struct CleanupNode {
	bool IsEmpty() const { return function == nullptr; }
	void Run() {
		assert(function != nullptr);
		(*function)(arg1, arg2);
	}
        CleanupFunction function; // 清理函数，类型定义下面有定义
        void* arg1;               // 清理函数的参数1
        void* arg2;               // 清理函数的参数2
        CleanupNode* next;            // 单向链表，指向下一个清理对象
    };
    // 所有需要清理的内容
    CleanupNode cleanup_head_;
};	
}