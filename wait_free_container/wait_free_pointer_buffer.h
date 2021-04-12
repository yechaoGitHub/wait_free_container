#pragma once
#include <atomic>
#include <stdint.h>

class wait_free_pointer_buffer 
{
	static void *const INSERTING;
	static void *const FREE;
	enum state { free = 0, inserting = 1,  };

public:
	wait_free_pointer_buffer(int64_t initial_capacity = 10);
	~wait_free_pointer_buffer();

	int64_t insert(void* elem);
	bool set(int64_t index, void* elem);
	bool remove(int64_t index, void** value);
	bool get(int64_t index, void **value);
	
	size_t cur_pos();
	size_t size();
	size_t capacity();
	
private:
	void**						m_data;
	std::atomic<int64_t>		m_cur_pos;
	std::atomic<int64_t>		m_size;
	std::atomic<int64_t>		m_capacity;
	std::atomic<int64_t>		m_inserting;
	std::atomic<int64_t>		m_removing;
	std::atomic<int64_t>		m_setting;
	std::atomic<int64_t>		m_getting;
	std::atomic<int64_t>		m_resizing;

	void resize(int64_t new_capacity);
};
