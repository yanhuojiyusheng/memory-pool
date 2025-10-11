#pragma once
#include "ThreadCache.h"
#include "CentralCache.h"
#include "PageCache.h"
#include <sys/mman.h>
namespace Kama_memoryPool
{

    class MemoryPool
    {
    public:
        static void *allocate(size_t size)
        {
            if (size > MAX_BYTES)
            {
                // ---- 大对象直接从 PageCache 分配 ----
                size_t numPages = (size + kPageSize - 1) / kPageSize;
                PageCache::Span *span = PageCache::getInstance().allocateSpan(numPages);
                if (!span)
                    return nullptr; // mmap 失败

                // 标记为独立分配
                span->sizeClass = -1; // 特殊标志（非小块）
                span->freeList = nullptr;
                span->freeObjects = 1;
                span->totalObjects = 1;

                return span->pageAddr;
            }

            // 小对象走 ThreadCache / CentralCache 正常流程
            return ThreadCache::getInstance()->allocate(size);
        }

        static void deallocate(void *ptr, size_t size)
        {
            if (size > MAX_BYTES)
            {
                // ---- 大对象：从 PageCache 回收 ----
                PageCache &pc = PageCache::getInstance();
                size_t pageId = PageIdOf(ptr);
                PageCache::Span *span = pc.mapFind(pageId);

                if (span && span->sizeClass == -1)
                {
                    pc.deallocateSpan(span);
                }
                return;
            }
            ThreadCache::getInstance()->deallocate(ptr, size);
        }
        static void releaseAll()
        {
            ThreadCache::getInstance()->releaseAll();
            CentralCache::getInstance().releaseAll();
            PageCache::getInstance().releaseAll();
        }
    };

} // namespace memoryPool