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

//容器内的元素elem， 容器外value

//_Select<is_pointer_v<_TVal>&& is_object_v<remove_pointer_t<_TVal>>>

template<typename T, typename TAllocator>
using wait_free_buffer_base = typename select_type<std::is_integral_v<T> && std::is_same_v<T, bool>, wait_free_buffer_integer<T, TAllocator>,
	typename select_type<std::is_pointer_v<T> && std::is_object_v<std::remove_pointer_t<T>>, wait_free_buffer_pointer<T, TAllocator>, wait_free_buffer_base<T, TAllocator>>::type>::type;

template<typename T, typename TAllocator>
class wait_free_buffer_base 
{
public:
	wait_free_buffer_base(const T& inserting, const T& free, int64_t capacity = 10, const TAllocator& allocator = TAllocator()) :
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
		m_data = m_allocator.allocate(initial_capacity);
		assert(m_data);
		std::fill_n(m_data, m_inserting, initial_capacity);
		m_capacity = initial_capacity;
	}

	~wait_free_buffer_base()
	{
		std::for_each(m_data, m_data + m_capacity,
		[](T& elem)
		{
			elem.~T();
		});

		m_allocator.deallocate(m_data, m_capacity);
	}

	int64_t insert(const T& value)
	{
		int64_t insert_pos(0);
		T old_elem;

		mutex_check_weak(m_inserting, m_resizing);

		insert_pos = m_cur_pos.fetch_add(1);

		while (insert_pos >= m_capacity)
		{
			std::this_thread::yield();
		}

		old_elem = m_inserting_value;
		while (!m_data[insert_pos].compare_exchange_strong(old_elem, value))
		{
			old_elem = m_inserting_value;
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

	bool remove(int64_t index, T* elem = nullptr)
	{
		void* old_elem(nullptr);
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
				old_elem = m_data[index];
				if (old_elem == m_free_value)
				{
					m_removing--;
					return false;
				}

				wait_for_inserting = old_elem == m_inserting_value;
				if (wait_for_inserting)
				{
					std::this_thread::yield();
				}
			} while (wait_for_inserting);
		} while (!m_data[index].compare_exchange_strong(old_elem, m_free_value));

		*elem = old_elem;

		m_size--;
		m_removing--;

		return true;
	}

	bool load(int64_t index, T& elem) const
	{
		T old_elem;
		bool wait_for_inserting(false);

		mutex_check_weak(m_getting, m_resizing);

		if (index > m_cur_pos)
		{
			m_getting--;
			return false;
		}

		do
		{
			old_elem = m_data[index];
			if (old_elem == m_free_value)
			{
				m_getting--;
				return false;
			}

			wait_for_inserting = old_value == m_inserting_value;
			if (wait_for_inserting)
			{
				std::this_thread::yield();
			}
		} while (wait_for_inserting);

		elem = old_elem;
		m_getting--;

		return true;
	}

	bool store(int64_t index, T value)
	{
		T old_elem;

		mutex_check_weak(m_setting, m_resizing);
		if (index >= m_cur_pos)
		{
			m_setting--;
			return false;
		}

		do
		{
			old_elem = m_data[index];
			if (old_elem == m_free_value)
			{
				m_setting--;
				return false;
			}
		} while (!m_data[index].compare_exchange_strong(old_elem, value));

		m_size++;
		m_setting--;

		return true;
	}

	bool compare_and_exchange_strong(int64_t index, T& exchange, const T& compare)
	{
		T old_elem;

		mutex_check_weak(m_setting, m_resizing);
		if (index >= m_cur_pos)
		{
			m_setting--;
			return false;
		}

		do
		{
			old_elem = m_data[index];
			if (old_elem == m_free_value ||
				old_elem != compare)
			{
				exchange = old_elem;
				m_setting--;
				return false;
			}
		} while (!m_data[index].compare_exchange_strong(old_elem, exchange));

		m_setting--;

		return true;
	}

	bool compare_and_exchange_weak(int64_t index, T& exchange, const T& compare)
	{
		T old_elem;

		mutex_check_weak(m_setting, m_resizing);
		if (index >= m_cur_pos)
		{
			m_setting--;
			return false;
		}

		do
		{
			old_elem = m_data[index];
			if (old_elem == m_free_value ||
				old_elem != compare)
			{
				exchange = old_elem;
				m_setting--;
				return false;
			}
		} while (!m_data[index].compare_exchange_weak(old_elem, exchange));

		m_setting--;

		return true;
	}

	size_t cur_pos() const
	{
		return m_cur_pos;
	}

	size_t size() const
	{
		return m_size;
	}

	size_t capacity() const
	{
		return m_capacity;
	}

protected:
	std::atomic<T>*				m_data;
	TAllocator					m_allocator;
	const T						m_inserting_value;
	const T						m_free_value;
	std::atomic<int64_t>		m_cur_pos;
	std::atomic<int64_t>		m_size;
	std::atomic<int64_t>		m_capacity;
	std::atomic<int64_t>		m_inserting;
	std::atomic<int64_t>		m_removing;
	std::atomic<int64_t>		m_setting;
	std::atomic<int64_t>		m_getting;
	std::atomic<int64_t>		m_resizing;

	void resize(int64_t new_capacity) 
	{
		mutex_check_strong(m_resizing, m_inserting, m_removing, m_setting, m_getting);
		assert(m_size == m_capacity);
		assert(m_resizing == 1);

		std::atomic<T>* new_data = new m_allocator.allocate(initial_capacity);
		assert(new_data);
		std::fill_n(new_data, initial_capacity, m_inserting_value);
		std::copy_n(m_data, m_capacity, new_data);

		m_allocator.deallocate(m_data);
		m_data = new_data;
		m_capacity = new_capacity;

		m_resizing--;
	}

};

template<typename T, typename TAllocator>
class wait_free_buffer_object : public wait_free_buffer_base<T, TAllocator>
{
	using base = wait_free_buffer_base<T, TAllocator>;

public:
	wait_free_buffer_object(const T& inserting, const T& free, int64_t capacity = 10, const TAllocator& allocator = TAllocator()) :
		base(inserting, free, capacity, allocator)
	{
	}

	~wait_free_buffer_object() 
	{
	}
};

template<typename T, typename TAllocator>
class wait_free_buffer_integer : public wait_free_buffer_base<T, TAllocator>
{
	using base = wait_free_buffer_base<T, TAllocator>;

public:
	wait_free_buffer_integer(const T& inserting, const T& free, int64_t capacity = 10, const TAllocator& allocator = TAllocator()) :
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

		mutex_check_weak(m_setting, m_resizing);
		if (index >= m_cur_pos)
		{
			m_setting--;
			return false;
		}

		do
		{
			old_elem = m_data[index];
			if (old_elem == m_free_value)
			{
				m_setting--;
				return false;
			}
			new_elem = old_elem + operand;
		} while (!m_data[index].compare_exchange_strong(old_elem, new_elem));

		result = old_elem;

		m_size++;
		m_setting--;

		return true;
	}

	bool fetch_and(int64_t index, T operand, T& result)
	{
		T old_elem;
		T new_elem;

		mutex_check_weak(m_setting, m_resizing);
		if (index >= m_cur_pos)
		{
			m_setting--;
			return false;
		}

		do
		{
			old_elem = m_data[index];
			if (old_elem == m_free_value)
			{
				m_setting--;
				return false;
			}
			new_elem = old_elem & operand;
		} while (!m_data[index].compare_exchange_strong(old_elem, new_elem));

		result = old_elem;

		m_size++;
		m_setting--;

		return true;
	}

	bool fetch_or(int64_t index, T operand, T& result) 
	{
		T old_elem;
		T new_elem;

		mutex_check_weak(m_setting, m_resizing);
		if (index >= m_cur_pos)
		{
			m_setting--;
			return false;
		}

		do
		{
			old_elem = m_data[index];
			if (old_elem == m_free_value)
			{
				m_setting--;
				return false;
			}
			new_elem = old_elem | operand;
		} while (!m_data[index].compare_exchange_strong(old_elem, new_elem));

		result = old_elem;

		m_size++;
		m_setting--;

		return true;
	}

	bool fetch_sub(int64_t index, T operand, T& result)
	{
		T old_elem;
		T new_elem;

		mutex_check_weak(m_setting, m_resizing);
		if (index >= m_cur_pos)
		{
			m_setting--;
			return false;
		}

		do
		{
			old_elem = m_data[index];
			if (old_elem == m_free_value)
			{
				m_setting--;
				return false;
			}
			new_elem = old_elem - operand;
		} while (!m_data[index].compare_exchange_strong(old_elem, new_elem));

		result = old_elem;

		m_size++;
		m_setting--;

		return true;
	}

	bool fetch_xor(int64_t index, T operand, T& result)
	{
		T old_elem;
		T new_elem;

		mutex_check_weak(m_setting, m_resizing);
		if (index >= m_cur_pos)
		{
			m_setting--;
			return false;
		}

		do
		{
			old_elem = m_data[index];
			if (old_elem == m_free_value)
			{
				m_setting--;
				return false;
			}
			new_elem = old_elem ^ operand;
		} while (!m_data[index].compare_exchange_strong(old_elem, new_elem));

		result = old_elem;

		m_size++;
		m_setting--;

		return true;
	}
};

template<typename T, typename TAllocator>
class wait_free_buffer_pointer : public wait_free_buffer_base<T, TAllocator>
{
	using base = wait_free_buffer_base<T, TAllocator>;

public:
	wait_free_buffer_pointer(const T& inserting, const T& free, int64_t capacity = 10, const TAllocator& allocator = TAllocator()) :
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

		mutex_check_weak(m_setting, m_resizing);
		if (index >= m_cur_pos)
		{
			m_setting--;
			return false;
		}

		do
		{
			old_elem = m_data[index];
			if (old_elem == m_free_value)
			{
				m_setting--;
				return false;
			}
			new_elem = old_elem + operand;
		} while (!m_data[index].compare_exchange_strong(old_elem, new_elem));

		result = old_elem;

		m_size++;
		m_setting--;

		return true;
	}

	bool fetch_sub(int64_t index, T operand, T& result)
	{
		T old_elem;
		T new_elem;

		mutex_check_weak(m_setting, m_resizing);
		if (index >= m_cur_pos)
		{
			m_setting--;
			return false;
		}

		do
		{
			old_elem = m_data[index];
			if (old_elem == m_free_value)
			{
				m_setting--;
				return false;
			}
			new_elem = old_elem - operand;
		} while (!m_data[index].compare_exchange_strong(old_elem, new_elem));

		result = old_elem;

		m_size++;
		m_setting--;

		return true;
	}
};

template<typename T, typename TAllocator = std::allocator<T>>
class wait_free_buffer : public wait_free_buffer_base<T, TAllocator>
{
public:
	wait_free_buffer(const T& inserting, const T& free, int64_t capacity = 10, const TAllocator& allocator = TAllocator()) :
		wait_free_buffer_base(inserting, free, capacity, allocator)
	{
	}

	~wait_free_buffer() 
	{

	}
};
