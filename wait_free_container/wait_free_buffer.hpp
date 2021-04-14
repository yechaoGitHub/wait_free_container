#pragma once

#include <assert.h>
#include <stdint.h>

#include <algorithm>
#include <atomic>
#include <thread>
#include <type_traits>
#include <memory>

#include "template_util.hpp"
#include "mutex_check_template.hpp"

//�����ڵ�Ԫ��elem�� ������value
template<typename T, template<typename U> typename TAllocator>
class wait_free_buffer_base 
{
	using base = wait_free_buffer_base;

public:
	wait_free_buffer_base(const T& inserting, const T& free, int64_t capacity = 10, const TAllocator<std::atomic<T>>& allocator = TAllocator<std::atomic<T>>()) :
		m_data(nullptr),
		m_allocator(allocator),
		m_inserting_value(inserting),
		m_free_value(free),
		m_cur_pos(0),
		m_size(0),
		m_capacity(0),
		m_inserting(0),
		m_removing(0),
		m_setting(0),
		m_getting(0),
		m_resizing(0)
	{

		this->m_data = this->m_allocator.allocate(capacity);
		assert(m_data);

		std::for_each(this->m_data, this->m_data + capacity,
		[=](std::atomic<T>& elem)
		{
			elem.store(base::m_inserting_value);
		});

		this->m_capacity = capacity;
	}

	~wait_free_buffer_base()
	{
		std::for_each(this->m_data, this->m_data + this->m_capacity,
		[=](std::atomic<T>& elem)
		{
			elem.~atomic<T>();
		});

		this->m_allocator.deallocate(this->m_data, this->m_capacity);
	}

	int64_t insert(const T& value)
	{
		assert(value != base::m_inserting_value && 
			value != base::m_free_value);

		int64_t insert_pos(0);
		T old_elem;

		mutex_check_weak(this->m_inserting, this->m_resizing);

		insert_pos = this->m_cur_pos.fetch_add(1);

		while (insert_pos >= this->m_capacity)
		{
			std::this_thread::yield();
		}

		old_elem = base::m_inserting_value;
		while (!this->m_data[insert_pos].compare_exchange_strong(old_elem, value))
		{
			old_elem = base::m_inserting_value;
			std::this_thread::yield();
		}

		this->m_size++;
		this->m_inserting--;

		if (insert_pos >= this->m_capacity - 1)
		{
			resize(this->m_capacity * 1.5);
		}

		return insert_pos;
	}

	bool remove(int64_t index, T* elem = nullptr)
	{
		void* old_elem(nullptr);
		bool wait_for_inserting(false);

		mutex_check_weak(this->m_removing, this->m_resizing);

		if (index > this->m_cur_pos)
		{
			this->m_removing--;
			return false;
		}

		do
		{
			do
			{
				old_elem = this->m_data[index];
				if (old_elem == base::m_free_value)
				{
					this->m_removing--;
					return false;
				}

				wait_for_inserting = old_elem == this->m_inserting_value;
				if (wait_for_inserting)
				{
					std::this_thread::yield();
				}
			} 
			while (wait_for_inserting);
		} 
		while (!this->m_data[index].compare_exchange_strong(old_elem, base::m_free_value));

		*elem = old_elem;

		this->m_size--;
		this->m_removing--;

		return true;
	}

	bool load(int64_t index, T& elem) const
	{
		T old_elem;
		bool wait_for_inserting(false);

		mutex_check_weak(this->m_getting, this->m_resizing);

		if (index > this->m_cur_pos)
		{
			this->m_getting--;
			return false;
		}

		do
		{
			old_elem = this->m_data[index];
			if (old_elem == base::m_free_value)
			{
				this->m_getting--;
				return false;
			}

			wait_for_inserting = old_elem == base::m_inserting_value;
			if (wait_for_inserting)
			{
				std::this_thread::yield();
			}
		} 
		while (wait_for_inserting);

		elem = old_elem;
		this->m_getting--;

		return true;
	}

	bool store(int64_t index, T value)
	{
		T old_elem;

		mutex_check_weak(this->m_setting, this->m_resizing);
		if (index >= this->m_cur_pos)
		{
			this->m_setting--;
			return false;
		}

		do
		{
			old_elem = this->m_data[index];
			if (old_elem == base::m_free_value)
			{
				this->m_setting--;
				return false;
			}
		} 
		while (!this->m_data[index].compare_exchange_strong(old_elem, value));

		this->m_size++;
		this->m_setting--;

		return true;
	}

	bool compare_and_exchange_strong(int64_t index, T& exchange, const T& compare)
	{
		T old_elem;

		mutex_check_weak(this->m_setting, this->m_resizing);
		if (index >= this->m_cur_pos)
		{
			this->m_setting--;
			return false;
		}

		do
		{
			old_elem = this->m_data[index];
			if (old_elem == base::m_free_value ||
				old_elem != compare)
			{
				exchange = old_elem;
				this->m_setting--;
				return false;
			}
		} 
		while (!this->m_data[index].compare_exchange_strong(old_elem, exchange));

		this->m_setting--;

		return true;
	}

	bool compare_and_exchange_weak(int64_t index, T& exchange, const T& compare)
	{
		T old_elem;

		mutex_check_weak(this->m_setting, this->m_resizing);
		if (index >= this->m_cur_pos)
		{
			this->m_setting--;
			return false;
		}

		do
		{
			old_elem = this->m_data[index];
			if (old_elem == base::m_free_value ||
				old_elem != compare)
			{
				exchange = old_elem;
				this->m_setting--;
				return false;
			}
		} 
		while (!this->m_data[index].compare_exchange_weak(old_elem, exchange));

		this->m_setting--;

		return true;
	}

	size_t cur_pos() const
	{
		return this->m_cur_pos;
	}

	size_t size() const
	{
		return this->m_size;
	}

	size_t capacity() const
	{
		return this->m_capacity;
	}

protected:
	std::atomic<T>*					m_data;
	TAllocator<std::atomic<T>>		m_allocator;
	const T							m_inserting_value;
	const T							m_free_value;
	std::atomic<int64_t>			m_cur_pos;
	std::atomic<int64_t>			m_size;
	std::atomic<int64_t>			m_capacity;
	std::atomic<int64_t>			m_inserting;
	std::atomic<int64_t>			m_removing;
	std::atomic<int64_t>			m_setting;
	std::atomic<int64_t>			m_getting;
	std::atomic<int64_t>			m_resizing;

	void resize(int64_t new_capacity) 
	{
		assert(this->m_size == this->m_capacity);
		int64_t old_resizing = mutex_check_strong(this->m_resizing, this->m_inserting, this->m_removing, this->m_setting, this->m_getting);
		if (old_resizing != 0) 
		{
			return;
		}

		std::atomic<T>* new_data = this->m_allocator.allocate(new_capacity);
		assert(new_data);
		std::fill_n(new_data, new_capacity, base::m_inserting_value);

		for (int64_t i =0; i < this->m_capacity; i++)
		{
			new_data[i].store(this->m_data[i]);
		}

		this->m_allocator.deallocate(this->m_data, this->m_capacity);
		this->m_data = new_data;
		this->m_capacity = new_capacity;

		this->m_resizing--;
	}
};

template<typename T, template<typename U> typename TAllocator>
class wait_free_buffer_object : public wait_free_buffer_base<T, TAllocator>
{
	using base = wait_free_buffer_base<T, TAllocator>;

public:
	wait_free_buffer_object(const T& inserting, const T& free, int64_t capacity = 10, const TAllocator<std::atomic<T>>& allocator = TAllocator<std::atomic<T>>()) :
		base(inserting, free, capacity, allocator)
	{
	}

	~wait_free_buffer_object() 
	{
	}
};

template<typename T, template<typename U> typename TAllocator>
class wait_free_buffer_integer : public wait_free_buffer_base<T, TAllocator>
{
	using base = wait_free_buffer_base<T, TAllocator>;

public:
	wait_free_buffer_integer(const T& inserting, const T& free, int64_t capacity = 10, const TAllocator<std::atomic<T>>& allocator = TAllocator<std::atomic<T>>()) :
		base(inserting, free, capacity, allocator)
	{
	}

	~wait_free_buffer_integer()
	{
	}
	
	bool fetch_add(int64_t index, T operand, T& result) 
	{
		T old_elem;
		T new_elem;

		mutex_check_weak(this->m_setting, this->m_resizing);
		if (index >= this->m_cur_pos)
		{
			this->m_setting--;
			return false;
		}

		do
		{
			old_elem = this->m_data[index];
			if (old_elem == base::m_free_value)
			{
				this->m_setting--;
				return false;
			}
			new_elem = old_elem + operand;
		} while (!this->m_data[index].compare_exchange_strong(old_elem, new_elem));

		result = old_elem;

		this->m_size++;
		this->m_setting--;

		return true;
	}

	bool fetch_and(int64_t index, T operand, T& result)
	{
		T old_elem;
		T new_elem;

		mutex_check_weak(this->m_setting, this->m_resizing);
		if (index >= this->m_cur_pos)
		{
			this->m_setting--;
			return false;
		}

		do
		{
			old_elem = this->m_data[index];
			if (old_elem == base::m_free_value)
			{
				this->m_setting--;
				return false;
			}
			new_elem = old_elem & operand;
		} while (!this->m_data[index].compare_exchange_strong(old_elem, new_elem));

		result = old_elem;

		this->m_size++;
		this->m_setting--;

		return true;
	}

	bool fetch_or(int64_t index, T operand, T& result) 
	{
		T old_elem;
		T new_elem;

		mutex_check_weak(this->m_setting, this->m_resizing);
		if (index >= this->m_cur_pos)
		{
			this->m_setting--;
			return false;
		}

		do
		{
			old_elem = this->m_data[index];
			if (old_elem == base::m_free_value)
			{
				this->m_setting--;
				return false;
			}
			new_elem = old_elem | operand;
		} while (!this->m_data[index].compare_exchange_strong(old_elem, new_elem));

		result = old_elem;

		this->m_size++;
		this->m_setting--;

		return true;
	}

	bool fetch_sub(int64_t index, T operand, T& result)
	{
		T old_elem;
		T new_elem;

		mutex_check_weak(this->m_setting, this->m_resizing);
		if (index >= this->m_cur_pos)
		{
			this->m_setting--;
			return false;
		}

		do
		{
			old_elem = this->m_data[index];
			if (old_elem == base::m_free_value)
			{
				this->m_setting--;
				return false;
			}
			new_elem = old_elem - operand;
		} 
		while (!this->m_data[index].compare_exchange_strong(old_elem, new_elem));

		result = old_elem;

		this->m_size++;
		this->m_setting--;

		return true;
	}

	bool fetch_xor(int64_t index, T operand, T& result)
	{
		T old_elem;
		T new_elem;

		mutex_check_weak(this->m_setting, this->m_resizing);
		if (index >= this->m_cur_pos)
		{
			this->m_setting--;
			return false;
		}

		do
		{
			old_elem = this->m_data[index];
			if (old_elem == base::m_free_value)
			{
				this->m_setting--;
				return false;
			}
			new_elem = old_elem ^ operand;
		} 
		while (!this->m_data[index].compare_exchange_strong(old_elem, new_elem));

		result = old_elem;

		this->m_size++;
		this->m_setting--;

		return true;
	}
};

template<typename T, template<typename U> typename TAllocator>
class wait_free_buffer_pointer : public wait_free_buffer_base<T, TAllocator>
{
	using base = wait_free_buffer_base<T, TAllocator>;

public:
	wait_free_buffer_pointer(const T& inserting, const T& free, int64_t capacity = 10, const TAllocator<std::atomic<T>>& allocator = TAllocator<std::atomic<T>>()) :
		base(inserting, free, capacity, allocator)
	{
	}

	~wait_free_buffer_pointer()
	{
	}

	bool fetch_add(int64_t index, T operand, T& result)
	{
		T old_elem;
		T new_elem;

		mutex_check_weak(this->m_setting, this->m_resizing);
		if (index >= this->m_cur_pos)
		{
			this->m_setting--;
			return false;
		}

		do
		{
			old_elem = this->m_data[index];
			if (old_elem == base::m_free_value)
			{
				this->m_setting--;
				return false;
			}
			new_elem = old_elem + operand;
		} while (!this->m_data[index].compare_exchange_strong(old_elem, new_elem));

		result = old_elem;

		this->m_size++;
		this->m_setting--;

		return true;
	}

	bool fetch_sub(int64_t index, T operand, T& result)
	{
		T old_elem;
		T new_elem;

		mutex_check_weak(this->m_setting, this->m_resizing);
		if (index >= this->m_cur_pos)
		{
			this->m_setting--;
			return false;
		}

		do
		{
			old_elem = this->m_data[index];
			if (old_elem == base::m_free_value)
			{
				this->m_setting--;
				return false;
			}
			new_elem = old_elem - operand;
		} 
		while (!this->m_data[index].compare_exchange_strong(old_elem, new_elem));

		result = old_elem;

		this->m_size++;
		this->m_setting--;

		return true;
	}
};

template<typename T, template<typename U> typename TAllocator>
using wait_free_buffer_base_t = typename select_type<std::is_integral_v<T> && !std::is_same_v<T, bool>, wait_free_buffer_integer<T, TAllocator>,
	typename select_type<std::is_pointer_v<T> && std::is_object_v<std::remove_pointer_t<T>>, wait_free_buffer_pointer<T, TAllocator>, wait_free_buffer_object<T, TAllocator>>::type>::type;

template<typename T, template<typename U> typename TAllocator = std::allocator>
class wait_free_buffer : public wait_free_buffer_base_t<T, TAllocator>
{
	using base = wait_free_buffer_base_t<T, TAllocator>;

public:
	wait_free_buffer(const T& inserting, const T& free, int64_t capacity = 10, const TAllocator<std::atomic<T>>& allocator = TAllocator<std::atomic<T>>()) :
		base(inserting, free, capacity, allocator)
	{
	}

	~wait_free_buffer() 
	{

	}
};
