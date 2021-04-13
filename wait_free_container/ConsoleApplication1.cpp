// ConsoleApplication1.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include "wait_free_buffer.hpp"
#include <atomic>

wait_free_buffer<int> buffer(-1, 0);

int main()
{
	int pos = buffer.insert(2);
	pos = buffer.insert(2);
	pos = buffer.insert(2);
	pos = buffer.insert(2);

	std::atomic<int> i;


	return 0;
}
