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
    constexpr size_t MIDALIGNMENT = 64;
    constexpr size_t BIGALIGNMENT = 256;
    constexpr size_t MAX_BYTES = 32 * 1024; // 32KB
    constexpr size_t MID_THRESHOLD = 1024;
    constexpr size_t BIG_THRESHOLD = 8 * 1024;
    constexpr size_t SMALL_CLASSES = MID_THRESHOLD / ALIGNMENT;                    // 128
    constexpr size_t MID_CLASSES = (BIG_THRESHOLD - MID_THRESHOLD) / MIDALIGNMENT; // 112
    constexpr size_t BIG_CLASSES = (MAX_BYTES - BIG_THRESHOLD) / BIGALIGNMENT;     // 96
    constexpr size_t NUM_CLASSES = SMALL_CLASSES + MID_CLASSES + BIG_CLASSES;      // 384
    constexpr size_t FREE_LIST_SIZE = NUM_CLASSES;
    const size_t KThreadMaxSize = 4 << 20;

    // 统一 4KB 页
    static constexpr size_t kPageShift = 12;
    static constexpr size_t kPageSize = 1ULL << kPageShift;

    inline size_t PageIdOf(const void *p)
    {
        return (reinterpret_cast<uintptr_t>(p) >> kPageShift);
    }

    inline void *PageAddrOf(size_t page_id)
    {
        return reinterpret_cast<void *>(page_id << kPageShift);
    }

    class SizeClass
    {
    public:
        // -----------------------
        // 每个大小类的元信息
        // -----------------------
        struct ClassInfo
        {
            size_t size;      // 每个对象的标准字节数（已对齐）
            size_t numToMove; // 每次批量获取数量
            size_t threshold; // 自由链表触发归还的上限
        };

        // ================================================================
        // SizeClass::table() —— 自适应分级 size-class 映射表生成函数
        // 适配对齐与区间定义：
        //   [8, 1KB]     —— 8B 对齐，小对象，共 128 类
        //   (1KB, 8KB]   —— 64B 对齐，中对象，共 112 类
        //   (8KB, 32KB]  —— 256B 对齐，大对象，共 96 类
        //   总计 384 类
        // ================================================================
        static inline const std::array<ClassInfo, NUM_CLASSES> &table()
        {
            auto clamp = [](size_t v, size_t lo, size_t hi)
            {
                return std::max(lo, std::min(v, hi));
            };

            static const std::array<ClassInfo, NUM_CLASSES> t = [&clamp]()
            {
                std::array<ClassInfo, NUM_CLASSES> arr{};
                size_t idx = 0;

                // ================================================================
                // 一、小对象区间 [8B, 1024B] —— 8B 对齐
                // ------------------------------------------------
                // - 批量 (batch)：一次 transfer 约 32KB，对应 batch = 32KB / size
                //   限制在 [2, 256]，保证小对象批量足够
                // - 阈值 (threshold)：ThreadCache 最大保留量
                //   = max(base, batch * 6)，允许热点对象留更多
                // ================================================================
                for (size_t i = 0; i < SMALL_CLASSES; ++i, ++idx)
                {
                    size_t sz = (i + 1) * ALIGNMENT;
                    size_t batch = clamp((32 * 1024) / sz, (size_t)2, (size_t)256);

                    size_t base;
                    if (sz <= 64)
                        base = 1024;
                    else if (sz <= 256)
                        base = 512;
                    else
                        base = 256;

                    size_t threshold = std::max(base, batch * 6);
                    arr[idx] = {sz, batch, threshold};
                }

                // ================================================================
                // 二、中对象区间 (1KB, 8KB] —— 64B 对齐
                // ------------------------------------------------
                // - 一次 transfer 约 32KB
                // - batch = clamp(32KB / size, 1, 128)
                // - threshold = max(base, batch * 4)
                // ================================================================
                for (size_t i = 0; i < MID_CLASSES; ++i, ++idx)
                {
                    size_t sz = MID_THRESHOLD + (i + 1) * MIDALIGNMENT;
                    size_t batch = clamp((32 * 1024) / sz, (size_t)1, (size_t)128);

                    size_t base;
                    if (sz <= 4 * 1024)
                        base = 128;
                    else
                        base = 64;

                    size_t threshold = std::max(base, batch * 4);
                    arr[idx] = {sz, batch, threshold};
                }

                // ================================================================
                // 三、大对象区间 (8KB, 32KB] —— 256B 对齐
                // ------------------------------------------------
                // - 对象太大，不适合批量分配
                // - batch 固定为 1
                // - threshold 仅少量缓存（16~64）
                // ================================================================
                for (size_t i = 0; i < BIG_CLASSES; ++i, ++idx)
                {
                    size_t sz = BIG_THRESHOLD + (i + 1) * BIGALIGNMENT;
                    size_t batch = 1;
                    size_t base = (sz <= 16 * 1024) ? 64 : 32;
                    size_t threshold = base;

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