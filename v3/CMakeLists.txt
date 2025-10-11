cmake_minimum_required(VERSION 3.10)
project(memory_pool)

# 设置C++标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 编译选项
add_compile_options(-Wall -O2)
# # 启用 AddressSanitizer 调试
# if (CMAKE_BUILD_TYPE STREQUAL "Debug")
#     message(STATUS "AddressSanitizer enabled (Debug mode)")
#     add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
#     add_link_options(-fsanitize=address)
# endif()
# 查找pthread库
find_package(Threads REQUIRED)

# 设置目录
set(SRC_DIR ${CMAKE_SOURCE_DIR}/src)
set(INC_DIR ${CMAKE_SOURCE_DIR}/include)
set(TEST_DIR ${CMAKE_SOURCE_DIR}/tests)

# 源文件
file(GLOB SOURCES "${SRC_DIR}/*.cpp")

# 添加头文件目录
include_directories(${INC_DIR})

# 创建单元测试可执行文件
add_executable(unit_test 
    ${SOURCES}
    ${TEST_DIR}/UnitTest.cpp
)

# 创建性能测试可执行文件
add_executable(perf_test
    ${SOURCES}
    ${TEST_DIR}/PerformanceTest.cpp
)

# 创建性能测试可执行文件
add_executable(new_test
    ${SOURCES}
    ${TEST_DIR}/NewTest.cpp
)

# 链接pthread库
target_link_libraries(unit_test PRIVATE Threads::Threads)
target_link_libraries(perf_test PRIVATE Threads::Threads)
target_link_libraries(new_test PRIVATE Threads::Threads)


# 添加测试命令
add_custom_target(test
    COMMAND ./unit_test
    DEPENDS unit_test
)

add_custom_target(perf
    COMMAND ./perf_test
    DEPENDS perf_test
)
add_custom_target(new
    COMMAND ./new_test
    DEPENDS new_test
)