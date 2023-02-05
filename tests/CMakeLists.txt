# 查找 GTest 库
find_package(GTest REQUIRED)
# GTest 的头文件
include_directories(${GTEST_INCLUDE_DIRS})

aux_source_directory(. TEST_LIST)
aux_source_directory(../src SRC_FOR_TEST_LIST)
add_executable(tinykv-unitest ${SRC_FOR_TEST_LIST} ${TEST_LIST})

# 链接测试库
target_link_libraries(tinykv-unitest
        ${GTEST_BOTH_LIBRARIES}
        pthread )

# 添加到测试
gtest_discover_tests(tinykv-unitest)
