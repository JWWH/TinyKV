#pragma once

#include <cstdint>
#include <atomic>

// 参考C++标准库内存池的设计
namespace tinykv{
/**
 * @brief 
 * 参考C++标准库中的设计：SGI第二级配置器的做法是，如果区块够大，超过128bytes时， 
 * 就移交第一级配置器处理，也就是直接调用malloc分配内存。当区块小于128 bytes时，则以内存池(memory pool)管理，
 * 此法又称为次层配置(sub-allocation): 每次配置一大块内存，并维护其对应的自由链表(free-list)
 */

class SimpleFreeListAlloc final {
public:
	SimpleFreeListAlloc() = default;
	~SimpleFreeListAlloc();
	void* Allocate(int32_t n);	// 分配内存
	void Deallocate(void* p, int32_t n);	// 释放内存
	void* Reallocate(void* p, int32_t old_size, int32_t new_size);	// 扩容
	uint32_t MemoryUsage() const {
		return memory_usage_.load(std::memory_order_relaxed);
	}
private:
	/*根据FreeList设计原理，分析如下：
	1.当使用next时，表示当前内存并未使用
	2.当使用data时，表示该内存已经不在freelist中
	因此两者不会同时使用，借助union可以节省8个字节(64bit下指针占8个字节)
	*/
	union FreeList {	// free-lists的节点构造
		union FreeList* next;
		char data[1]; /* The client sees this. */
	};
private:
	// int32_t Aligin(int32_t bytes);	// 字节对齐
	int32_t Roundup(int32_t bytes); // 向上取整
	int32_t FreeListIndex(int32_t bytes);	// 根据对象的大小决定使用第n号free-list
	void* Refill(int32_t n);	// 返回一个大小为n的对象，并可能加入大小为n的其他对象到free-list
	// 配置一大块空间，可以容纳obj个大小为size的对象
	// 如果配置obj个对象有所不便，obj可能会降低
	char* ChunkAlloc(int32_t size, int32_t& obj);

private:
	static const uint32_t kAlignBytes = 8;	// 按照8来对齐 小对象的上调边界
	static const uint32_t kSmallObjectBytes = 4096;	// 小对象最大的字节数据
	static const uint32_t kFreeListMaxNum = kSmallObjectBytes / kAlignBytes; //free-lists 个数

	char* free_list_start_pos_ = nullptr;	// 当前可用内存起点
	char* free_list_end_pos_ = nullptr;	// 当前可用内存终点
	int32_t heap_size_ = 0;	// 总的内存大小，可以理解为bias
	FreeList* freelist_[kFreeListMaxNum] = {nullptr};
};	std::atomic<uint32_t> memory_usage_;	// 用户获取当前内存分配量

}