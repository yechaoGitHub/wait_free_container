// ConsoleApplication1.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include "wait_free_buffer.hpp"
#include <thread>

wait_free_buffer<int> buffer(-1, 0, 3);
std::atomic<int> i(1);
std::atomic<bool> b(true);

void increase_func() 
{
	while (b)
	{
		buffer.push_back(i++);
	}
}

void remove_func() 
{
	while (b)
	{
		size_t s = buffer.elem_count();
		if (s != 0) 
		{
			buffer.clear();
		}

		if (s == 0 && !b) 
		{
			break;
		}
	}
}


int main()
{
	std::thread r1(remove_func);
	std::thread r2(remove_func);
	std::thread r3(remove_func);
	std::thread i1(increase_func);
	std::thread i2(increase_func);
	std::thread i3(increase_func);
	std::thread i4(increase_func);

	std::this_thread::sleep_for(std::chrono::seconds(1));

	b = false;

	r1.join();
	r2.join();
	r3.join();

	i1.join();
	i2.join();
	i3.join();
	i4.join();

	

	return 0;
}
