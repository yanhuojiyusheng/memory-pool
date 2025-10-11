#pragma once
#include "Common.h"
#include <unordered_map>
#include <array>
#include <mutex>
#include <list>

namespace Kama_memoryPool
{

    class PageCache
    {
    public:
        static PageCache &getInstance()
        {
            static PageCache instance;
            return instance;
        }

        struct Span
        {
            void *pageAddr = nullptr; // 起始页地址
            size_t numPages = 0;      // 连续页数
            int sizeClass = 0;        // 0=未分割，>0 属于哪个 size class
            void *freeList = nullptr; // 若被切成小块：小块链表
            size_t freeObjects = 0;   // 当前空闲小块数
            size_t totalObjects = 0;  // 小块总数（Central 用）
            Span *prev = nullptr;
            Span *next = nullptr;
        };

    public:
        // -----------------------
        // 核心接口
        // -----------------------
        Span *allocateSpan(size_t numPages);
        void deallocateSpan(Span *span);

        // 页号查找（Central 用）
        Span *mapFind(size_t pageId);
        void mapPopulate(Span *span);
        void mapErase(Span *span);

        // 清空（测试或销毁）
        void releaseAll();

    private:
        PageCache() = default;
        PageCache(const PageCache &) = delete;
        PageCache &operator=(const PageCache &) = delete;

        // 从系统申请新页
        Span *systemAlloc(size_t numPages);

        // 内部操作：取 >=numPages 的最小可用 Span（可能拆分）
        Span *takeFromFreeLists(size_t numPages);

        // 尝试与前后合并
        void tryMerge(Span *&span);

        // 把 Span 插入空闲桶
        void pushToFreeList(Span *span);
        inline void removeFromFreeList(Span *s)
        {
            Span **head = (s->numPages <= kMaxPagesBucket) ? &freeLists_[s->numPages]
                                                           : &bigList_;
            if (*head == s)
                *head = s->next;
            if (s->prev)
                s->prev->next = s->next;
            if (s->next)
                s->next->prev = s->prev;
            s->prev = s->next = nullptr;
        }

    private:
        // 1~128 页的桶
        static constexpr size_t kMaxPagesBucket = 128;
        std::array<Span *, kMaxPagesBucket + 1> freeLists_{};

        // >128 页的 span 挂在 bigList_
        Span *bigList_ = nullptr;

        // 页号 -> Span* 映射
        std::unordered_map<size_t, Span *> pageMap_;

        // Span 管理容器
        std::list<Span> allSpans_;

        std::mutex mutex_;
    };

} // namespace Kama_memoryPool
