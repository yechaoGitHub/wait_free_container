#pragma once
#include "mutex_check_template.hpp"
#include "wait_free_buffer.hpp"
#include "wait_free_queue.hpp"

#include <assert.h>

#include <algorithm>
#include <memory>

template<typename T, typename TAllocator = std::allocator<T>>
class wait_free_memory_pool
{
	template<T>
	class iterator;

	static const int64_t  BUFFER_FREE = -1;
	static const int64_t  BUFFER_INSERTING = -2;
	static const int64_t  QUEUE_FREE = -1;

public:
	wait_free_memory_pool(int64_t capacity = 10, const TAllocator& allocator = TAllocator()) :
		m_data(nullptr),
		m_capacity(0),
		m_elem_ref_count(0),
		m_resizing(0),
		m_getting_base(0),
		m_buffer(BUFFER_INSERTING, BUFFER_FREE, capacity, allocator),
		m_queue(QUEUE_FREE, capacity, allocator),
		m_allocator(allocator)
	{
		m_data = m_allocator.allocate(capacity);
		std::fill_n(m_data, capacity, T());
		assert(m_data);
		m_capacity = capacity;
	}

	~wait_free_memory_pool()
	{
		std::for_each(m_data, m_data + m_capacity, 
			[=](const T& elem) 
			{
				elem.~T();
			});

		m_allocator.allocate(m_data, m_capacity);
	}

	iterator<T> allocate()
	{
		int64_t offset(0);

		if (m_queue.dequeue(&offset))
		{
			m_allocating--;
			return {*this,  offset };
		}
		else
		{
			int64_t cur_pos = m_buffer.insert(0);
			if (cur_pos >= m_capacity)
			{
				resize(m_capacity * 1.5);
			}

			return { *this,  cur_pos };
		}
	}

	bool free(const iterator<T>& it) 
	{
		return free(it.offset());
	}

	bool free(int64_t offset)
	{
		if (m_buffer.remove(offset))
		{
			m_queue.enqueue(offset);

			m_freeing--;
			return true;
		}
		else
		{
			assert(0);
			m_freeing--;
			return false;
		}
	}

	iterator<T> get(int64_t offset) 
	{
		int64_t ref_count(0);
		if (m_buffer.get(offset, ref_count) && 
			ref_count > 0) 
		{
			return { *this, offset };
		}
		else 
		{
			return { *this, -1 };
		}
	}
	
	T* get_base() 
	{
		return m_data;
	}

	const T* get_base() const 
	{
		return m_data;
	}

	size_t size() const
	{
		return m_buffer.size();
	}

	size_t capacity() const
	{
		return m_buffer.capacity();
	}

private:
	T*							m_data;
	std::atomic<int64_t>		m_capacity;
	std::atomic<int64_t>		m_elem_ref_count;
	std::atomic<int64_t>		m_resizing;
	std::atomic<int64_t>		m_getting_base;
	
	wait_free_buffer<int64_t>	m_buffer;
	wait_free_queue<int64_t>	m_queue;
	TAllocator					m_allocator;

	void resize(int64_t new_capacity)
	{
		/*int64_t old_value = mutex_check_strong(m_resizing, m_allocating, m_freeing, m_setting);
		if (old_value != 0)
		{
			m_resizing--;
			return;
		}

		m_resizing--;*/
	}

	void increase_elem_ref_count() 
	{
		//m_buffer.get()

	}

	void decrease_elem_ref_count() 
	{

	}

public:
	template<T>
	class iterator
	{
		friend class wait_free_memory_pool;

	public:
		~iterator()
		{
		}

		T* lock() 
		{

		}

		const T* lock() const 
		{

		}

		void unlock() const 
		{

		}

		void free() 
		{

		}

		size_t offset() const
		{
			return m_offset;
		}

	private:
		iterator(wait_free_memory_pool<T>& mempry_pool, uint64_t offset) :
			m_mempry_pool(mempry_pool),
			m_offset(offset),
			m_released(false)
		{
			m_mempry_pool.increase_memory_ref_count();
		}

		wait_free_memory_pool&		m_mempry_pool;
		const int64_t				m_offset;
	};

};


