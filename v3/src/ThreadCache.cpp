#include "../include/ThreadCache.h"
#include "../include/CentralCache.h"
#include <cassert>

namespace Kama_memoryPool
{

    void *ThreadCache::allocate(size_t size)
    {
        // 处理0大小的分配请求
        if (size < ALIGNMENT)
        {
            size = ALIGNMENT; // 至少分配一个对齐大小
        }

        if (size > MAX_BYTES)
        {
            // 大对象直接从系统分配
            return malloc(size);
        }

        size_t index = SizeClass::getIndex(size);

        // 检查线程本地自由链表
        // 如果 freeList_[index] 不为空，表示该链表中有可用内存块
        if (void *ptr = freeList_[index])
        {
            freeList_[index] = *reinterpret_cast<void **>(ptr); // 将freeList_[index]指向的内存块的下一个内存块地址（取决于内存块的实现）
            freeListSize_[index]--;
            threadBytes_ -= SizeClass::classSize(index);
            return ptr;
        }

        // 如果线程本地自由链表为空，则从中心缓存获取一批内存
        return fetchFromCentralCache(index);
    }

    void ThreadCache::deallocate(void *ptr, size_t size)
    {
        if (size > MAX_BYTES)
        {
            free(ptr);
            return;
        }

        size_t index = SizeClass::getIndex(size);

        // 插入到线程本地自由链表
        *reinterpret_cast<void **>(ptr) = freeList_[index];
        freeList_[index] = ptr;

        // 更新自由链表大小
        freeListSize_[index]++; // 增加对应大小类的自由链表大小
        threadBytes_ += SizeClass::classSize(index);
        // 判断是否需要将部分内存回收给中心缓存
        if (shouldReturnToCentralCache(index))
        {
            returnToCentralCache(index);
        }
    }

    void ThreadCache::releaseAll()
    {
        for (size_t i = 0; i < FREE_LIST_SIZE; ++i)
        {
            freeList_[i] = nullptr;
            freeListSize_[i] = 0;
        }
    }
    // 判断是否需要将内存回收给中心缓存
    bool ThreadCache::shouldReturnToCentralCache(size_t index)
    {
        return (freeListSize_[index] > SizeClass::getThreshold(index));
    }

    void *ThreadCache::fetchFromCentralCache(size_t index)
    {
        // 根据对象内存大小计算批量获取的数量
        size_t batchNum = SizeClass::getBatchNum(index);
        // 从中心缓存批量获取内存
        void *start = CentralCache::getInstance().fetchRange(index, batchNum);
        if (!start)
            return nullptr;

        // 更新自由链表大小
        freeListSize_[index] += batchNum - 1; // 增加对应大小类的自由链表大小
        threadBytes_ += (batchNum - 1) * SizeClass::classSize(index);

        // 取一个返回，其余放入线程本地自由链表
        void *result = start;
        if (batchNum > 1)
        {
            freeList_[index] = *reinterpret_cast<void **>(start);
        }

        return result;
    }

    void ThreadCache::returnToCentralCache(size_t index)
    {
        if (index >= FREE_LIST_SIZE)
            return;

        void *head = freeList_[index];
        size_t listLen = freeListSize_[index];
        if (!head || listLen <= 1)
            return;

        size_t classSize = SizeClass::classSize(index);
        size_t threshold = SizeClass::getThreshold(index);
        size_t numToMove = SizeClass::getBatchNum(index);

        // 目标保留长度（阈值的一半或批量数）
        size_t targetKeep = std::max(threshold / 2, numToMove);
        if (listLen <= targetKeep)
            return;
        size_t returnNum = listLen - targetKeep;

        // 走到保留段的尾节点
        void *cur = head;
        for (size_t i = 1; i < targetKeep && cur; ++i)
            cur = *reinterpret_cast<void **>(cur);
        if (!cur)
            return;

        // 断链
        void *returnHead = *reinterpret_cast<void **>(cur);
        *reinterpret_cast<void **>(cur) = nullptr;

        // 更新本地缓存统计
        freeList_[index] = head;
        freeListSize_[index] = targetKeep;
        threadBytes_ -= returnNum * classSize;

        // 调用中心缓存释放接口
        if (returnHead && returnNum > 0)
            CentralCache::getInstance().returnRange(returnHead,returnNum, index);
    }

} // namespace memoryPool