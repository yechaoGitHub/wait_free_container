#pragma once

#include <assert.h>

#include <algorithm>
#include <atomic>
#include <stdint.h>
#include <type_traits>

#include "template_util.hpp"

template<typename T, template<typename U> typename TAllocator = std::allocator>
class wait_free_queue
{
public:
    explicit wait_free_queue(const T &free_value, int64_t capacity = 10, const TAllocator<std::atomic<T>>& allocator = TAllocator<std::atomic<T>>()) :
		m_data(nullptr),
		m_allocator(allocator),
		m_free_value(free_value),
		m_enqueue_count(0),
		m_dequeue_count(0),
		m_size(0),
		m_capacity(0),
		m_enqueuing(0),
		m_dequeuing(0),
		m_reszing(0),
		m_stuck_enqueue(0),
		m_offset(0)
	{
		assert(capacity > 0);

		this->m_data = this->m_allocator.allocate(capacity);
		assert(m_data);
		std::for_each(this->m_data, this->m_data + capacity,
		[=](std::atomic<T> &elem) 
		{
			elem.store(this->m_free_value);
		});

		m_capacity = capacity;
	}

	// to fix... add lock unlock function
	~wait_free_queue() 
	{
		std::for_each(this->m_data, this->m_data + this->m_capacity,
		[=](std::atomic<T>& elem)
		{
			elem.~atomic<T>();
		});

		this->m_allocator.deallocate(this->m_data, this->m_capacity);
	}

	int64_t enqueue(const T& value) 
	{
		int64_t old_size(0);
		int64_t new_size(0);
		int64_t old_count(0);
		int64_t en_pos(0);
		T old_value();
		bool full(false);
		bool size_failed(false);

		do
		{
			do
			{
				mutex_check_weak(this->m_enqueuing, this->m_reszing, this->m_stuck_enqueue);

				old_size = this->m_size;
				new_size = (std::min)(old_size + 1, static_cast<int64_t>(this->m_capacity));
				full = new_size <= old_size;
				if (full)
				{
					this->m_enqueuing--;
					std::this_thread::yield();
				}
			}
			while (full);

			size_failed = !this->m_size.compare_exchange_strong(old_size, new_size);
			if (size_failed)
			{
				this->m_enqueuing--;
				std::this_thread::yield();
			}
		}
		while (size_failed);

		do
		{
			old_count = this->m_enqueue_count;
			en_pos = (old_count + m_offset) % this->m_capacity;
		} while (!this->m_enqueue_count.compare_exchange_strong(old_count, old_count + 1));

		T free_value(m_free_value);
		while (!m_data[en_pos].compare_exchange_strong(free_value, value))
		{	
			free_value = this->m_free_value;
			std::this_thread::yield();
		} 

		m_enqueuing--;

		if (new_size == this->m_capacity)
		{
			resize(new_size * 1.5);
		}

		return old_count;
	}

    template<typename TIterator>
	int64_t enqueue_range(TIterator it_start, const TIterator& it_end)
	{
		int64_t old_size(0);
		int64_t new_size(0);
		int64_t old_count(0);
		int64_t en_pos(0);
		int64_t fill_count(0);
		int64_t remain_count(0);
		bool full(false);
		bool size_failed(false);
		bool resized(false);

        auto count = it_end - it_start;

		do
		{
			do
			{
				mutex_check_weak(this->m_enqueuing, this->m_reszing, this->m_stuck_enqueue);

				old_size = m_size;
				new_size = (std::min)(old_size + static_cast<int64_t>(count), static_cast<int64_t>(this->m_capacity));
				full = new_size <= old_size;
				if (full)
				{
					this->m_enqueuing--;
					std::this_thread::yield();
				}
			} while (full);

			size_failed = !this->m_size.compare_exchange_strong(old_size, new_size);
			if (size_failed)
			{
				this->m_enqueuing--;
				std::this_thread::yield();
			}

		} while (size_failed);

		if (new_size == this->m_capacity)
		{
			stuck_enqueue();
			resized = true;
		}

		fill_count = new_size - old_size;
		do
		{
			old_count = this->m_enqueue_count;
			en_pos = (old_count + m_offset) % this->m_capacity;
		} 
		while (!this->m_enqueue_count.compare_exchange_strong(old_count, old_count + fill_count));

		T free_value(this->m_free_value);
		for (int64_t i = 0; i < fill_count; i++)
		{
            T& value = *(it_start + i);

			while (!this->m_data[en_pos].compare_exchange_strong(free_value, value))
			{
				free_value = this->m_free_value;
				std::this_thread::yield();
			}

			en_pos = (en_pos + 1) % this->m_capacity;
		}

		//??????????elem // bug
		remain_count = count - fill_count;
		if (resized)
		{

			resize((new_size + remain_count) * 1.5, it_start + fill_count, it_end);
			this->m_stuck_enqueue = 0;
		}
		else
		{
			this->m_enqueuing--;
		}

		return old_count;
	}

    int64_t dequeue(T& elem) noexcept
	{
		int64_t old_size(0);
		int64_t new_size(0);
		int64_t old_count(0);
		int64_t de_pos(0);
        T old_value{};

		if (this->m_size <= 0)
		{
			return -1;
		}

		mutex_check_weak(this->m_dequeuing, this->m_reszing);

		do
		{
			old_size = this->m_size;
			new_size = (std::max)(old_size - 1, 0ll);
			if (new_size >= old_size)
			{
				this->m_dequeuing--;
				return -1;
			}
		} 
		while (!this->m_size.compare_exchange_strong(old_size, new_size));

		do
		{
			old_count = this->m_dequeue_count;
			de_pos = (old_count + this->m_offset) % this->m_capacity;
		} 
		while (!this->m_dequeue_count.compare_exchange_strong(old_count, old_count + 1));

		while (true)
		{
			old_value = this->m_data[de_pos];
			if (old_value == this->m_free_value)
			{
				std::this_thread::yield();
				continue;
			}
			else
			{
				if (this->m_data[de_pos].compare_exchange_strong(old_value, m_free_value))
				{
                    elem = old_value;
					break;
				}
			}
		}

		m_dequeuing--;

		return old_count;
	}

    int64_t dequeue() noexcept
    {
        int64_t old_size(0);
        int64_t new_size(0);
        int64_t old_count(0);
        int64_t de_pos(0);
        T old_value{};

        if (this->m_size <= 0)
        {
            return -1;
        }

        mutex_check_weak(this->m_dequeuing, this->m_reszing);

        do
        {
            old_size = this->m_size;
            new_size = (std::max)(old_size - 1, 0ll);
            if (new_size >= old_size)
            {
                this->m_dequeuing--;
                return -1;
            }
        } while (!this->m_size.compare_exchange_strong(old_size, new_size));

        do
        {
            old_count = this->m_dequeue_count;
            de_pos = (old_count + this->m_offset) % this->m_capacity;
        } while (!this->m_dequeue_count.compare_exchange_strong(old_count, old_count + 1));

        while (true)
        {
            old_value = this->m_data[de_pos];
            if (old_value == this->m_free_value)
            {
                std::this_thread::yield();
                continue;
            }
            else
            {
                if (this->m_data[de_pos].compare_exchange_strong(old_value, m_free_value))
                {
                    break;
                }
            }
        }

        m_dequeuing--;

        return old_count;
    }

    template<typename TIterator>
	int64_t dequeue_range(TIterator& start_it, const TIterator& end_it) noexcept
	{
		int64_t count(end_it - start_it);
		int64_t old_size(0);
		int64_t new_size(0);
		int64_t old_count(0);
		int64_t de_pos(0);
		T old_value{};

		if (this->m_size <= 0 || count <= 0)
		{
			return -1;
		}

		mutex_check_weak(this->m_dequeuing, this->m_reszing);

		do
		{
			old_size = this->m_size;
			new_size = (std::max)(old_size - count, 0ll);
			if (new_size >= old_size)
			{
				this->m_dequeuing--;
				return -1;
			}
		}
        while (!this->m_size.compare_exchange_strong(old_size, new_size));

		count = old_size - new_size;

		do
		{
			old_count = this->m_dequeue_count;
			de_pos = (old_count + this->m_offset) % this->m_capacity;
		}
		while (!this->m_dequeue_count.compare_exchange_strong(old_count, old_count + count));

		for (int64_t i = 0; i < count; i++, start_it++)
		{
			while (true)
			{
				old_value = this->m_data[de_pos];
				if (old_value == this->m_free_value)
				{
					std::this_thread::yield();
					continue;
				}
				else
				{
					if (this->m_data[de_pos].compare_exchange_strong(old_value, this->m_free_value))
					{
                        *start_it = old_value;
						break;
					}
				}
			}

			de_pos = (de_pos + 1) % this->m_capacity;
		}

		this->m_dequeuing--;

		return old_count;
	}

    int64_t dequeue_range(int64_t &count) noexcept
    {
        int64_t old_size(0);
        int64_t new_size(0);
        int64_t old_count(0);
        int64_t de_pos(0);
        T old_value(nullptr);

        if (this->m_size <= 0 || count <= 0)
        {
            return -1;
        }

        mutex_check_weak(this->m_dequeuing, this->m_reszing);

        do
        {
            old_size = this->m_size;
            new_size = (std::max)(old_size - count, 0ll);
            if (new_size >= old_size)
            {
                this->m_dequeuing--;
                return -1;
            }
        } while (!this->m_size.compare_exchange_strong(old_size, new_size));

        count = old_size - new_size;

        do
        {
            old_count = this->m_dequeue_count;
            de_pos = (old_count + this->m_offset) % this->m_capacity;
        } while (!this->m_dequeue_count.compare_exchange_strong(old_count, old_count + count));

        for (int64_t i = 0; i < count; i++)
        {
            while (true)
            {
                old_value = this->m_data[de_pos];
                if (old_value == this->m_free_value)
                {
                    std::this_thread::yield();
                    continue;
                }
                else
                {
                    if (this->m_data[de_pos].compare_exchange_strong(old_value, this->m_free_value))
                    {
                        break;
                    }
                }
            }

            de_pos = (de_pos + 1) % this->m_capacity;
        }

        this->m_dequeuing--;

        return old_count;
    }

	size_t size() const noexcept
	{
		return this->m_size;
	}

	size_t capacity() const noexcept
	{
		return this->m_capacity;
	}

private:
	std::atomic<T>*					m_data;
	TAllocator<std::atomic<T>>		m_allocator;
	const T							m_free_value;
	std::atomic<int64_t>			m_enqueue_count;
	std::atomic<int64_t>			m_dequeue_count;
	std::atomic<int64_t>			m_size;
	std::atomic<int64_t>			m_capacity;

    mutable std::atomic<int64_t>	m_enqueuing;
    mutable std::atomic<int64_t>	m_dequeuing;
    mutable std::atomic<int64_t>	m_reszing;
    mutable std::atomic<int64_t>	m_stuck_enqueue;
	std::atomic<int64_t>			m_offset;

	int64_t resize(int64_t new_capacity) 
	{
		//??????????????????????????????????????resize??????????????????????????????????????resize
		int64_t old_value = mutex_check_strong(this->m_reszing, this->m_enqueuing, this->m_dequeuing);
		if (old_value != 0)
		{
			this->m_reszing--;
			return 0;
		}

		std::atomic<T>* new_data = this->m_allocator.allocate(new_capacity);
		assert(new_data);
		std::for_each(new_data, new_data + new_capacity, 
		[=](std::atomic<T> &elem) 
		{
			elem.store(this->m_free_value);
		});

		int64_t head_pos((this->m_dequeue_count + this->m_offset) % this->m_capacity);
		int64_t tail_pos((this->m_enqueue_count + this->m_offset) % this->m_capacity);

		for (int64_t i = 0; i < this->m_size; i++)
		{
			new_data[i].store(this->m_data[head_pos]);
			head_pos = (head_pos + 1) % this->m_capacity;
		}

		assert(head_pos == tail_pos);

		this->m_allocator.deallocate(this->m_data, this->m_capacity);
		this->m_data = new_data;
		this->m_offset = new_capacity - (this->m_dequeue_count % new_capacity);
		this->m_capacity = new_capacity;

		this->m_reszing--;

		return new_capacity;
	}

    template<typename TIterator>
	int64_t resize(int64_t new_capacity, TIterator start_it, const TIterator &end_it)
	{
		//??????????????????????????????????????resize??????????????????????????????????????resize
		int64_t old_value = mutex_check_strong(this->m_reszing, this->m_enqueuing, this->m_dequeuing);
		if (old_value != 0)
		{
			this->m_reszing--;
			return 0;
		}

		std::atomic<T>* new_data = this->m_allocator.allocate(new_capacity);
		assert(new_data);
		std::for_each(new_data, new_data + new_capacity,
		[=](std::atomic<T>& elem)
		{
			elem.store(this->m_free_value);
		});

		int64_t head_pos((this->m_dequeue_count + this->m_offset) % this->m_capacity);
		int64_t tail_pos((this->m_enqueue_count + this->m_offset) % this->m_capacity);

		for (int64_t i = 0; i < this->m_size; i++)
		{
			new_data[i].store(this->m_data[head_pos]);
			head_pos = (head_pos + 1) % this->m_capacity;
		}
		assert(head_pos == tail_pos);
		this->m_offset = new_capacity - (this->m_dequeue_count % new_capacity);

		int64_t en_pos(0);
		auto size = end_it - start_it;
		for (int32_t i = 0; i < size; i++)
		{
			T& value = *(start_it + i);
			en_pos = (this->m_enqueue_count + this->m_offset) % new_capacity;
			new_data[en_pos] = value;
			this->m_enqueue_count++;

		}

		this->m_allocator.deallocate(this->m_data, this->m_capacity);
		this->m_data = new_data;
		this->m_size += size;
		this->m_capacity = new_capacity;
		this->m_reszing--;

		return new_capacity;
	}

	void stuck_enqueue() noexcept
	{
		while (this->m_stuck_enqueue.exchange(1))
		{
			std::this_thread::yield();
		}

		this->m_enqueuing--;
		while (this->m_enqueuing)
		{
			std::this_thread::yield();
		}
	}
};


