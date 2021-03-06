#pragma once

#include <assert.h>
#include <stdint.h>

#include <algorithm>
#include <atomic>
#include <thread>
#include <type_traits>
#include <memory>

#include "template_util.hpp"

enum class wait_free_elem_state : int64_t  
{ 
	free = 0, 
	inserting, 
	vailded,
	unallocated
};

//容器内的元素elem， 容器外value
template<typename T, template<typename U> typename TAllocator>
class wait_free_buffer_base 
{
	
	using base = wait_free_buffer_base;

public:

	explicit wait_free_buffer_base(const T& inserting, const T& free, int64_t capacity = 10, const TAllocator<std::atomic<T>>& allocator = TAllocator<std::atomic<T>>()) :
		m_data(nullptr),
		m_allocator(allocator),
		m_inserting_value(inserting),
		m_free_value(free),
		m_cur_pos(0),
		m_size(0),
		m_elem_operating(0),
		m_buffer_operating(0)
	{

		this->m_data = this->m_allocator.allocate(capacity);
		assert(m_data);

		std::for_each(this->m_data, this->m_data + capacity,
		[=](std::atomic<T>& elem)
		{
			elem.store(this->m_inserting_value);
		});

		this->m_capacity = capacity;
	}

	~wait_free_buffer_base()
	{
		mutex_check_cas_lock_strong(this->m_buffer_operating, this->m_elem_operating);

		this->m_allocator.deallocate(this->m_data, this->m_capacity);
		this->m_data = nullptr;
		this->m_size = 0;
		this->m_cur_pos = 0;
		this->m_capacity = 0;

		this->m_buffer_operating = false;
	}
	
	//在尾部添加元素,元素必须为inserting,增加size
	int64_t push_back(const T& value)
	{
		assert(value != this->m_inserting_value &&
			value != this->m_free_value);

		int64_t old_pos(0);
        T old_elem{};

		while (true)
		{
			while (true)
			{
				mutex_check_weak(this->m_elem_operating, this->m_buffer_operating);

				old_pos = this->m_cur_pos;
				if (old_pos >= this->m_capacity)
				{
					this->m_elem_operating--;
					std::this_thread::yield();
				}
				else 
				{
					break;
				}
			}

			if (this->m_cur_pos.compare_exchange_strong(old_pos, old_pos + 1))
			{
				break;
			}
			else 
			{
				this->m_elem_operating--;
			}
		}
	
		old_elem = this->m_data[old_pos];
		assert(old_elem == this->m_inserting_value);
		this->m_data[old_pos].store(value);

		this->m_size++;
		this->m_elem_operating--;

		if (old_pos >= this->m_capacity - 1)
		{
			increase_capacity(this->m_capacity * 1.5);
		}

		return old_pos;
	}

	//添加元素,元素必须为free,增加size
	bool insert(int64_t index, const T& value) noexcept
	{
		assert(index >= 0);

		T old_elem{};

		mutex_check_weak(this->m_elem_operating, this->m_buffer_operating);
		if (index >= this->m_cur_pos)
		{
			this->m_elem_operating--;
			return false;
		}

		do
		{
			old_elem = this->m_data[index];
			if (old_elem != this->m_free_value)
			{
				this->m_elem_operating--;
				return false;
			}
		} 
		while (!this->m_data[index].compare_exchange_strong(old_elem, value));

		this->m_size++;
		this->m_elem_operating--;

		return true;
	}

	bool remove(int64_t index, T* elem = nullptr) noexcept
	{
        T old_elem{};
		bool wait_for_inserting(false);

		mutex_check_weak(this->m_elem_operating, this->m_buffer_operating);
		
		assert(index >= 0);

		if (index >= this->m_cur_pos)
		{
			this->m_elem_operating--;
			return false;
		}

		do
		{
			do
			{
				old_elem = this->m_data[index];
				if (old_elem == this->m_free_value)
				{
					this->m_elem_operating--;
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
		while (!this->m_data[index].compare_exchange_strong(old_elem, this->m_free_value));

		if (elem)
		{
			*elem = old_elem;
		}		

		this->m_size--;
		this->m_elem_operating--;

		return true;
	}

	//在元素不为free,inserting情况下,设置元素值,不增加size
	bool store(int64_t index, T value) noexcept
	{
		T old_elem();

		mutex_check_weak(this->m_elem_operating, this->m_buffer_operating);
		if (index >= this->m_cur_pos)
		{
			this->m_elem_operating--;
			return false;
		}

		do
		{
			old_elem = this->m_data[index];
			if (old_elem == this->m_free_value ||
				old_elem == this->m_inserting_value)
			{
				this->m_elem_operating--;
				return false;
			}
		} 
		while (!this->m_data[index].compare_exchange_strong(old_elem, value));

		this->m_elem_operating--;

		return true;
	}

	bool load(int64_t index, T& elem) const noexcept
	{
		T old_elem();
		bool wait_for_inserting(false);

		mutex_check_weak(this->m_elem_operating, this->m_buffer_operating);

		if (index >= this->m_cur_pos)
		{
			this->m_elem_operating--;
			return false;
		}

		do
		{
			old_elem = this->m_data[index];
			if (old_elem == this->m_free_value)
			{
				this->m_elem_operating--;
				return false;
			}

			wait_for_inserting = old_elem == this->m_inserting_value;
			if (wait_for_inserting)
			{
				std::this_thread::yield();
			}
		} 
		while (wait_for_inserting);

		elem = old_elem;
		this->m_elem_operating--;

		return true;
	}

	wait_free_elem_state elem_state(int64_t index) const noexcept
	{
		wait_free_elem_state ret;

		mutex_check_weak(this->m_elem_operating, this->m_buffer_operating);

		if (index >= this->m_cur_pos)
		{
			this->m_elem_operating--;
			return wait_free_elem_state::unallocated;
		}

		T old_elem = this->m_data[index];
		if (old_elem == this->m_free_value)
		{
			ret = wait_free_elem_state::free;
		}
		else if (old_elem == this->m_inserting_value)
		{
			ret = wait_free_elem_state::inserting;
		}
		else 
		{
			ret = wait_free_elem_state::vailded;
		}

		this->m_elem_operating--;

		return ret;
	}

	bool compare_and_exchange_strong(int64_t index, bool &exchanged, T& compare_value, const T& exchange_value) noexcept
	{
		assert(exchange_value != this->m_inserting_value && 
			exchange_value != this->m_free_value);

		assert(compare_value != this->m_inserting_value &&
			compare_value != this->m_free_value);

		mutex_check_weak(this->m_elem_operating, this->m_buffer_operating);
		if (index >= this->m_cur_pos) 
		{
			this->m_elem_operating--;
			return false;
		}

		exchanged = this->m_data[index].compare_exchange_strong(compare_value, exchange_value);

		this->m_elem_operating--; 

		return true;
	}

	bool compare_and_exchange_weak(int64_t index, bool &exchanged, T& compare_value, const T& exchange_value) noexcept
	{
		assert(exchange_value != this->m_inserting_value &&
			exchange_value != this->m_free_value);

		assert(compare_value != this->m_inserting_value &&
			compare_value != this->m_free_value);

		mutex_check_weak(this->m_elem_operating, this->m_buffer_operating);
		if (index >= this->m_cur_pos)
		{
			this->m_elem_operating--;
			return false;
		}

		exchanged = this->m_data[index].compare_and_exchange_weak(compare_value, exchange_value);

		this->m_elem_operating--;

		return true;
	}

	void clear() noexcept
	{
		mutex_check_cas_lock_strong(this->m_buffer_operating, this->m_elem_operating);

		std::for_each(this->m_data, this->m_data + this->m_cur_pos,
		[=](std::atomic<T> &elem)
		{
			assert(elem != this->m_inserting_value);
			if (elem != this->m_free_value)
			{
				elem.~atomic<T>();
				elem.store(this->m_inserting_value);
			}
		});

		this->m_size = 0;
		this->m_cur_pos = 0;

		this->m_buffer_operating = false;
	}

	void resize(int64_t new_size) 
	{
		int64_t new_cur_pos{ new_size - 1 };

		if (new_cur_pos > m_capacity) 
		{
			increase_capacity((new_cur_pos + 1) * 1.5);
		}

		mutex_check_cas_lock_strong(this->m_buffer_operating, this->m_elem_operating);

		if (new_cur_pos > this->m_cur_pos) 
		{
			std::fill_n(this->m_data + this->m_cur_pos, new_cur_pos - m_cur_pos + 1, this->m_free_value);
		}
		else 
		{
			std::for_each(this->m_data + new_cur_pos, this->m_data + this->m_cur_pos + 1, 
			[=](std::atomic<T>& elem) 
			{
				if (elem == this->m_free_value) 
				{
					elem.~atomic<T>();
				}
				elem = this->m_inserting_value;
			});
		}
		
		m_cur_pos = new_cur_pos;

		this->m_buffer_operating = false;
	}

	size_t cur_pos() const noexcept
	{
		return this->m_cur_pos;
	}

	size_t elem_count() const noexcept
	{
		return this->m_size;
	}

	size_t capacity() const noexcept
	{
		return this->m_capacity;
	}

	const T& inserting_value() const noexcept
	{
		return m_inserting_value;
	}

	const T& free_value() const noexcept
	{
		return m_free_value;
	}

protected:
	std::atomic<T>*						m_data;
	TAllocator<std::atomic<T>>			m_allocator;
	const T								m_inserting_value;
	const T								m_free_value;
	std::atomic<int64_t>				m_cur_pos;
	std::atomic<int64_t>				m_size;
	std::atomic<int64_t>				m_capacity;
	mutable std::atomic<int64_t>		m_elem_operating;
	mutable std::atomic<int64_t>		m_buffer_operating;

	void increase_capacity(int64_t new_capacity)
	{
		mutex_check_cas_lock_strong(this->m_buffer_operating, this->m_elem_operating);

		if (new_capacity < m_capacity) 
		{
			this->m_buffer_operating = false;
			return;
		}

		std::atomic<T>* new_data = this->m_allocator.allocate(new_capacity);
		assert(new_data);
		std::fill_n(new_data, new_capacity, this->m_inserting_value);

		for (int64_t i = 0; i < this->m_cur_pos; i++)
		{
			assert(this->m_data[i] != this->m_inserting_value);
			new_data[i].store(this->m_data[i]);
		}

		this->m_allocator.deallocate(this->m_data, this->m_capacity);
		this->m_data = new_data;
		this->m_capacity = new_capacity;

		this->m_buffer_operating = false;
	}
};

template<typename T, template<typename U> typename TAllocator>
class wait_free_buffer_object : public wait_free_buffer_base<T, TAllocator>
{
	using base = wait_free_buffer_base<T, TAllocator>;

public:
    explicit wait_free_buffer_object(const T& inserting, const T& free, int64_t capacity = 10, const TAllocator<std::atomic<T>>& allocator = TAllocator<std::atomic<T>>()) :
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
    explicit wait_free_buffer_integer(const T& inserting, const T& free, int64_t capacity = 10, const TAllocator<std::atomic<T>>& allocator = TAllocator<std::atomic<T>>()) :
		base(inserting, free, capacity, allocator)
	{
	}

	~wait_free_buffer_integer()
	{
	}
	
	bool fetch_add(int64_t index, T operand, T& result) noexcept
	{
		T old_elem();
		T new_elem();

		mutex_check_weak(this->m_elem_operating, this->m_buffer_operating, this->m_resizing);
		if (index >= this->m_cur_pos)
		{
			this->m_elem_operating--;
			return false;
		}

		do
		{
			old_elem = this->m_data[index];
			if (old_elem == this->m_free_value ||
				old_elem == this->m_inserting_value)
			{
				this->m_elem_operating--;
				return false;
			}
			new_elem = old_elem + operand;
		} 
		while (!this->m_data[index].compare_exchange_strong(old_elem, new_elem));

		result = old_elem;

		this->m_elem_operating--;

		return true;
	}

	bool fetch_and(int64_t index, T operand, T& result) noexcept
	{
		T old_elem();
		T new_elem();

		mutex_check_weak(this->m_elem_operating, this->m_buffer_operating, this->m_resizing);
		if (index >= this->m_cur_pos)
		{
			this->m_elem_operating--;
			return false;
		}

		do
		{
			old_elem = this->m_data[index];
			if (old_elem == this->m_free_value ||
				old_elem == this->m_inserting_value)
			{
				this->m_elem_operating--;
				return false;
			}
			new_elem = old_elem & operand;
		} while (!this->m_data[index].compare_exchange_strong(old_elem, new_elem));

		result = old_elem;

		this->m_elem_operating--;

		return true;
	}

	bool fetch_or(int64_t index, T operand, T& result) noexcept
	{
		T old_elem();
		T new_elem();

		mutex_check_weak(this->m_elem_operating, this->m_buffer_operating, this->m_resizing);
		if (index >= this->m_cur_pos)
		{
			this->m_elem_operating--;
			return false;
		}

		do
		{
			old_elem = this->m_data[index];
			if (old_elem == this->m_free_value ||
				old_elem == this->m_inserting_value)
			{
				this->m_elem_operating--;
				return false;
			}
			new_elem = old_elem | operand;
		} 
		while (!this->m_data[index].compare_exchange_strong(old_elem, new_elem));

		result = old_elem;

		this->m_elem_operating--;

		return true;
	}

	bool fetch_sub(int64_t index, T operand, T& result) noexcept
	{
		T old_elem();
		T new_elem();

		mutex_check_weak(this->m_elem_operating, this->m_buffer_operating, this->m_resizing);
		if (index >= this->m_cur_pos)
		{
			this->m_elem_operating--;
			return false;
		}

		do
		{
			old_elem = this->m_data[index];
			if (old_elem == this->m_free_value ||
				old_elem == this->m_inserting_value)
			{
				this->m_elem_operating--;
				return false;
			}
			new_elem = old_elem - operand;
		} 
		while (!this->m_data[index].compare_exchange_strong(old_elem, new_elem));

		result = old_elem;

		this->m_setting--;

		return true;
	}

	bool fetch_xor(int64_t index, T operand, T& result) noexcept
	{
		T old_elem();
		T new_elem();

		mutex_check_weak(this->m_elem_operating, this->m_buffer_operating, this->m_resizing);
		if (index >= this->m_cur_pos)
		{
			this->m_elem_operating--;
			return false;
		}

		do
		{
			old_elem = this->m_data[index];
			if (old_elem == this->m_free_value ||
				old_elem == this->m_inserting_value)
			{
				this->m_elem_operating--;
				return false;
			}
			new_elem = old_elem ^ operand;
		} 
		while (!this->m_data[index].compare_exchange_strong(old_elem, new_elem));

		result = old_elem;

		this->m_setting--;

		return true;
	}
};

template<typename T, template<typename U> typename TAllocator>
class wait_free_buffer_pointer : public wait_free_buffer_base<T, TAllocator>
{
	using base = wait_free_buffer_base<T, TAllocator>;

public:
    explicit wait_free_buffer_pointer(const T& inserting, const T& free, int64_t capacity = 10, const TAllocator<std::atomic<T>>& allocator = TAllocator<std::atomic<T>>()) :
		base(inserting, free, capacity, allocator)
	{
	}

	~wait_free_buffer_pointer() 
	{
	}

	bool fetch_add(int64_t index, T operand, T& result) noexcept
	{
		T old_elem();
		T new_elem();

		mutex_check_weak(this->m_elem_operating, this->m_buffer_operating);
		if (index >= this->m_cur_pos)
		{
			this->m_elem_operating--;
			return false;
		}

		do
		{
			old_elem = this->m_data[index];
			if (old_elem == this->m_free_value ||
				old_elem == this->m_inserting_value)
			{
				this->m_elem_operating--;
				return false;
			}
			new_elem = old_elem + operand;
		} while (!this->m_data[index].compare_exchange_strong(old_elem, new_elem));

		result = old_elem;

		this->m_setting--;

		return true;
	}

	bool fetch_sub(int64_t index, T operand, T& result) noexcept
	{
		T old_elem();
		T new_elem();

		mutex_check_weak(this->m_setting, this->m_buffer_operating);
		if (index >= this->m_cur_pos)
		{
			this->m_setting--;
			return false;
		}

		do
		{
			old_elem = this->m_data[index];
			if (old_elem == this->m_free_value ||
				old_elem == this->m_inserting_value)
			{
				this->m_setting--;
				return false;
			}
			new_elem = old_elem - operand;
		} 
		while (!this->m_data[index].compare_exchange_strong(old_elem, new_elem));

		result = old_elem;

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
    explicit wait_free_buffer(const T& inserting, const T& free, int64_t capacity = 10, const TAllocator<std::atomic<T>>& allocator = TAllocator<std::atomic<T>>()) :
		base(inserting, free, capacity, allocator)
	{
	}

	~wait_free_buffer() 
	{

	}
};
