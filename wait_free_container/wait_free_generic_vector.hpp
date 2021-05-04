#pragma once

#include <assert.h>

#include <algorithm>
#include <atomic>
#include <stdint.h>
#include <type_traits>
#include <vector>

#include "wait_free_memory_pool.hpp"
#include "wait_free_vector.hpp"
#include "template_util.hpp"

template<typename T, template<typename U> typename TAllocator = std::allocator>
class wait_free_generic_vecotor 
{

    using iterator = typename wait_free_memory_pool<T, TAllocator>::iterator;

public:
    wait_free_generic_vecotor(int64_t capacity = 10,
        const TAllocator<T>& memory_pool_allocator = TAllocator<T>(),
        const TAllocator<std::atomic<int64_t>>& vector_offset_allocator = TAllocator<std::atomic<int64_t>>()) :
        m_memory_pool(capacity, memory_pool_allocator),
        m_vector(0, capacity, vector_offset_allocator)
    {

    }

    // to fix.... add lock unlock function
    ~wait_free_generic_vecotor() 
    {

    }


    void push_back(const T& value)
    {
        iterator it = m_memory_pool.allocate();
        T* elem = it.lock();
        assert(elem);
        *elem = value;
        it.unlock();

        m_vector.push_back(it.offset());
    }

    bool remove(int64_t index, T& elem) noexcept
    {
        int64_t offset{};
        if (m_vector.remove(index, offset)) 
        {
            iterator it = m_memory_pool.get(offset);
            T* elem_src = it.lock();
            assert(elem_src);
            elem = std::move(*elem_src);
            it.unlock();
            m_memory_pool.deallocate(it);

        }
        else 
        {
            return false;
        }
    }

    bool remove(int64_t index)
    {
        int64_t offset{};
        if (m_vector.remove(index, offset))
        {
            iterator it = m_memory_pool.get(offset);
            T* elem_src = it.lock();
            assert(elem_src);
            (*elem_src).~T();
            it.unlock();
            bool b = m_memory_pool.deallocate(it);
            assert(b);
        }
        else
        {
            return false;
        }
    }

    // refer to std::vector
    void resize(int64_t new_size)
    {
       
    }


    bool get(int64_t index, T& elem)
    {
        int64_t offset{};
        if (m_vector.get(index, offset)) 
        {
            iterator it = m_memory_pool.get(offset);
            T* elem_src = it.lock();
            assert(elem_src);
            *elem_src = elem;
            it.unlock();
            
            return true;
        }
        else 
        {
            return false;
        }
    }


    bool get(int64_t index) 
    {
        return m_vector.get(index);
    }

    size_t size() const noexcept
    {
        return m_vector.size();
    }
    
private:

    wait_free_memory_pool<T, TAllocator>    m_memory_pool;
    wait_free_vector<int64_t, TAllocator>   m_vector;
};
