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
	while (runnning)
	{
		queue.enqueue(A());
	}
}

int main()
{
	std::thread th1(EnqueueFunction);
	std::thread th2(EnqueueFunction);
	std::thread th3(EnqueueFunction);
    
	clock_t t1 = ::clock();
	std::this_thread::sleep_for(std::chrono::seconds(1));
	clock_t t2 = ::clock();

	clock_t t3 = t2 = t1;

	runnning = false;

	th1.join();
	th2.join();
	th3.join();

	std::cin.get();

	return 0;
}

