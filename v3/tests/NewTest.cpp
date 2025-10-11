#include "../include/MemoryPool.h"
#include <iostream>
#include <vector>
#include <chrono>
#include <random>
#include <iomanip>
#include <thread>
#include <atomic>
#include <numeric>

using namespace Kama_memoryPool;
using namespace std::chrono;

// ======================
// 工具类：高精度计时器
// ======================
class Timer {
    high_resolution_clock::time_point start;
public:
    Timer() : start(high_resolution_clock::now()) {}
    double elapsedMs() const {
        auto end = high_resolution_clock::now();
        return duration_cast<microseconds>(end - start).count() / 1000.0;
    }
};

// ======================
// 统计结构
// ======================
struct TestStats {
    double timeMs{0.0};
    size_t allocs{0};
    size_t totalBytes{0};
    std::string name;
};

// ======================
// 通用测试函数模板
// ======================
template <typename AllocFunc, typename FreeFunc>
TestStats runTest(const std::string& name,
                  AllocFunc allocFn, FreeFunc freeFn,
                  const std::vector<size_t>& sizes,
                  int numAllocs, bool randomFree = true)
{
    std::vector<std::pair<void*, size_t>> ptrs;
    ptrs.reserve(numAllocs);

    Timer timer;
    for (int i = 0; i < numAllocs; ++i) {
        size_t sz = sizes[i % sizes.size()];
        void* p = allocFn(sz);
        ptrs.emplace_back(p, sz);

        // 随机释放，模拟碎片
        if (randomFree && rand() % 10 < 3 && !ptrs.empty()) {
            freeFn(ptrs.back().first, ptrs.back().second);
            ptrs.pop_back();
        }
    }

    // 释放剩余
    for (auto& [p, s] : ptrs) freeFn(p, s);

    double ms = timer.elapsedMs();
    size_t totalBytes = std::accumulate(
        sizes.begin(), sizes.end(), 0ull) * numAllocs / sizes.size();

    return {ms, static_cast<size_t>(numAllocs), totalBytes, name};
}

// ======================
// 多线程测试
// ======================
template <typename AllocFunc, typename FreeFunc>
TestStats runMultithreaded(const std::string& name,
                           AllocFunc allocFn, FreeFunc freeFn,
                           const std::vector<size_t>& sizes,
                           int numThreads, int perThread)
{
    std::vector<std::thread> threads;
    std::atomic<size_t> totalOps{0};
    Timer t;

    for (int i = 0; i < numThreads; ++i) {
        threads.emplace_back([&, i]() {
            std::mt19937 gen(i + 12345);
            std::uniform_int_distribution<> dis(0, sizes.size() - 1);
            std::vector<std::pair<void*, size_t>> localPtrs;
            localPtrs.reserve(perThread);

            for (int j = 0; j < perThread; ++j) {
                size_t sz = sizes[dis(gen)];
                void* p = allocFn(sz);
                localPtrs.emplace_back(p, sz);
                if (rand() % 100 < 70 && !localPtrs.empty()) {
                    freeFn(localPtrs.back().first, localPtrs.back().second);
                    localPtrs.pop_back();
                }
            }
            for (auto& [p, s] : localPtrs) freeFn(p, s);
            totalOps += perThread;
        });
    }
    for (auto& th : threads) th.join();

    double elapsed = t.elapsedMs();
    return {elapsed, totalOps.load(), 0, name};
}

// ======================
// 格式化输出
// ======================
void printResult(const TestStats& pool, const TestStats& sys) {
    double speedup = (sys.timeMs - pool.timeMs) / sys.timeMs * 100.0;
    std::cout << std::fixed << std::setprecision(2)
              << "  " << std::left << std::setw(18) << pool.name
              << "  Pool: " << std::setw(8) << pool.timeMs << " ms  "
              << "  System: " << std::setw(8) << sys.timeMs << " ms  "
              << "  ↑ Speedup: " << speedup << "%\n";
}

// ======================
// 主测试逻辑
// ======================
int main() {
    std::cout << "=============================\n";
    std::cout << " MemoryPool Performance Test \n";
    std::cout << "=============================\n";

    // ---------- Warmup ----------
    std::cout << "\n[1] Warmup..." << std::endl;
    for (int i = 0; i < 5000; ++i)
        MemoryPool::deallocate(MemoryPool::allocate(64), 64);

    // ---------- Small Alloc ----------
    std::cout << "\n[2] Small allocations test\n";
    std::vector<size_t> smallSizes = {16, 32, 64, 128};
    auto poolSmall = runTest("Small alloc", 
        [](size_t s){ return MemoryPool::allocate(s); },
        [](void* p, size_t s){ MemoryPool::deallocate(p, s); },
        smallSizes, 100000);
    auto sysSmall = runTest("Small alloc", 
        [](size_t s){ return new char[s]; },
        [](void* p, size_t){ delete[] (char*)p; },
        smallSizes, 100000);
    printResult(poolSmall, sysSmall);

    // ---------- Mixed sizes ----------
    std::cout << "\n[3] Mixed size allocations\n";
    std::vector<size_t> mixedSizes = {16, 32, 128, 512, 1024, 4096};
    auto poolMixed = runTest("Mixed alloc", 
        [](size_t s){ return MemoryPool::allocate(s); },
        [](void* p, size_t s){ MemoryPool::deallocate(p, s); },
        mixedSizes, 50000);
    auto sysMixed = runTest("Mixed alloc", 
        [](size_t s){ return new char[s]; },
        [](void* p, size_t){ delete[] (char*)p; },
        mixedSizes, 50000);
    printResult(poolMixed, sysMixed);

    // ---------- Multi-thread ----------
    std::cout << "\n[4] Multi-thread stress test\n";
    std::vector<size_t> mtSizes = {16, 64, 256, 1024};
    auto poolMT = runMultithreaded("Multithread", 
        [](size_t s){ return MemoryPool::allocate(s); },
        [](void* p, size_t s){ MemoryPool::deallocate(p, s); },
        mtSizes, 8, 20000);
    auto sysMT = runMultithreaded("Multithread", 
        [](size_t s){ return new char[s]; },
        [](void* p, size_t){ delete[] (char*)p; },
        mtSizes, 8, 20000);
    printResult(poolMT, sysMT);

    // ---------- Fragmentation test ----------
    std::cout << "\n[5] Fragmentation stress test (random reuse)\n";
    std::vector<size_t> fragSizes;
    for (int i = 0; i < 100; ++i)
        fragSizes.push_back((rand() % 4096) + 8);
    auto poolFrag = runTest("Fragmentation",
        [](size_t s){ return MemoryPool::allocate(s); },
        [](void* p, size_t s){ MemoryPool::deallocate(p, s); },
        fragSizes, 80000);
    auto sysFrag = runTest("Fragmentation",
        [](size_t s){ return new char[s]; },
        [](void* p, size_t){ delete[] (char*)p; },
        fragSizes, 80000);
    printResult(poolFrag, sysFrag);

    std::cout << "\n[✓] All tests completed.\n";
    MemoryPool::releaseAll();
    return 0;
}
