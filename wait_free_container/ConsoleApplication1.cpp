#include <iostream>
#include <thread>
#include <vector>
#include "wait_free_vector.hpp"
#include <random>
#include <stdint.h>
#include <assert.h>
#include <limits>

wait_free_vector<int> vec(-1);
std::atomic<bool> b = true;


void func(int i) 
{
	std::uniform_int_distribution<int> d(0);
	std::default_random_engine re(i);

	while (b)
	{
		int r = d(re) % 100;
		if (r <= 25) 
		{
			vec.push_back(0);
			vec.push_back(0);
			vec.push_back(0);
		}
		else if (r > 25 && r <= 50) 
		{
			vec.remove(0);
		}
		else if (r > 50 && r <= 75) 
		{
			vec.remove( r % (vec.size() + 1));
		}
		else 
		{
			vec.get(r % (vec.size() + 1));
		}
	}
}



int main()
{
	std::thread th1(func, 0);
	std::thread th2(func, 1);
	std::thread th3(func, 2);

	std::this_thread::sleep_for(std::chrono::seconds(10));
	b = false;

	th1.join();
	th2.join();
	th3.join();
	

	return 0;
}

