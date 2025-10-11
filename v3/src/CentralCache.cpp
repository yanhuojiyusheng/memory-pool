#include "CentralCache.h"
#include "PageCache.h"
#include <cassert>
#include <cstring>

namespace Kama_memoryPool
{

    // --------------------------------------------------------
    // 从中心缓存获取一批对象（给 ThreadCache）
    // --------------------------------------------------------
    void *CentralCache::fetchRange(size_t index, size_t &batchNum)
    {
        assert(index < FREE_LIST_SIZE);
        const size_t objSize = SizeClass::classSize(index);
        CentralFreeList &cf = centralLists_[index];
        SpinLockGuard lock(cf.spin);

        // 若无可用 span，则 refill
        if (cf.nonEmptySpans.empty())
        {
            // 目标：每个 span 至少能供 ThreadCache 拿几次 batch，减少 PageCache 调用
            constexpr size_t targetObjectsPerSpan = 4; // 可调参数，越大越激进

            // 计算对象总字节
            size_t targetBytes = objSize * batchNum * targetObjectsPerSpan;

            // 保证至少一页，向上取整页数
            size_t numPages = (targetBytes + kPageSize - 1) / kPageSize;
            numPages = std::clamp<size_t>(numPages, 1, 8);

            PageCache::Span *span = PageCache::getInstance().allocateSpan(numPages);
            if (!span)
            {
                batchNum = 0;
                return nullptr;
            }

            span->sizeClass = static_cast<int>(index);
            span->totalObjects = (numPages * kPageSize) / objSize;
            span->freeObjects = span->totalObjects;

            // 构造 freelist
            char *base = static_cast<char *>(span->pageAddr);
            for (size_t i = 0; i + 1 < span->totalObjects; ++i)
                *reinterpret_cast<void **>(base + i * objSize) = base + (i + 1) * objSize;
            *reinterpret_cast<void **>(base + (span->totalObjects - 1) * objSize) = nullptr;
            span->freeList = base;

            cf.nonEmptySpans.push_front(span); // 新建 span 必定有空闲
        }

        // 队首必然有货（无清扫）
        PageCache::Span *span = cf.nonEmptySpans.front();
        assert(span->freeObjects > 0 && span->freeList != nullptr);

        // 批量取
        size_t realBatch = std::min(batchNum, span->freeObjects);
        if (realBatch == 0)
        {
            batchNum = 0;
            return nullptr;
        }
        batchNum = realBatch;

        void *head = span->freeList;
        void *cur = head;
        for (size_t i = 1; i < realBatch; ++i)
            cur = *reinterpret_cast<void **>(cur);

        void *nextHead = *reinterpret_cast<void **>(cur);
        *reinterpret_cast<void **>(cur) = nullptr;

        span->freeList = nextHead;
        span->freeObjects -= realBatch;

        // 用尽就出队
        if (span->freeObjects == 0)
        {
            cf.nonEmptySpans.pop_front();
        }

        return head;
    }

    void CentralCache::returnRange(size_t index, void *head, size_t n)
    {
        if (!head || n == 0)
            return;

        CentralFreeList &cf = centralLists_[index];

        SpinLockGuard lock(cf.spin);

        PageCache &pageCache = PageCache::getInstance();

        void *cur = head;
        for (size_t i = 0; i < n && cur; ++i)
        {
            void *next = *reinterpret_cast<void **>(cur);

            size_t pageId = PageIdOf(cur);
            PageCache::Span *span = pageCache.mapFind(pageId);

            if (!span)
            {
                cur = next;
                continue; // skip unknown
            }

            // 插入当前对象到对应 span 的 freelist
            *reinterpret_cast<void **>(cur) = span->freeList;
            span->freeList = cur;
            span->freeObjects++;

            // 如果 span 由空变非空
            if (span->freeObjects == 1)
                cf.nonEmptySpans.push_back(span);

            // 如果 span 全部回收，则归还给 PageCache
            if (span->freeObjects == span->totalObjects)
            {
                cf.nonEmptySpans.remove(span);
                span->freeList = nullptr;
                span->sizeClass = 0;
                pageCache.deallocateSpan(span);
            }

            cur = next;
        }
    }

    // --------------------------------------------------------
    // 释放所有 Span（测试 / 销毁用）
    // --------------------------------------------------------
    void CentralCache::releaseAll()
    {
        for (auto &cf : centralLists_)
        {
            SpinLockGuard lock(cf.spin);
            cf.nonEmptySpans.clear();
        }
    }

} // namespace Kama_memoryPool
