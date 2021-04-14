// ConsoleApplication1.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include "wait_free_buffer.hpp"
#include <atomic>

wait_free_buffer<int> buffer(-1, 0);

int main()
{
	int pos = buffer.push_back(2);
	pos = buffer.push_back(2);
	pos = buffer.push_back(2);
	pos = buffer.push_back(2);

	int a(0), b(0), c(0);
	bool d = buffer.remove(0, &a);
	d = buffer.remove(1, &b);
	d = buffer.remove(2, &c);

	d = buffer.remove(0);
	d = buffer.remove(4);

	size_t s = buffer.size();
	s = buffer.cur_pos();
	s = buffer.capacity();

	d = buffer.insert(0, 3);
	d = buffer.store(0, 10);

	int t;
	d = buffer.load(0, t);


	return 0;
}
