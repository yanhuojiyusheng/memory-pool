#pragma once
#include "ThreadCache.h"
#include "CentralCache.h"
#include "PageCache.h"
namespace Kama_memoryPool
{

    class MemoryPool
    {
    public:
        static void *allocate(size_t size)
        {
            return ThreadCache::getInstance()->allocate(size);
        }

        static void deallocate(void *ptr, size_t size)
        {
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