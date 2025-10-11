#include "PageCache.h"
#include <sys/mman.h>
#include <cstring>

namespace Kama_memoryPool
{

    // 辅助函数：链表操作
    static inline void pushSpan(PageCache::Span *&head, PageCache::Span *s)
    {
        s->prev = nullptr;
        s->next = head;
        if (head)
            head->prev = s;
        head = s;
    }
    static inline void removeSpan(PageCache::Span *&head, PageCache::Span *s)
    {
        if (s->prev)
            s->prev->next = s->next;
        else
            head = s->next;
        if (s->next)
            s->next->prev = s->prev;
        s->prev = s->next = nullptr;
    }

    // -----------------------
    // 从系统申请新页
    // -----------------------
    PageCache::Span *PageCache::systemAlloc(size_t numPages)
    {
        size_t bytes = numPages * kPageSize;
        void *mem = mmap(nullptr, bytes, PROT_READ | PROT_WRITE,
                         MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (mem == MAP_FAILED)
            return nullptr;

        allSpans_.push_back({});
        Span *span = &allSpans_.back();
        span->pageAddr = mem;
        span->numPages = numPages;
        span->sizeClass = 0;
        span->freeList = nullptr;
        span->freeObjects = 0;
        span->totalObjects = 0;
        span->prev = span->next = nullptr;

        return span;
    }

    // -----------------------
    // 插入 freeLists
    // -----------------------
    void PageCache::pushToFreeList(Span *span)
    {
        if (span->numPages <= kMaxPagesBucket)
            pushSpan(freeLists_[span->numPages], span);
        else
            pushSpan(bigList_, span);
    }

    // -----------------------
    // 从 freeLists 中取 >=numPages 的 span
    // -----------------------
    PageCache::Span *PageCache::takeFromFreeLists(size_t numPages)
    {
        if (numPages <= kMaxPagesBucket)
        {
            for (size_t i = numPages; i <= kMaxPagesBucket; ++i)
            {
                if (freeLists_[i])
                {
                    Span *s = freeLists_[i];
                    removeSpan(freeLists_[i], s);

                    // 拆分多余部分
                    if (s->numPages > numPages)
                    {
                        allSpans_.push_back({});
                        Span *remain = &allSpans_.back();
                        remain->pageAddr = static_cast<char *>(s->pageAddr) + numPages * kPageSize;
                        remain->numPages = s->numPages - numPages;
                        remain->sizeClass = 0;
                        // 重新进行页号和span的映射
                        mapErase(s);
                        s->numPages = numPages;
                        mapPopulate(s);
                        mapPopulate(remain);

                        pushToFreeList(remain);
                    }
                    return s;
                }
            }
        }

        // >128 页，从 bigList_ 查
        for (Span *it = bigList_; it; it = it->next)
        {
            if (it->numPages >= numPages)
            {
                Span *s = it;
                removeSpan(bigList_, s);

                if (s->numPages > numPages)
                {
                    allSpans_.push_back({});
                    Span *remain = &allSpans_.back();
                    remain->pageAddr = static_cast<char *>(s->pageAddr) + numPages * kPageSize;
                    remain->numPages = s->numPages - numPages;
                    remain->sizeClass = 0;

                    mapErase(s);
                    s->numPages = numPages;
                    mapPopulate(s);
                    mapPopulate(remain);

                    pushToFreeList(remain);
                }
                return s;
            }
        }
        return nullptr;
    }

    // -----------------------
    // 分配 span
    // -----------------------
    PageCache::Span *PageCache::allocateSpan(size_t numPages)
    {
        std::lock_guard<std::mutex> lock(mutex_);

        Span *span = takeFromFreeLists(numPages);
        if (span)
            return span;

        // 若无合适 span，则向系统要更大块（例如至少 64 页）
        size_t sysPages = std::max(numPages, (size_t)64);
        Span *big = systemAlloc(sysPages);
        if (!big)
            return nullptr;

        // 从 big 切 numPages 出来
        if (big->numPages > numPages)
        {
            allSpans_.push_back({});
            Span *remain = &allSpans_.back();
            remain->pageAddr = static_cast<char *>(big->pageAddr) + numPages * kPageSize;
            remain->numPages = big->numPages - numPages;
            remain->sizeClass = 0;
            big->numPages = numPages;
            mapPopulate(big);
            mapPopulate(remain);

            pushToFreeList(remain);
        }

        return big;
    }

    // -----------------------
    // 尝试与前后合并
    // -----------------------
    void PageCache::tryMerge(Span *&span)
    {
        size_t pid = PageIdOf(span->pageAddr);

        // 向前
        while (pid > 0)
        {
            auto it = pageMap_.find(pid - 1);
            if (it == pageMap_.end())
                break;
            Span *left = it->second;
            if (left->sizeClass != 0)
                break;
            if ((char *)left->pageAddr + left->numPages * kPageSize != span->pageAddr)
                break;
            removeFromFreeList(left);
            mapErase(span);
            left->numPages += span->numPages;
            mapPopulate(left);

            span = left;
            pid = PageIdOf(span->pageAddr);
        }
        pid = PageIdOf(span->pageAddr);
        // 向后
        while (true)
        {
            auto it = pageMap_.find(pid + span->numPages);
            if (it == pageMap_.end())
                break;
            Span *right = it->second;
            if (right->sizeClass != 0)
                break;
            if ((char *)span->pageAddr + span->numPages * kPageSize != right->pageAddr)
                break;

            removeFromFreeList(right);
            mapErase(right);

            span->numPages += right->numPages;
            mapPopulate(span); // ✅ 当前 span 继续扩大，不换引用
        }
    }

    // -----------------------
    // 回收 span
    // -----------------------
    void PageCache::deallocateSpan(Span *span)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        span->sizeClass = 0;
        span->freeList = nullptr;
        span->freeObjects = 0;
        span->totalObjects = 0;

        tryMerge(span);
        // ------------------------------
        // 判断是否应直接归还系统
        // ------------------------------
        if (span->numPages > kMaxPagesBucket)
        {
            // 如果是超大 span（挂 bigList_），直接归还系统
            munmap(span->pageAddr, span->numPages * kPageSize);

            mapErase(span);
            // 从 allSpans_ 移除逻辑可选（如保留索引可复用）

            return;
        }
        pushToFreeList(span);
    }

    // -----------------------
    // 映射管理
    // -----------------------
    PageCache::Span *PageCache::mapFind(size_t pageId)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = pageMap_.find(pageId);
        return (it == pageMap_.end()) ? nullptr : it->second;
    }
    void PageCache::mapPopulate(Span *s)
    {
        size_t pid = PageIdOf(s->pageAddr);
        for (size_t i = 0; i < s->numPages; ++i)
            pageMap_[pid + i] = s;
    }
    void PageCache::mapErase(Span *s)
    {
        size_t pid = PageIdOf(s->pageAddr);
        for (size_t i = 0; i < s->numPages; ++i)
            pageMap_.erase(pid + i);
    }

    // -----------------------
    // 释放全部内存
    // -----------------------
    void PageCache::releaseAll()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto &s : allSpans_)
        {
            munmap(s.pageAddr, s.numPages * kPageSize);
        }
        allSpans_.clear();
        pageMap_.clear();
        bigList_ = nullptr;
        for (auto &f : freeLists_)
            f = nullptr;
    }

} // namespace Kama_memoryPool
