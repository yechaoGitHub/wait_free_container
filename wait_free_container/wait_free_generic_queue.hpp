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
    explicit wait_free_generic_queue(
        int64_t capacity = 10, 
        const TAllocator<T>& memory_pool_allocator = TAllocator<T>(), 
        const TAllocator<std::atomic<int64_t>>& offset_queue_allocator = TAllocator<std::atomic<int64_t>>()) :
        m_memory_pool(capacity, memory_pool_allocator),
        m_queue(-1, capacity, offset_queue_allocator)
    {

    }

    // to fix.... add lock unlock function
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

        return m_queue.enqueue(it.offset());
    }

    template<typename TIterator>
    int64_t enqueue_range(TIterator it_start, const TIterator& it_end)
    {
        std::vector<int64_t> arr_it;

        while (it_start != it_end)
        {
            iterator it = m_memory_pool.allocate();
            T* elem = it.lock();
            *elem = *it_start;
            it.unlock();

            arr_it.push_back(it.offset());

            it_start++;
        }

        return m_queue.enqueue_range(arr_it.begin(), arr_it.end());
    }

    int64_t dequeue(T& elem) noexcept
    {
        int64_t offset{};
        int64_t ret = m_queue.dequeue(offset);
        if (ret != -1)
        {
            iterator it = m_memory_pool.get(offset);

            T* elem_src = it.lock();
            elem = std::move(*elem_src);
            it.unlock();

            m_memory_pool.deallocate(it);
        }

        return ret;
    }

    int64_t dequeue() noexcept 
    {
        int64_t offset{};
        int64_t ret = m_queue.dequeue(offset);
        if (ret != -1)
        {
            iterator it = m_memory_pool.get(offset);
            m_memory_pool.deallocate(it);
        }

        return ret;
    }

    template<typename TIterator>
    int64_t dequeue_range(TIterator it_start, const TIterator& it_end) noexcept
    {
        auto count = it_end - it_start;
        std::vector<int64_t> arr_offset(count, -1);
        auto arr_it_start = arr_offset.begin();
        auto arr_it_end = arr_offset.end();

        int64_t ret = this->m_queue.dequeue_range(arr_it_start, arr_it_end);
        if (ret != -1) 
        {
            count = arr_it_start - arr_offset.begin();

            for (int64_t i = 0; i < count; i++, it_start++)
            {
                int64_t offset = arr_offset[i];
                iterator it = this->m_memory_pool.get(offset);
                T* elem = it.lock();
                *it_start = std::move(*elem);
                it.unlock();

                this->m_memory_pool.deallocate(it);
            }
        }

        return ret;
    }

    int64_t dequeue_range(int64_t& count) noexcept 
    {
        std::vector<int64_t> arr_offset(count, -1);
        auto arr_start = arr_offset.begin();
        int64_t ret = m_queue.dequeue_range(arr_start, arr_offset.end());

        count =  arr_offset.end() - arr_start;
        for (int64_t i = 0; i < count; i++) 
        {
            int64_t offset = arr_offset[i];
            iterator it = this->m_memory_pool.get(offset);
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
    wait_free_queue<int64_t, TAllocator>    m_queue;
};


