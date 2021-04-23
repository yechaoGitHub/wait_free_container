#pragma once
#include <assert.h>

#include <algorithm>
#include <atomic>
#include <stdint.h>
#include <type_traits>
#include <vector>

#include "wait_free_memory_pool.hpp"
#include "wait_free_queue.hpp"
#include "template_util.hpp"


//replace wait_free_queue<iterator> to  wait_free_queue<int64_t>, serious error;
template<typename T, template<typename U> typename TAllocator = std::allocator>
class wait_free_generic_queue 
{

    using iterator = typename wait_free_memory_pool<T, TAllocator>::iterator;

public:
    explicit wait_free_generic_queue(int64_t capacity, const TAllocator<T>& memory_pool_allocator, const TAllocator<std::atomic<iterator>>& ) :
        m_memory_pool(capacity),
        m_queue(iterator(), capacity)
    {

    }

    ~wait_free_generic_queue()
    {

    }

    int64_t enqueue(const T& value)
    {
        iterator it = m_memory_pool.allocate();
        T* elem = it.lock();
        assert(elem != nullptr);
        *elem = value;
        it.unlock();

        return m_queue.enqueue(it);
    }

    template<typename TIterator>
    int64_t enqueue_range(TIterator it_start, const TIterator& it_end)
    {
        std::vector<iterator> arr_it;

        while (it_start != it_end)
        {
            iterator it = m_memory_pool.allocate();
            T* elem = it.lock();
            *elem = *it_start;
            it.unlock();

            arr_it.push_back(it);

            it_start++;
        }

        return m_queue.enqueue_range(arr_it.begin(), arr_it.end());
    }

    int64_t dequeue(T& elem) noexcept
    {
        iterator it;
        int64_t ret = m_queue.dequeue(it);
        if (ret != -1)
        {
            T* elem_src = it.lock();
            elem = std::move(*elem_src);
            it.unlock();

            m_memory_pool.deallocate(it);
        }

        return ret;
    }

    int64_t dequeue() noexcept 
    {
        iterator it;
        int64_t ret = m_queue.dequeue(it);
        if (ret != -1)
        {
            m_memory_pool.deallocate(it);
        }

        return ret;
    }

    template<typename TIterator>
    int64_t dequeue_range(TIterator it_start, const TIterator& it_end) noexcept
    {
        auto count = it_end - it_start;
        std::vector<iterator> arr_it(count, iterator());
        
        int64_t ret = m_queue.enqueue_range(arr_it.begin(), arr_it.end());
        if (ret != -1) 
        {
            for (int64_t i = 0; i < count; i++, it_start++)
            {
                iterator &it = arr_it[i];
                T* elem = it.lock();
                *it_start = std::move(*elem);
                it.unlock();

                m_memory_pool.deallocate(it);
            }
        }

        return ret;
    }

    int64_t dequeue_range(int64_t& count) noexcept 
    {
        std::vector<iterator> arr_it(count, iterator());
        auto it_start = arr_it.begin();
        int64_t ret = m_queue.dequeue_range(it_start, arr_it.end());

        count = it_start - arr_it.end();
        for (int64_t i = 0; i < count; i++) 
        {
            iterator& it = arr_it[i];
            bool b = m_memory_pool.deallocate(it);
            assert(b);
        }
        
        return ret;
    }

    size_t size() const noexcept
    {
        return m_queue.size();
    }

    size_t capacity() const noexcept
    {
        return m_queue.capacity();
    }

private:

    wait_free_memory_pool<T, TAllocator>    m_memory_pool;
    wait_free_queue<iterator, TAllocator>   m_queue;

};


