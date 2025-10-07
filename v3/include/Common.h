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
        // -----------------------
        // 基础参数定义
        // -----------------------
        static constexpr size_t ALIGNMENT = 8;             // 小对象对齐
        static constexpr size_t MIDALIGNMENT = 128;        // 中对象对齐
        static constexpr size_t BIGALIGNMENT = 4 * 1024;   // 大对象对齐
        static constexpr size_t MID_THRESHOLD = 1024;      // 1KB
        static constexpr size_t BIG_THRESHOLD = 32 * 1024; // 32KB
        static constexpr size_t MAX_BYTES = 256 * 1024;    // 256KB

        static constexpr size_t SMALL_CLASSES = MID_THRESHOLD / ALIGNMENT;                    // 128
        static constexpr size_t MID_CLASSES = (BIG_THRESHOLD - MID_THRESHOLD) / MIDALIGNMENT; // 248
        static constexpr size_t BIG_CLASSES = (MAX_BYTES - BIG_THRESHOLD) / BIGALIGNMENT;     // 56
        static constexpr size_t NUM_CLASSES = SMALL_CLASSES + MID_CLASSES + BIG_CLASSES;      // 432

        // -----------------------
        // 每个大小类的元信息
        // -----------------------
        struct ClassInfo
        {
            size_t size;      // 每个对象的标准字节数（已对齐）
            size_t numToMove; // 每次批量获取数量
            size_t threshold; // 自由链表触发归还的上限
        };

        // -----------------------
        // 构建 class 信息表（仅初始化一次）
        // -----------------------
        static inline const std::array<ClassInfo, NUM_CLASSES> &table()
        {
            static const std::array<ClassInfo, NUM_CLASSES> t = []
            {
                std::array<ClassInfo, NUM_CLASSES> arr{};
                size_t idx = 0;

                // -------------------------------
                // 小对象 [8, 1024]  8B 对齐
                // -------------------------------
                for (size_t i = 0; i < SMALL_CLASSES; ++i, ++idx)
                {
                    size_t sz = (i + 1) * ALIGNMENT;
                    // 每批约 4KB，总量上限 512
                    size_t batch = std::max<size_t>(2, 4096 / sz);
                    batch = std::min<size_t>(batch, 512);

                    // 基础门限（base threshold）
                    size_t base;
                    if (sz <= 64)
                        base = 512;
                    else if (sz <= 256)
                        base = 256;
                    else
                        base = 128;

                    // 最终门限 = max(base, batch * 2)
                    size_t threshold = std::max(base, batch * 2);

                    arr[idx] = {sz, batch, threshold};
                }

                // -------------------------------
                // 中对象 (1KB, 32KB]  128B 对齐
                // -------------------------------
                for (size_t i = 0; i < MID_CLASSES; ++i, ++idx)
                {
                    size_t sz = MID_THRESHOLD + (i + 1) * MIDALIGNMENT;
                    // 每批约 32KB
                    size_t batch = std::max<size_t>(1, 32768 / sz);

                    // 基础门限
                    size_t base;
                    if (sz <= 4096)
                        base = 64;
                    else
                        base = 32;

                    // 最终门限
                    size_t threshold = std::max(base, batch * 2);

                    arr[idx] = {sz, batch, threshold};
                }

                // -------------------------------
                // 大对象 (32KB, 256KB]  4KB 对齐
                // -------------------------------
                for (size_t i = 0; i < BIG_CLASSES; ++i, ++idx)
                {
                    size_t sz = BIG_THRESHOLD + (i + 1) * BIGALIGNMENT;
                    size_t batch = 1; // 太大不适合批量

                    // 基础门限：大对象几乎不缓存
                    size_t base = (sz <= 65536) ? 8 : 4;
                    size_t threshold = std::max(base, batch * 2);

                    arr[idx] = {sz, batch, threshold};
                }

                return arr;
            }();
            return t;
        }

        // -----------------------
        // roundUp: 向上取整到对应对齐粒度
        // -----------------------
        static inline size_t roundUp(size_t bytes)
        {
            if (bytes <= MID_THRESHOLD)
                return (bytes + ALIGNMENT - 1) & ~(ALIGNMENT - 1);
            else if (bytes <= BIG_THRESHOLD)
                return (bytes + MIDALIGNMENT - 1) & ~(MIDALIGNMENT - 1);
            else if (bytes <= MAX_BYTES)
                return (bytes + BIGALIGNMENT - 1) & ~(BIGALIGNMENT - 1);
            else
                return bytes;
        }

        // -----------------------
        // getIndex: 获取对应的 size class 下标
        // -----------------------
        static inline size_t getIndex(size_t bytes)
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
                // 中对象 (1024, 32768] -> [128, 375]
                return SMALL_CLASSES + (size - MID_THRESHOLD - 1) / MIDALIGNMENT;
            }
            else if (size <= MAX_BYTES)
            {
                // 大对象 (32768, 262144] -> [376, 431]
                return SMALL_CLASSES + MID_CLASSES + (size - BIG_THRESHOLD - 1) / BIGALIGNMENT;
            }
            else
            {
                return static_cast<size_t>(-1);
            }
        }

        // -----------------------
        // 查询接口
        // -----------------------
        static inline size_t classSize(size_t index)
        {
            return table()[index].size;
        }

        static inline size_t getBatchNum(size_t index)
        {
            return table()[index].numToMove;
        }

        static inline size_t getThreshold(size_t index)
        {
            return table()[index].threshold;
        }
    };
} // namespace memoryPool