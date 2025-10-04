#pragma once
#include <cstddef>
#include <atomic>
#include <array>
#include <cstdlib>
#include <algorithm>
namespace Kama_memoryPool
{
    // 对齐数和大小定义
    constexpr size_t ALIGNMENT = 8;
    constexpr size_t MIDALIGNMENT = 128;
    constexpr size_t BIGALIGNMENT = 4 * 1024;
    constexpr size_t MAX_BYTES = 256 * 1024; // 256KB
    constexpr size_t MID_THRESHOLD = 1024;
    constexpr size_t BIG_THRESHOLD = 32 * 1024;
    constexpr size_t SMALL_CLASSES = MID_THRESHOLD / ALIGNMENT;                    // 128
    constexpr size_t MID_CLASSES = (BIG_THRESHOLD - MID_THRESHOLD) / MIDALIGNMENT; // 248
    constexpr size_t BIG_CLASSES = (MAX_BYTES - BIG_THRESHOLD) / BIGALIGNMENT;     // 56
    constexpr size_t NUM_CLASSES = SMALL_CLASSES + MID_CLASSES + BIG_CLASSES;      // 432
    constexpr size_t FREE_LIST_SIZE = NUM_CLASSES;
    const size_t KThreadMaxSize = 4 << 20;
    // 内存块头部信息
    struct BlockHeader
    {
        size_t size;       // 内存块大小
        bool inUse;        // 使用标志
        BlockHeader *next; // 指向下一个内存块
    };

    class SizeClass
    {
    public:
        // 把任意 size round up 到对应的对齐
        static size_t roundUp(size_t bytes)
        {
            if (bytes <= MID_THRESHOLD)
            {
                return (bytes + ALIGNMENT - 1) & ~(ALIGNMENT - 1);
            }
            else if (bytes <= BIG_THRESHOLD)
            {
                return (bytes + MIDALIGNMENT - 1) & ~(MIDALIGNMENT - 1);
            }
            else if (bytes <= MAX_BYTES)
            {
                return (bytes + BIGALIGNMENT - 1) & ~(BIGALIGNMENT - 1);
            }
            else
            {
                return bytes; // 超过最大值，直接走系统 malloc
            }
        }

        // 获取 size 对应的 freelist 索引
        static size_t getIndex(size_t bytes)
        {
            bytes = std::max(bytes, (size_t)ALIGNMENT);
            size_t size = roundUp(bytes);

            if (size <= MID_THRESHOLD)
            {
                // 小对象 [8, 1024] -> [0, 127]
                return (size / ALIGNMENT) - 1;
            }
            else if (size <= BIG_THRESHOLD)
            {
                // 中对象 (1024, 32768] -> [128, 128+247]
                return +(size - MID_THRESHOLD - 1) / MIDALIGNMENT;
            }
            else if (size <= MAX_BYTES)
            {
                // 大对象 (32768, 262144] -> [128+248, 431]
                return SMALL_CLASSES + MID_CLASSES + (size - BIG_THRESHOLD - 1) / BIGALIGNMENT;
            }
            else
            {
                // 超过范围，不走 freelist
                return (size_t)-1;
            }
        }
    };
} // namespace memoryPool