#pragma once
#include <cstddef>
#include <atomic>
#include <array>
#include <cstdlib>
namespace Kama_memoryPool
{
    // 对齐数和大小定义
    constexpr size_t ALIGNMENT = 8;
    constexpr size_t MIDALIGNMENT = 128;
    constexpr size_t BIGALIGNMENT = 4*1024;
    constexpr size_t MAX_BYTES = 256 * 1024;                 // 256KB
    constexpr size_t MID_THRESHOLD = 1024;
    constexpr size_t BIG_THRESHOLD = 32*1024;
    constexpr size_t SMALL_FREE_LIST_SIZE = MID_THRESHOLD / ALIGNMENT; // 32k  ALIGNMENT等于指针void*的大小
    constexpr size_t MID_FREE_LIST_SIZE = (BIG_THRESHOLD-MID_THRESHOLD)/MIDALIGNMENT;
    constexpr size_t BIG_FREE_LIST_SIZE = (MAX_BYTES-BIG_THRESHOLD)/BIGALIGNMENT;
    constexpr size_t FREE_LIST_SIZE = SMALL_FREE_LIST_SIZE+MID_FREE_LIST_SIZE+BIG_FREE_LIST_SIZE;
    const size_t KThreadMaxSize = 4 << 20;
    // 内存块头部信息
    struct BlockHeader
    {
        size_t size;       // 内存块大小
        bool inUse;        // 使用标志
        BlockHeader *next; // 指向下一个内存块
    };

    // 大小类管理
    class SizeClass
    {
    public:
        static size_t roundUp(size_t bytes) // 向上取整到ALIGNMENT 的倍数
        {
            return (bytes + ALIGNMENT - 1) & ~(ALIGNMENT - 1);
        }

        static size_t getIndex(size_t bytes)
        {
            // 确保bytes至少为ALIGNMENT
            bytes = std::max(bytes, ALIGNMENT);
            // 向上取整后-1
            return (bytes + ALIGNMENT - 1) / ALIGNMENT - 1;
        }
    };

} // namespace memoryPool