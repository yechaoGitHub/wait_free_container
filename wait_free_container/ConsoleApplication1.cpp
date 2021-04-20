#include <iostream>
#include <thread>
#include <vector>
#include "wait_free_memory_pool.hpp"
#include <stdint.h>
#include <assert.h>
#include <limits>

wait_free_memory_pool<int> pool;

int main()
{
	auto it_p =  pool.allocate();



	return 0;
}
