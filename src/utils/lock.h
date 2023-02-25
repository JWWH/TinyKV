#pragma once

// pthread是Linux线程库， 这里改用C++11的线程库
//#include <pthread.h>
#include <mutex>
#include <atomic>

namespace tinykv {
// 类似于lock_guard, T接受NullLock、MutexLock、SpinLock
template <class T>
class ScopedLockImple {
public:
	ScopedLockImple(T& lock) : lock_(lock) {
		lock_.Lock();
		is_locked_ = true;
	}
	~ScopedLockImple() {
		UnLock();
	}

	void Lock() {
		if(!is_locked_) {
			lock_.lock();
			is_locked_ = true;
		}
	}

	void UnLock() {
		if (is_locked_) {
			lock_.UnLock();
			is_locked_ = false;
		}
	}
private:
	T& lock_;
	bool is_locked_ = false;
};

// 无锁
class NullLock final {
public:
	NullLock() = default;
	~NullLock() = default;
	void Lock() {}
	void UnLock() {}
};

// 互斥锁
class MutexLock final {
public:
	MutexLock() = default;
	~MutexLock() = default;
	void Lock() { mutex_.lock(); }
	void UnLock() { mutex_.unlock(); }
private:
	std::mutex mutex_;
};

// 自旋锁
class SpinLock final {
public:
	SpinLock() = default;
	SpinLock(const SpinLock&) = delete;
	SpinLock& operator=(const SpinLock&) = delete;
	void Lock() {
		while (flag_.test_and_set());
	}
	void unlock() {
		flag_.clear();
	}
private:
	std::atomic_flag flag_ = 0;
};

}