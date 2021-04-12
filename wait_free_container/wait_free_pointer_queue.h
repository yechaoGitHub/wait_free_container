#pragma once
#include <stdint.h>
#include <atomic>

class wait_free_pointer_queue
{
public:
	wait_free_pointer_queue(int64_t capacity = 10);
	~wait_free_pointer_queue();

	int64_t enqueue(void* elem);
	int64_t enqueue_range(void** arr_elem, int64_t count);
	bool dequeue(void** elem, int64_t* index = nullptr);
	int64_t dequeue_range(void** elem, int64_t arr_size, int64_t* index = nullptr);
	size_t size();

private:
	void**					m_data;
	std::atomic<int64_t>	m_enqueue_count;
	std::atomic<int64_t>	m_dequeue_count;
	std::atomic<int64_t>	m_size;
	std::atomic<int64_t>	m_capacity;

	std::atomic<int64_t>	m_enqueuing;
	std::atomic<int64_t>	m_dequeuing;
	std::atomic<int64_t>	m_reszing;
	std::atomic<int64_t>	m_stuck_enqueue;
	std::atomic<int64_t>	m_offset;

	int64_t resize(int64_t new_capacity);
	int64_t resize(int64_t new_capacity, void** extra_copy, int64_t size);
	void stuck_enqueue();
};

