#include "wait_free_pointer_buffer.h"
#include "mutex_check_template.hpp"
#include <windows.h>
#include <assert.h>

 void *const wait_free_pointer_buffer::INSERTING = reinterpret_cast<void*>(1);
 void *const wait_free_pointer_buffer::FREE = 0;

wait_free_pointer_buffer::wait_free_pointer_buffer(int64_t initial_capacity):
	m_data(nullptr),
	m_cur_pos(0),
	m_size(0),
	m_capacity(0),
	m_inserting(0),
	m_removing(0),
	m_setting(0),
	m_getting(0),
	m_resizing(0)
{
	m_data = new void* [initial_capacity] { INSERTING };
	assert(m_data);
	m_capacity = initial_capacity;
}

wait_free_pointer_buffer::~wait_free_pointer_buffer()
{
	delete []m_data;
}

int64_t wait_free_pointer_buffer::insert(void* elem)
{
	int64_t insert_pos(0);
	void* old_value(nullptr);

	mutex_check_weak(m_inserting, m_resizing);

	insert_pos = m_cur_pos.fetch_add(1);

	while (insert_pos >= m_capacity)
	{
		std::this_thread::yield();
	}

	while (InterlockedCompareExchange64(reinterpret_cast<volatile long long*>(&m_data[insert_pos]), reinterpret_cast<long long>(elem), reinterpret_cast<long long>(INSERTING)) != 0)
	{
		std::this_thread::yield();
	}
	
	m_size++;
	m_inserting--;

	if (m_inserting >= m_capacity - 1) 
	{
		resize(m_capacity * 1.5);
	}

	return insert_pos;
}

bool wait_free_pointer_buffer::set(int64_t index, void* elem)
{
	void* old_value(nullptr);

	mutex_check_weak(m_setting, m_resizing);
	if (index >= m_cur_pos)
	{
		m_setting--;
		return false;
	}

	do 
	{
		old_value = m_data[index];
		if (old_value != FREE) 
		{
			m_setting--;
			return false;
		}
	} while (InterlockedCompareExchange64(reinterpret_cast<volatile long long*>(m_data[index]), reinterpret_cast<long long>(elem), reinterpret_cast<long long>(old_value)) != reinterpret_cast<long long>(old_value));

	m_size++;
	m_setting--;

	return true;
}

bool wait_free_pointer_buffer::remove(int64_t index, void** value)
{
	void* old_value(nullptr);
	bool wait_for_inserting(false);

	mutex_check_weak(m_removing, m_resizing);

	if (index > m_cur_pos)
	{
		m_removing--;
		return false;
	}

	do 
	{
		do 
		{
			old_value = m_data[index];
			if (old_value == FREE)
			{
				m_removing--;
				return false;
			}

			wait_for_inserting = old_value == INSERTING;
			if (wait_for_inserting)
			{
				std::this_thread::yield();
			}
		} 
		while (wait_for_inserting);
	} 
	while (InterlockedCompareExchange64(reinterpret_cast<volatile long long *>(&m_data[index]), reinterpret_cast<long long>(FREE), reinterpret_cast<long long>(old_value)) != reinterpret_cast<long long> (old_value));

	*value = old_value;

	m_size--;
	m_removing--;

	return true;
}

bool wait_free_pointer_buffer::get(int64_t index, void** value)
{
	void* old_value(nullptr);
	bool wait_for_inserting(false);

	mutex_check_weak(m_getting, m_resizing);

	if (index > m_cur_pos)
	{
		m_getting--;
		return false;
	}

	do 
	{
		old_value = m_data[index];
		if (old_value == FREE) 
		{
			m_getting--;
			return false;
		}

		wait_for_inserting = old_value == INSERTING;
		if (wait_for_inserting) 
		{
			std::this_thread::yield();
		}
	} 
	while (wait_for_inserting);

	*value = old_value;
	m_getting--;

	return true;
}

size_t wait_free_pointer_buffer::cur_pos()
{
	return m_cur_pos;
}

size_t wait_free_pointer_buffer::size()
{
	return m_size;
}

size_t wait_free_pointer_buffer::capacity()
{
	return m_capacity;
}

void wait_free_pointer_buffer::resize(int64_t new_capacity)
{
	mutex_check_strong(m_resizing, m_inserting, m_removing, m_setting, m_getting);
	assert(m_size == m_capacity);
	assert(m_resizing == 1);

	void** new_data = new void* [new_capacity] { INSERTING };
	assert(new_data);

	::memcpy(new_data, m_data, m_capacity * sizeof(void*));
	delete m_data;
	m_data = new_data;
	m_capacity = new_capacity;

	m_resizing--;
}
