#pragma once

#include <assert.h>

#include <algorithm>
#include <memory>

#include "mutex_check_template.hpp"
#include "wait_free_buffer.hpp"
#include "wait_free_queue.hpp"

enum class memory_pool_elem_state : int64_t
{ 
    free = 0, 
    valided, 
    out_of_range, 
};

//wait_free_memory_pool will stuck when itorator dosen't release the lock, beacuse the iterator stuck increase_capacity function
//implement a manual resize wait_free_buffer, when wait_free_buffer fulled, allocate return nullptr, meantime memory_pool call the increase_capacity
template<typename T, template <typename U> typename TAllocator = std::allocator>
class wait_free_memory_pool
{
	class iterator;

	static const int64_t  BUFFER_FREE = -1;
	static const int64_t  BUFFER_INSERTING = -2;
	static const int64_t  QUEUE_FREE = -1;

public:

    explicit wait_free_memory_pool(int64_t capacity = 10, const TAllocator<T>& allocator = TAllocator<T>()) :
        m_data(nullptr),
        m_capacity(0),
        m_elem_ref_count(0),
        m_capacity_changing(0),
        m_buffer(BUFFER_INSERTING, BUFFER_FREE, capacity),
        m_queue(QUEUE_FREE, capacity),
        m_allocator(allocator)
	{
		this->m_data = this->m_allocator.allocate(capacity);
		assert(this->m_data);
		this->m_capacity = capacity;
	}

	~wait_free_memory_pool()
	{
		this->m_allocator.deallocate(this->m_data, this->m_capacity);
	}

	iterator allocate()
	{
		int64_t offset(0);

		if (this->m_queue.dequeue(&offset))
		{
			return { this, offset };
		}
		else
		{
			int64_t cur_pos = this->m_buffer.push_back(0);
			if (cur_pos >= this->m_capacity)
			{
				increase_capacity(this->m_buffer.capacity());
			}

			return { this,  cur_pos };
		}
	}

	bool deallocate(const iterator& it) noexcept
	{
		int64_t offset = it.offset();

		if (this->m_buffer.remove(offset))
		{
			this->m_queue.enqueue(offset);

			return true;
		}
		else
		{
			return false;
		}
	}

	iterator get(int64_t index) noexcept
	{
		return { this, index };
	}

	const iterator get(int64_t index) const
	{
		return const_cast<wait_free_memory_pool*>(this)->get(index);
	}
	
	T* get_base() noexcept
	{
		return this->m_data;
	}

	const T* get_base() const noexcept
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

	size_t elem_count() const noexcept
	{
		return this->m_buffer.elem_count();
	}

	size_t capacity() const noexcept
	{
		return this->m_capacity;
	}

private:

	T*									m_data;
	std::atomic<int64_t>				m_capacity;
	mutable std::atomic<int64_t>		m_elem_ref_count;
	mutable std::atomic<int64_t>		m_capacity_changing;

	mutable wait_free_buffer<int64_t>	m_buffer;
	wait_free_queue<int64_t>			m_queue;
	TAllocator<T>						m_allocator;

    int64_t increase_ref_count() const noexcept
	{
        return mutex_check_weak(this->m_elem_ref_count, this->m_capacity_changing);
	}

	void increase_capacity(int64_t new_capacity)
	{
		mutex_check_cas_lock_strong(this->m_capacity_changing, this->m_elem_ref_count);
		
		if (new_capacity < this->m_capacity) 
		{
			this->m_capacity_changing = false;
			return;
		}
		
		T* new_data = this->m_allocator.allocate(new_capacity);
		assert(new_data);
        ::memcpy(new_data, this->m_data, this->m_capacity * sizeof(T));

        this->m_allocator.deallocate(this->m_data, this->m_capacity);
        this->m_data = new_data;

		this->m_capacity = new_capacity;

		this->m_capacity_changing = false;
	}

	int64_t decrease_ref_count(int64_t count = 1) const noexcept
	{
		int64_t old_count(0);
		int64_t new_count(0);
		do 
		{
			old_count = this->m_elem_ref_count;
			new_count = std::max(old_count - count, 0ll);
		} 
		while (!this->m_elem_ref_count.compare_exchange_strong(old_count, new_count));

		return old_count;
	}

	int64_t ref_count() const noexcept
	{
		return this->m_elem_ref_count;
	}

    memory_pool_elem_state get_elem_state(int64_t index) 
    {
        auto state = this->m_buffer.elem_state(index);
        switch (state)
        {
        case wait_free_elem_state::free:
        case wait_free_elem_state::inserting:
            return memory_pool_elem_state::free;

        case wait_free_elem_state::vailded:
            return memory_pool_elem_state::valided;

        case wait_free_elem_state::unallocated:
            return memory_pool_elem_state::out_of_range;

        default:
            assert(0);
        }
    }

public:

	class iterator
	{
		friend class wait_free_memory_pool<T>;

	public:

        iterator(const iterator&) = delete;
        
        iterator& operator= (const iterator&) = delete;

        explicit iterator(iterator&& rhd) noexcept :
            iterator(rhd.m_mempry_pool, rhd.m_offset)
        {
            this->m_lock_count = rhd.m_lock_count;

            rhd.m_lock_count = 0;
            rhd.m_offset = -1;
        }

        iterator& operator=(iterator&& rhd) noexcept
        {
            this->~iterator();

            this->m_mempry_pool = rhd.m_mempry_pool;
            this->m_offset = rhd.m_offset;
            this->m_lock_count.store(rhd.m_lock_count);

            rhd.m_lock_count = 0;
            rhd.m_offset = -1;

            return *this;
        }

		~iterator() noexcept
		{
            this->m_offset = -1;
			this->m_mempry_pool->decrease_ref_count(m_lock_count);
            m_mempry_pool = nullptr;
		}

		T* lock() noexcept
		{
            if (this->m_mempry_pool == nullptr || 
                this->m_offset < 0) 
            {
                return nullptr;
            }

            memory_pool_elem_state state = this->m_mempry_pool->get_elem_state(this->m_offset);
            switch (state)
            {
            case memory_pool_elem_state::free:
            case memory_pool_elem_state::valided:				
                this->m_lock_count++;
		        this->m_mempry_pool->increase_ref_count();
				return this->m_mempry_pool->get_base() + this->m_offset;
               
            case memory_pool_elem_state::out_of_range:
                return nullptr;

            default:
                assert(0);
            }
		}

		const T* lock() const noexcept
		{
			return const_cast<iterator*>(this)->lock();
		}

		void unlock() const noexcept
		{
            assert(m_mempry_pool != nullptr);

			int64_t old_count{};
			int64_t new_count{};
			
			do 
			{
				old_count = this->m_lock_count;
				new_count = std::max(old_count - 1, 0);
			}
			while (!this->m_lock_count->compare_exchange_strong(old_count, new_count));

			if (old_count < new_count) 
			{
				this->m_mempry_pool->decrease_ref_count();
			}
		}

		size_t offset() const noexcept
		{
			return this->m_offset;
		}

        memory_pool_elem_state state() const noexcept
        {           
            assert(this->m_mempry_pool != nullptr);
            return this->m_mempry_pool->get_elem_state(this->m_offset);
        }

        bool valid() const noexcept
        {
            return this->m_mempry_pool && this->m_offset > 0;
        }

        operator bool() const noexcept
        {
            return valid();
        }

        bool operator == (const iterator &rhd) const noexcept
        {
            return (this->m_mempry_pool == rhd.m_mempry_pool) && (this->m_offset == rhd.m_offset);
        }

        bool operator != (const iterator &rhd) const noexcept
        {
            return !(*this == rhd);
        }

	private:

		iterator(wait_free_memory_pool<T> *mempry_pool, int64_t offset) noexcept :
			m_mempry_pool(mempry_pool),
			m_lock_count(0),
			m_offset(offset)
		{
		}

		wait_free_memory_pool*			m_mempry_pool;
		mutable std::atomic<int64_t>	m_lock_count;
		int64_t					        m_offset;
	};
};

