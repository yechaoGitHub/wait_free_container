#pragma once
#include "mutex_check_template.hpp"
#include "wait_free_buffer.hpp"
#include "wait_free_queue.hpp"

#include <assert.h>

#include <algorithm>
#include <memory>

template<typename T, template <typename U> typename TAllocator = std::allocator>
class wait_free_memory_pool
{
	template<typename>
	class iterator;

	static const int64_t  BUFFER_FREE = -1;
	static const int64_t  BUFFER_INSERTING = -2;
	static const int64_t  QUEUE_FREE = -1;

public:
	wait_free_memory_pool(int64_t capacity = 10, const TAllocator<T>& allocator = TAllocator<T>()) :
		m_data(nullptr),
		m_capacity(0),
		m_elem_ref_count(0),
		m_resizing(0),
		m_getting_base(0),
		m_buffer(BUFFER_INSERTING, BUFFER_FREE, capacity, allocator),
		m_queue(QUEUE_FREE, capacity, allocator),
		m_allocator(allocator)
	{
		this->m_data = this->m_allocator.allocate(capacity);
		std::fill_n(this->m_data, capacity, T());
		assert(this->m_data);
		this->m_capacity = capacity;
	}

	~wait_free_memory_pool()
	{
		std::for_each(this->m_data, this->m_data + this->m_capacity,
			[=](T& elem)
		{
			elem.~T();
		});

		this->m_allocator.allocate(this->m_data, this->m_capacity);
	}

	iterator<T> allocate()
	{
		int64_t offset(0);

		if (this->m_queue.dequeue(&offset))
		{
			this->m_allocating--;
			return { *this, offset };
		}
		else
		{
			int64_t cur_pos = this->m_buffer.insert(0);
			if (cur_pos >= this->m_capacity)
			{
				resize(this->m_capacity * 1.5);
			}

			return { *this,  cur_pos };
		}
	}

	bool free(const iterator<T>& it)
	{
		int64_t offset = it.offset();

		if (this->m_buffer.remove(offset))
		{
			this->m_queue.enqueue(offset);

			this->m_freeing--;
			return true;
		}
		else
		{
			assert(0);
			this->m_freeing--;
			return false;
		}
	}

	iterator<T> get(int64_t index) 
	{

	}

	iterator<T> get(int64_t offset) 
	{
		int64_t ref_count(0);
		if (this->m_buffer.get(offset, ref_count) &&
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
		return this->m_data;
	}

	const T* get_base() const 
	{
		return this->m_data;
	}

	size_t size() const
	{
		return this->m_buffer.size();
	}

	size_t capacity() const
	{
		return this->m_buffer.capacity();
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
		
	}

	int64_t increase_elem_ref_count(int64_t index) 
	{
		int64_t ret(0);
		if (m_buffer.fetch_add(index, 1, ret)) 
		{
			return ret;
		}
		else 
		{
			return -1;
		}
	}

	int64_t decrease_elem_ref_count(int64_t index)
	{
		int64_t old_count(0);
		int64_t new_count(0);
		bool exchanged(false);

		do
		{
			if (m_buffer.load(index, old_count))
			{
				new_count = std::max(old_count - 1, 0);
			}
			else 
			{
				return -1;
			}

			if (!m_buffer.compare_and_exchange_strong(index, exchanged, old_count, new_count)) 
			{
				return -1;
			}

		} while (!exchanged);

		return old_count;
	}

	int64_t increase_ref_count() 
	{
		return this->m_elem_ref_count++;
	}

	int64_t decrease_ref_count()
	{
		int64_t old_count(0);
		int64_t new_count(0);
		do 
		{
			old_count = this->m_elem_ref_count;
			new_count = std::max(old_count - 1, 0);
		} 
		while (!this->m_elem_ref_count.compare_exchange_strong(old_count, new_count));

		return old_count;
	}

	int64_t ref_count() const 
	{
		return this->m_elem_ref_count;
	}

public:
	template<T>
	class iterator
	{
		friend class wait_free_memory_pool<T>;

	public:
		~iterator()
		{
		}

		T* lock() 
		{

		}

		const T* lock() const 
		{
			this->m_mempry_pool.increase_ref_count();
		}

		void unlock() const 
		{

		}

		size_t offset() const
		{
			return this->m_offset;
		}

	private:
		iterator(wait_free_memory_pool<T>& mempry_pool, uint64_t offset) :
			m_mempry_pool(mempry_pool),
			m_lock_count(0),
			m_offset(offset)
		{
			this->m_mempry_pool.increase_memory_ref_count();
		}

		wait_free_memory_pool&		m_mempry_pool;
		std::atomic<int64_t>		m_lock_count;
		const int64_t				m_offset;
	};

};


