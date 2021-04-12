// ConsoleApplication1.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include <iostream>
#include <thread>
#include <vector>
#include "wait_free_pointer_queue.h"
#include <stdint.h>
#include <assert.h>
#include <limits>


wait_free_pointer_queue			q(5);
std::atomic<int>				count;

void func()
{
	int64_t i(600);

	int64_t arr[] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 };

	while (i--)
	{
		int64_t j = rand() % 100;

		if (j <= 30)
		{
			q.enqueue_range(reinterpret_cast<void**>(arr), sizeof(arr) / sizeof(void*));
			count += 10;
		}
		else if (j > 30 && j <= 60)
		{
			q.enqueue(reinterpret_cast<void*>(1));
			count++;
		}
		else if (j > 60 && j <= 90)
		{
			int64_t j(0);
			q.dequeue(reinterpret_cast<void**>(&j));
			count--;
		}
		else
		{
			int r = q.dequeue_range(reinterpret_cast<void**>(arr), sizeof(arr));
			count -= r;
		}
	}
}

void write_test()
{
	int64_t i(1);

	uint32_t count(10000);

	while (count--)
	{
		int d = rand() % 100;

		if (i < 70)
		{
			assert(q.enqueue(reinterpret_cast<void*>(i)) == i - 1);
			i++;
		}
		else
		{

			int64_t arr[10] = {};
			for (int64_t& j : arr)
			{
				j = i++;
			}

			auto c = q.enqueue_range(reinterpret_cast<void**>(arr), 10);

			assert(c == i - 10 - 1);
		}
	}
}

void read_test()
{
	int64_t pre_v(-1);
	int64_t i(0);
	int64_t index(0);

	while (true)
	{
		int d = rand() % 100;

		if (d < 70)
		{
			if (q.dequeue(reinterpret_cast<void**>(&i), &index))
			{
				assert((i - 1) == index);
				assert(i > pre_v);
				pre_v = i;
				std::cout << i << " ";
			}
		}
		else
		{
			int64_t arr[10] = {};
			uint32_t count = q.dequeue_range(reinterpret_cast<void**>(arr), sizeof(arr), &index);
			if (count)
			{
				assert((arr[0] - 1) == index);
			}
			for (uint32_t j = 0; j < count; j++)
			{
				std::cout << arr[j] << " ";
				assert((arr[j]) > pre_v);
				pre_v = arr[j];
			}
		}
	}
}

int main()
{
	//std::thread th1(write_test);
	//std::thread th2(write_test);
	//std::thread th3(write_test);
	//std::thread th4(read_test);
	//std::thread th5(read_test);
	//std::thread th6(read_test);

	//th1.join();
	//th2.join();
	//th3.join();
	//th4.join();
	//th5.join();
	//th6.join();

	/*std::thread th1(write_test);
	std::thread th2(read_test);
	th1.join();
	th2.join();*/

	unsigned int i = std::numeric_limits<unsigned int>::max();

	auto c =  i % 22;
	auto d = (i + 1) % 22;


	return 0;
}
