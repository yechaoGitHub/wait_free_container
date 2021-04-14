#include "wait_free_pointer_queue.h"
#include "mutex_check_template.hpp"
#include <windows.h>
#include <assert.h>
#include <thread>
#include <algorithm>

wait_free_pointer_queue::wait_free_pointer_queue(int64_t capacity) :
    m_data(nullptr),
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

    m_data = new void* [capacity] { 0 };
    assert(m_data);
    m_capacity = capacity;
}

wait_free_pointer_queue::~wait_free_pointer_queue()
{
    delete m_data;
}

int64_t wait_free_pointer_queue::enqueue(void* elem)
{
    int64_t old_size(0);
    int64_t new_size(0);
    int64_t old_count(0);
    int64_t en_pos(0);
    void* old_value(nullptr);
    bool full(false);
    bool size_failed(false);

    do
    {
        do
        {
            mutex_check_weak(m_enqueuing, m_reszing, m_stuck_enqueue);

            old_size = m_size;
            new_size = (std::min)(old_size + 1, static_cast<int64_t>(m_capacity));
            full = new_size <= old_size;
            if (full)
            {
                m_enqueuing--;
                std::this_thread::yield();
            }
        } while (full);

        size_failed = !m_size.compare_exchange_strong(old_size, new_size);
        if (size_failed)
        {
            m_enqueuing--;           
            std::this_thread::yield();
        }
    } while (size_failed);

    do
    {
        old_count = m_enqueue_count;
        en_pos = (old_count + m_offset) % m_capacity;
    } while (!m_enqueue_count.compare_exchange_strong(old_count, old_count + 1));

    while (InterlockedCompareExchange64(reinterpret_cast<volatile long long*>(&m_data[en_pos]), reinterpret_cast<long long>(elem), 0) != 0)
    {
        std::this_thread::yield();
    }

    m_enqueuing--;

    if (new_size == m_capacity)
    {
        resize(new_size * 1.5);
    }

    return old_count;
}

int64_t wait_free_pointer_queue::enqueue_range(void** arr_elem, int64_t count)
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

    do
    {
        do
        {
            mutex_check_weak(m_enqueuing, m_reszing, m_stuck_enqueue);

            old_size = m_size;
            new_size = (std::min)(old_size + static_cast<int64_t>(count), static_cast<int64_t>(m_capacity));
            full = new_size <= old_size;
            if (full)
            {
                m_enqueuing--;
                std::this_thread::yield();
            }
        } while (full);

        size_failed = !m_size.compare_exchange_strong(old_size, new_size);
        if (size_failed) 
        {
            m_enqueuing--;    
            std::this_thread::yield();
        }

    } while (size_failed);

    if (new_size == m_capacity)
    {
        stuck_enqueue();
        resized = true;
    }

    fill_count = new_size - old_size;
    do
    {
        old_count = m_enqueue_count;
        en_pos = (old_count + m_offset) % m_capacity;
    } while (!m_enqueue_count.compare_exchange_strong(old_count, old_count + fill_count));

    //为了能顺利dequeue先把queue填满
    for (int64_t i = 0; i < fill_count; i++)
    {
        while (InterlockedCompareExchange64(reinterpret_cast<volatile long long*>(&m_data[en_pos]), reinterpret_cast<long long>(arr_elem[i]), 0) != 0)
        {
            std::this_thread::yield();
        }

        en_pos = (en_pos + 1) % m_capacity;
    }

    //处理余下的elem // bug
    remain_count = count - fill_count;
    if (resized)
    {

        resize((new_size + remain_count) * 1.5, &arr_elem[fill_count], remain_count);
        m_stuck_enqueue = 0;
    }
    else
    {
        m_enqueuing--;
    }

    return old_count;
}

bool wait_free_pointer_queue::dequeue(void** elem, int64_t* index)
{
    int64_t old_size(0);
    int64_t new_size(0);
    int64_t old_count(0);
    int64_t de_pos(0);
    void* old_value(nullptr);

    if (m_size <= 0)
    {
        return false;
    }

    mutex_check_weak(m_dequeuing, m_reszing);

    do
    {
        old_size = m_size;
        new_size = (std::max)(old_size - 1, 0ll);
        if (new_size >= old_size)
        {
            m_dequeuing--;
            return false;
        }
    } while (!m_size.compare_exchange_strong(old_size, new_size));

    do
    {
        old_count = m_dequeue_count;
        de_pos = (old_count + m_offset) % m_capacity;
    } while (!m_dequeue_count.compare_exchange_strong(old_count, old_count + 1));

    while (true)
    {
        old_value = m_data[de_pos];
        if (!old_value)
        {
            std::this_thread::yield();
            continue;
        }
        else
        {
            if (InterlockedCompareExchange64(reinterpret_cast<volatile long long*>(&m_data[de_pos]), 0, reinterpret_cast<long long>(old_value)) == reinterpret_cast<long long> (old_value))
            {
                *elem = old_value;
                break;
            }
        }
    }

    m_dequeuing--;

    if (index)
    {
        *index = old_count;
    }

    return true;
}

int64_t wait_free_pointer_queue::dequeue_range(void** elem, int64_t arr_size, int64_t* index)
{
    int64_t count(arr_size / sizeof(void*));
    int64_t old_size(0);
    int64_t new_size(0);
    int64_t old_count(0);
    int64_t de_pos(0);
    void* old_value(nullptr);

    if (m_size <= 0)
    {
        return 0;
    }

    mutex_check_weak(m_dequeuing, m_reszing);

    do
    {
        old_size = m_size;
        new_size = (std::max)(old_size - count, 0ll);
        if (new_size >= old_size)
        {
            m_dequeuing--;
            return false;
        }
    } while (!m_size.compare_exchange_strong(old_size, new_size));

    count = old_size - new_size;

    do
    {
        old_count = m_dequeue_count;
        de_pos = (old_count + m_offset) % m_capacity;
    } while (!m_dequeue_count.compare_exchange_strong(old_count, old_count + count));

    for (int64_t i = 0; i < count; i++)
    {
        while (true)
        {
            old_value = m_data[de_pos];
            if (!old_value)
            {
                std::this_thread::yield();
                continue;
            }
            else
            {
                if (InterlockedCompareExchange64(reinterpret_cast<volatile long long*>(&m_data[de_pos]), 0, reinterpret_cast<long long>(old_value)) == reinterpret_cast<long long> (old_value))
                {
                    elem[i] = old_value;
                    break;
                }
            }
        }

        de_pos = (de_pos + 1) % m_capacity;
    }

    m_dequeuing--;

    if (index)
    {
        *index = old_count;
    }

    return count;
}

size_t wait_free_pointer_queue::size()
{
    return m_size;
}

int64_t wait_free_pointer_queue::resize(int64_t new_capacity)
{
    //队列满后，出列元素，在入列元素，会导致resize重复调用，只允许第一个争抢到的线程运行resize
    int64_t old_value = mutex_check_strong(m_reszing, m_enqueuing, m_dequeuing);
    if (old_value != 0)
    {
        m_reszing--;
        return 0;
    }

    void** new_data = new void* [new_capacity] { 0 };
    assert(new_data);

    int64_t head_pos((m_dequeue_count + m_offset) % m_capacity);
    int64_t tail_pos((m_enqueue_count + m_offset) % m_capacity);

    for (int64_t i = 0; i < m_size; i++)
    {
        new_data[i] = m_data[head_pos];
        head_pos = (head_pos + 1) % m_capacity;
    }

    assert(head_pos == tail_pos);

    delete m_data;
    m_data = new_data;
    m_offset = new_capacity - (m_dequeue_count % new_capacity);
    m_capacity = new_capacity;

    m_reszing--;

    return new_capacity;
}

int64_t wait_free_pointer_queue::resize(int64_t new_capacity, void** extra_copy, int64_t size)
{
    //队列满后，出列元素，在入列元素，会导致resize重复调用，只允许第一个争抢到的线程运行resize
    int64_t old_value = mutex_check_strong(m_reszing, m_enqueuing, m_dequeuing);
    if (old_value != 0)
    {
        m_reszing--;
        return 0;
    }

    void** new_data = new void* [new_capacity] { 0 };
    assert(new_data);

    int64_t head_pos((m_dequeue_count + m_offset) % m_capacity);
    int64_t tail_pos((m_enqueue_count + m_offset) % m_capacity);

    for (int64_t i = 0; i < m_size; i++)
    {
        new_data[i] = m_data[head_pos];
        head_pos = (head_pos + 1) % m_capacity;
    }
    assert(head_pos == tail_pos);
    m_offset = new_capacity - (m_dequeue_count % new_capacity);

    int64_t en_pos(0);
    for (int64_t i = 0; i < size; i++)
    {
        en_pos = (m_enqueue_count + m_offset) % new_capacity;
        new_data[en_pos] = extra_copy[i];
        m_enqueue_count++;
    }

    delete m_data;
    m_data = new_data;
    m_size += size;
    m_capacity = new_capacity;
    m_reszing--;

    return new_capacity;
}

void wait_free_pointer_queue::stuck_enqueue()
{
    while (m_stuck_enqueue.exchange(1))
    {
        std::this_thread::yield();
    }

    m_enqueuing--;
    while (m_enqueuing)
    {
        std::this_thread::yield();
    }
}