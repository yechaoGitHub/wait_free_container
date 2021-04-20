#pragma once

#include <assert.h>

#include <algorithm>
#include <memory>

#include "mutex_check_template.hpp"
#include "wait_free_buffer.hpp"
#include "wait_free_queue.hpp"

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
		assert(this->m_data);
		std::fill_n(this->m_data, capacity, T());
		this->m_capacity = capacity;
	}

	~wait_free_memory_pool()
	{
		std::for_each_n(this->m_data, this->m_capacity,
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
				increase_capacity(this->m_buffer.capacity());
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
		return { *this, index };
	}

	const iterator<T> get(int64_t index) const
	{
		return const_cast<wait_free_memory_pool*>(this)->get(index);
	}
	
	T* get_base()
	{
		return this->m_data;
	}

	const T* get_base() const 
	{
		return const_cast<wait_free_memory_pool*>(this)->get_base();
	}

	void resize(int64_t new_size)
	{
		if (new_size > this->m_capacity)
		{
			increase_capacity(new_size);
		}

		this->m_buffer.resize(new_size);
	}

	size_t elem_count() const
	{
		return this->m_buffer.elem_count();
	}

	size_t capacity() const
	{
		return this->m_capacity;
	}

private:
	T*									m_data;
	std::atomic<int64_t>				m_capacity;
	mutable std::atomic<int64_t>		m_elem_ref_count;
	mutable std::atomic<int64_t>		m_resizing;
	mutable std::atomic<int64_t>		m_capacity_changing;

	mutable wait_free_buffer<int64_t>	m_buffer;
	wait_free_queue<int64_t>			m_queue;
	TAllocator<T>						m_allocator;

	void wait_for_capacity_changing() const
	{
		while (this->m_capacity_changing)
		{
			std::this_thread::yield();
		}
	}

	void increase_capacity(int64_t new_capacity)
	{
		mutex_check_cas_lock_strong(m_capacity_changing, m_elem_ref_count);
		
		if (new_capacity < m_capacity) 
		{
			m_capacity_changing = false;
			return;
		}
		
		T* new_data = m_allocator.allocate(new_capacity);
		assert(new_data);
		std::copy_n(m_data, m_capacity, new_data);
		m_capacity = new_capacity;

		m_capacity_changing = false;
	}

	int64_t increase_ref_count(int64_t count = 1) const
	{
		return this->m_elem_ref_count.fetch_add(count);
	}

	int64_t decrease_ref_count(int64_t count = 1) const
	{
		int64_t old_count(0);
		int64_t new_count(0);
		do 
		{
			old_count = this->m_elem_ref_count;
			new_count = std::max(old_count - count, 0);
		} 
		while (!this->m_elem_ref_count.compare_exchange_strong(old_count, new_count));

		return old_count;
	}

	int64_t ref_count() const 
	{
		return this->m_elem_ref_count;
	}

public:
	class iterator
	{
		friend class wait_free_memory_pool<T>;

	public:
		~iterator()
		{
			this->m_mempry_pool.decrease_ref_count(m_lock_count);
		}

		//<< to do add correct logic to this function >>
		T* lock() 
		{
			if (this->m_offset == -1) 
			{
				return nullptr;
			}
			else 
			{
				m_lock_count++;
				this->m_mempry_pool.increase_ref_count();
				this->m_mempry_pool.wait_for_capacity_changing();
				return this->m_mempry_pool.get_base() + this->m_offset;
			}
		}

		const T* lock() const 
		{
			return const_cast<iterator*>(this)->lock();
		}

		void unlock() const 
		{
			int64_t old_count{};
			int64_t new_count{};
			
			do 
			{
				old_count = m_lock_count;
				new_count = std::max(old_count - 1, 0);
			}
			while (!m_lock_count.compare_exchange_strong(old_count, new_count));

			if (old_count < new_count) 
			{
				this->m_mempry_pool.decrease_ref_count();
			}
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
		}

		wait_free_memory_pool&			m_mempry_pool;
		mutable std::atomic<int64_t>	m_lock_count;
		const int64_t					m_offset;
	};
};

