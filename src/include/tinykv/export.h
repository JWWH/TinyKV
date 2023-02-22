#ifndef STORAGE_LEVELDB_INCLUDE_EXPORT_H_
#define STORAGE_LEVELDB_INCLUDE_EXPORT_H_

/**
 * 总体来说，该头文件是为了解决C++多动态库标识符冲突的问题
 * 详见:https://dapiqing.cn/2019/11/26/c-c-%E5%A4%9A%E5%8A%A8%E6%80%81%E5%BA%93%E6%A0%87%E8%AF%86%E7%AC%A6%E5%86%B2%E7%AA%81/#%E8%8C%83%E4%BE%8B%EF%BC%88leveldb%EF%BC%89
 */

#if !defined(LEVELDB_EXPORT)

#if defined(LEVELDB_SHARED_LIBRARY)
#if defined(_WIN32)
// Windows 系列
#if defined(LEVELDB_COMPILE_LIBRARY)
#define LEVELDB_EXPORT __declspec(dllexport)
#else
#define LEVELDB_EXPORT __declspec(dllimport)
#endif  // defined(LEVELDB_COMPILE_LIBRARY)
// Linux gcc系列
#else  // defined(_WIN32)
#if defined(LEVELDB_COMPILE_LIBRARY)
#define LEVELDB_EXPORT __attribute__((visibility("default")))
#else
#define LEVELDB_EXPORT
#endif
#endif  // defined(_WIN32)

#else  // defined(LEVELDB_SHARED_LIBRARY)
#define LEVELDB_EXPORT
#endif

#endif  // !defined(LEVELDB_EXPORT)

#endif  // STORAGE_LEVELDB_INCLUDE_EXPORT_H_