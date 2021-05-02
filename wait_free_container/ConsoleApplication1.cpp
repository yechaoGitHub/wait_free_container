#include <iostream>
#include <thread>
#include <vector>
#include "wait_free_generic_queue.hpp"
#include <stdint.h>
#include <assert.h>
#include <limits>

class A
{
public:
	
};


wait_free_generic_queue<A> queue;
std::atomic<bool> runnning = true;

void EnqueueFunction() 
{
	int ret(0);

	while (runnning)
	{
		int r = ::rand() % 100; 
		if (r > 50)
		{
			ret = queue.enqueue(A());
			assert(ret != -1);
		}
		else 
		{
			std::vector<A> vec(10, A());
			ret = queue.enqueue_range(vec.begin(), vec.end());
			assert(ret != -1);
		}
	}
}

void DequeueFunction() 
{
	int ret(0);
	while (runnning)
	{
		int r = ::rand() % 100;
		if (r > 50) 
		{
			ret = queue.dequeue();
			//assert(ret != -1);
		}
		else 
		{
			std::vector<A> vec(10, A());
			auto it_begin = vec.begin();
			ret = queue.dequeue_range(it_begin, vec.end());
			//assert(ret != -1);
		}
	}
}

int main()
{
	std::thread th1(EnqueueFunction);
	std::thread th2(EnqueueFunction);
	std::thread th3(EnqueueFunction);
	
	std::thread th4(DequeueFunction);
	std::thread th5(DequeueFunction);

	std::this_thread::sleep_for(std::chrono::seconds(3));
	runnning = false;

	th1.join();
	th2.join();
	th3.join();
	th4.join();
	th5.join();

	std::cin.get();

	return 0;
}

