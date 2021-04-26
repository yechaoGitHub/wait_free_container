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
	A()
	{
		std::cout << "contrust A" << std::endl;
	}

	~A()
	{
		std::cout << "deconstruct A" << std::endl;
	}
};

wait_free_generic_queue<A> queue;

int main()
{
	queue.enqueue(A());
	queue.enqueue(A());
	queue.enqueue(A());
    

	return 0;
}

