#include <iostream>
#include <thread>
#include <vector>
#include "wait_free_memory_pool.hpp"
#include <stdint.h>
#include <assert.h>
#include <limits>

wait_free_memory_pool<int> pool;
std::atomic<bool> running { true };

void allocate_func() 
{
    while (running)
    {
        pool.allocate();
        std::this_thread::yield();
    }
}

void deallocate_func() 
{
    while (running)
    {
        size_t capacity = pool.capacity();
    }
}

int main()
{
    std::thread th1(allocate_func);
    std::thread th2(allocate_func);
    std::thread th3(allocate_func);

    std::this_thread::sleep_for(std::chrono::seconds(2));

    th1.join();
    th2.join();
    th3.join();

	return 0;
}

