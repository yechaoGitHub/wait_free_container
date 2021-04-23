#pragma once

#include <assert.h>

#include <algorithm>
#include <atomic>
#include <stdint.h>
#include <type_traits>

#include "template_util.hpp"

//not tested
template<typename T, template<typename U> typename TAllocator = std::allocator>
class wait_free_vector 
{
public:
    explicit wait_free_vector(const T& free_value, int64_t capacity = 10, const TAllocator<std::atomic<T>>& allocator = TAllocator<std::atomic<T>>()) :
        m_data(nullptr),
        m_allocator(allocator),
        m_free_value(free_value),
        m_size(0),
        m_capacity(0),
        m_elem_operating(0),
        m_resizing(0)
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

    ~wait_free_vector() 
    {

    }

    void push_back(const T& value) 
    {
        assert(value != this->m_free_value);

        int64_t old_size(0);
        int64_t new_size(0);
        T free_value{ this->m_free_value };

        while (true)
        {
            mutex_check_weak(this->m_elem_operating, this->m_resizing);

            old_size = this->m_size;
            new_size = (std::min)(old_size + 1, this->m_capacity);
            if (old_size >= new_size || !this->m_size.compare_exchange_strong(old_size, new_size))
            {
                this->m_elem_operating--;
                std::this_thread::yield();
            }
            else
            {
                break;
            }
        }

        assert(new_size > old_size);

        while (!this->m_data[old_size].compare_exchange_strong(free_value, value))
        {
            free_value = this->m_free_value;
            std::this_thread::yield();
        } 

        this->m_elem_operating--;

        if (new_size >= this->m_capacity)
        {
            increase_capacity(new_size * 1.5);
        }
    }

    bool remove(int64_t index, T* elem = nullptr) noexcept
    {
        int64_t old_size(0);
        int64_t new_size(0);
        T old_elem{};
        T free_value{ this->m_free_value };

        mutex_check_weak(this->m_elem_operating, this->m_resizing);

        do
        {
            old_size = this->m_size;
            if (index < old_size)
            {
                new_size = (std::max)(old_size - 1, 0ll);
            }
            else
            {
                this->m_elem_operating--;
                return false;
            }
        } 
        while (!this->m_size.compare_exchange_strong(old_size, new_size));

        while (true)
        {
            old_elem = this->m_data[index];

            if (old_elem != this->m_free_value && this->m_data[index].compare_exchange_strong(old_elem, this->m_free_value))
            {
                if (elem) 
                {
                    *elem = old_elem;
                }
                break;
            }
            else 
            {
                std::this_thread::yield();
            }
        }

        if (index != old_size - 1)
        {
            while (true)
            {
                old_elem = this->m_data[old_size - 1];
                if (old_elem != this->m_free_value && this->m_data[old_size - 1].compare_exchange_strong(old_elem, this->m_free_value))
                {
                    break;
                }
                else
                {
                    std::this_thread::yield();
                }
            }

            while (!this->m_data[index].compare_exchange_strong(free_value, old_elem))
            {
                free_value = this->m_free_value;
                std::this_thread::yield();
            }
        }

        this->m_elem_operating--;

        return true;
    }

    void resize(int64_t new_size) 
    {
        if (new_size > this->m_capacity) 
        {
            increase_capacity(new_size);
        }

        mutex_check_cas_lock_strong(this->m_resizing, this->m_elem_operating);
        this->m_size = new_size:
        this->m_resizing = false;
    }

    bool get(uint32_t index, T* elem = nullptr)
    {
        T old_elem{};

        mutex_check_weak(this->m_elem_operating, this->m_resizing);
        
        do 
        {
            if (index >= this->m_size)
            {
                this->m_elem_operating--;
                return false;
            }

            old_elem = this->m_data[index];
        } 
        while (old_elem == this->m_free_value);
        
        if (elem) 
        {
            *elem = old_elem;
        }
        
        this->m_elem_operating--;

        return true;
    }

    size_t size() const noexcept
    {
        return this->m_size;
    }

private:

    std::atomic<T>*					m_data;
    TAllocator<std::atomic<T>>		m_allocator;
    const T                         m_free_value;
    std::atomic<int64_t>            m_size;
    std::atomic<int64_t>            m_capacity;
    mutable std::atomic<int64_t>	m_elem_operating;
    mutable std::atomic<int64_t>	m_resizing;

    void increase_capacity(int64_t new_capacity) 
    {
        mutex_check_cas_lock_strong(this->m_resizing, this->m_elem_operating);

        if (new_capacity <= this->m_capacity)
        {
            this->m_resizing = false;
            return;
        }
        
        std::atomic<T>* new_data = m_allocator.allocate(new_capacity);
        assert(new_data);
        std::for_each(new_data, new_data + new_capacity, 
        [=](std::atomic<T>& elem) 
        {
            elem.store(this->m_free_value);
        });
        
        std::uninitialized_copy(new_data, this->m_size, this->m_data);
        
        m_allocator.deallocate(this->m_data, new_capacity);
        this->m_data = new_data;
        this->m_capacity = new_capacity;

        this->m_resizing = false;
    }
};
