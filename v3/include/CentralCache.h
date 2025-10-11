#pragma once
#include "Common.h"
#include "PageCache.h"
#include <array>
#include <list>
#include <mutex>

namespace Kama_memoryPool
{

    class CentralCache
    {
    public:
        static CentralCache &getInstance()
        {
            static CentralCache instance;
            return instance;
        }

        // 从中心缓存获取一批对象
        void *fetchRange(size_t index, size_t &batchNum);

        // 将一批对象归还中心缓存
        void returnRange(size_t index, void *head, size_t n);

        void releaseAll();

    private:
        CentralCache() = default;
        CentralCache(const CentralCache &) = delete;
        CentralCache &operator=(const CentralCache &) = delete;

    private:
        struct CentralFreeList
        {
            std::atomic_flag spin = ATOMIC_FLAG_INIT;
            std::list<PageCache::Span *> nonEmptySpans;
        };
        std::array<CentralFreeList, FREE_LIST_SIZE> centralLists_;
    };
    // 自旋锁 RAII 封装
    class SpinLockGuard
    {
    public:
        explicit SpinLockGuard(std::atomic_flag &flag) : flag_(flag)
        {
            while (flag_.test_and_set(std::memory_order_acquire))
            {
                // 轻量级自旋 + hint，避免忙等浪费
                __builtin_ia32_pause();
            }
        }
        ~SpinLockGuard()
        {
            flag_.clear(std::memory_order_release);
        }

    private:
        std::atomic_flag &flag_;
    };

} // namespace memoryPool
