#include "alloc.h"

#include <stdlib.h>
#include <iostream>
#include <cassert>

namespace tinykv{
SimpleFreeListAlloc::~SimpleFreeListAlloc() {
	//释放二级内存，最后统一释放
	if(!free_list_start_pos_) {
		FreeList* p = (FreeList*)free_list_start_pos_;
		while(p) {
			FreeList* next = p->next;
			free(p);
			p = next;
		}
	}
}

int32_t SimpleFreeListAlloc::FreeListIndex(int32_t bytes) {
  // first fit策略
  return (bytes + kAlignBytes - 1) / kAlignBytes - 1;
}
int32_t SimpleFreeListAlloc::Roundup(int32_t bytes) {
  //向上取整
  return (bytes + kAlignBytes - 1) & ~(kAlignBytes - 1);
}
/**
 * @brief 
 * allocate()中，当发现free list没有可用区块时就调用refill(),准备为free list重新填充空间，
 * 新的空间将取自内存池(经由chunkalloc()完成)
 */
// 返回一个大小为n的对象，并且有时候会为适当的free list增加节点
void* SimpleFreeListAlloc::Refill (int32_t n) {
	// 默认一次先分配10个block，分配太多，导致浪费，太少可能不够用
	static const int32_t kInitBlockCount = 10;	// 一次先分配10个， STL默认是20个
	int32_t real_block_count = kInitBlockCount;	// 初始化，先按理想值来分配
	char* chunk = ChunkAlloc(n, real_block_count);
	FreeList* volatile * my_free_list;
	FreeList* current_obj;
	FreeList* next_obj;
	FreeList* result;
	// 如果只获得一个区块，这个区块就分配给调用者使用，free list无新节点
	if (real_block_count == 1) {
		return chunk;
	}
	// 否则准备调整free list，纳入新节点
	my_free_list = freelist_ + FreeListIndex(n);
	// 以下在chunk空间内建立free list
	result = (FreeList*) chunk;	// 这一块准备返回给客户端
	// 以下引导free list指向新配置的空间（取自内存池）
	*my_free_list = next_obj = (FreeList*)(chunk + n);
	// 以下将free list的各节点串接起来
	for (int i=1; ; i++) {	// 从1开始，因为第0个将返回给客户端
		current_obj = next_obj;
		next_obj = (FreeList*)((char*)next_obj + n);
		if(i == real_block_count-1) {
			current_obj->next = 0;
			break;
		}
		else {
			current_obj->next = next_obj;
		}
	}
	return result;
}

// 从内存池中取空间给free list使用，是ChunkAlloc的工作
char* SimpleFreeListAlloc::ChunkAlloc(int32_t size, int32_t& nobjs) {
	char* result;
	uint32_t total_bytes = size * nobjs;	// 总的字节大小
	uint32_t bytes_left = free_list_end_pos_ - free_list_start_pos_; // 内存池剩余空间
	if(bytes_left >= total_bytes) {
		// 内存池的剩余空间完全满足需求量，那么此时直接使用剩余的空间
		result = free_list_start_pos_;
		// 更新剩余空间的起始位置
		free_list_start_pos_ += total_bytes;
		memory_usage_.fetch_add(total_bytes, std::memory_order_relaxed);
		return result;
	} else if (bytes_left >= size) {
		// 内存池剩余空间不能完全满足需求量，但足够供应一个(含)以上的区块
		nobjs = bytes_left / size;
		total_bytes = nobjs * size;
		result = free_list_start_pos_;
		free_list_start_pos_ += total_bytes;
		memory_usage_.fetch_add(total_bytes, std::memory_order_relaxed);
		return result; 
	} else {
		// 内存池剩余空间一个都没法分配时
		// 这里又分配了2倍
		int32_t bytes_to_get = 2 * total_bytes + Roundup(heap_size_>>4);
		// 以下试着让内存池中的残余零头还有利用价值
		if(bytes_left > 0) {
			// 内存池中还有剩余，先分配给适当的free list， 否则这部分会浪费掉
			// 首先寻找适当的free list
			FreeList* volatile * my_free_list = freelist_ + FreeListIndex(bytes_left);
			//调整free list, 将内存池剩余空间编入
			((FreeList*)free_list_start_pos_)->next = *my_free_list; // 头插法
			*my_free_list = ((FreeList*)free_list_start_pos_);
		}

		// 分配新的空间，用来补充内存池
		// 配置堆空间
		free_list_start_pos_ = (char*)malloc(bytes_to_get);
		if(free_list_start_pos_ == 0) {
			// heap空间不足，malloc分配失败
			FreeList* volatile * my_free_list, *p;
			// 以下搜寻适当的free list
			// 也就是找到尚有未用区块，且区块够大的free list
			for(int32_t i = size; i <= kSmallObjectBytes; i += kAlignBytes) {
				my_free_list = freelist_ + Roundup(i);
				p = *my_free_list;
				if(p) {	// freelist中尚有未使用的区块
					// 调整free list
					*my_free_list = p->next;
					free_list_start_pos_ = (char*)p;
					free_list_end_pos_ = free_list_start_pos_ + i;
					// 递归调用自己， 为了修正nobjs
					return ChunkAlloc(size, nobjs);
				}
			}
			// 如果未找到，此时我们再重新尝试分配一次，如果分配失败，此时将终止程序
			free_list_end_pos_ = nullptr;
			free_list_start_pos_ = (char*)malloc(bytes_to_get);
			if(!free_list_start_pos_) {
				exit(1);
			}
		}
		// malloc分配堆空间成功
		heap_size_ += bytes_to_get;
		free_list_end_pos_ = free_list_start_pos_ + bytes_to_get;
		// TODO: 此处不确定是否需要增加memory_usage_
		//memory_usage_.fetch_add(bytes_to_get, std::memory_order_relaxed);
		return ChunkAlloc(size, nobjs);
	}

}

void* SimpleFreeListAlloc::Allocate(int32_t n) {
	assert(n > 0);
	FreeList* volatile * my_free_list;
	memory_usage_.fetch_add(n, std::memory_order_relaxed);
	//如果超过4kb，此时直接使用glibc自带的malloc，因为我们假设大多数时候都是小对象
  	//针对大对象，可能会存在大key，在上层我们就应该尽可能的规避
	if(n > kSmallObjectBytes) {
		return (char*)malloc(n);
	}
	// 根据对象的大小， 定位位于哪个slot
	my_free_list = freelist_ + FreeListIndex(n);
	FreeList* result = *my_free_list;
	if(result == 0) {
		// 没有找到可用的free list, 准备重新填充free list
		void* r = Refill(Roundup(n));
		return r;
	}
	// 调整free list
	// 目前的freelist的一块已经被用掉了，所以slot中应该更新freelist的起始位置
	*my_free_list = result->next;
	return result;
}

void SimpleFreeListAlloc::Deallocate(void* p, int32_t n) {
	if(p) {	// p不可以是0
		FreeList* q = (FreeList*)p;
		FreeList* volatile * my_free_list;
		memory_usage_.fetch_sub(n, std::memory_order_relaxed);
		if(n > kSmallObjectBytes) {
			free(p);
			return;
		}
		// 寻找对应的free list
		my_free_list = freelist_ + FreeListIndex(n);
		// 调整free list，回收区块
		q->next = *my_free_list; // 头插法
		*my_free_list = q;

	}
}
void* SimpleFreeListAlloc::Reallocate(void* address, int32_t old_size,
                                      int32_t new_size) {
	Deallocate(address, old_size);
	address = Allocate(new_size);
	return address;
}
} // namespace tinykySimpleFreeListAlloc::