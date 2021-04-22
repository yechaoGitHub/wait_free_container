#pragma once

#include <assert.h>

#include <algorithm>
#include <atomic>
#include <stdint.h>
#include <type_traits>

#include "template_util.hpp"

#include <iostream>
#include <assert.h>
#include <vector>
#include <atomic>
#include <mutex>
#include <unordered_set>
#include <thread>
#include <windows.h>

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

        if (new_size == this->m_capacity)
        {
            increase_capacity();
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

    //modify interface
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

    //uncompleted,to do...
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
        


        volatile void** new_place = new volatile void* [new_capacity] { 0 };
        assert(new_place);

        ::memcpy(const_cast<void**>(new_place), const_cast<void**>(_data), _size * sizeof(void*));

        delete[] _data;
        _data = new_place;

        _capacity = new_capacity;

        _resizing--;
    }

};


class lock_free_vector
{
    static const int INIT_SIZE = 3;

public:
    lock_free_vector()
    {
        _data = new volatile void*[INIT_SIZE];
        assert(_data);
        ::memset(_data, 0, sizeof(void*) * INIT_SIZE);
        _capacity = INIT_SIZE;
        _size = 0;
        _inserting = 0;
        _removing = 0;
        _getting = 0;
        _resizing = 0;
    }

    ~lock_free_vector()
    {
        delete _data;
    }

    void insert(void* v)
    {
        assert(v);

        int64_t old_size(0);
        int64_t new_size(0);

        while (true)
        {
            MutexCheckWeak(_inserting, _resizing);
            old_size = _size;
            new_size = (std::min)(old_size + 1, static_cast<int64_t>(_capacity));
            if (old_size == new_size)
            {
                _inserting--;
                continue;
            }
            else if (!_size.compare_exchange_strong(old_size, new_size))
            {
                _inserting--;
                continue;
            }
            else
            {
                break;
            }
        }

        assert(new_size != old_size);

        while (InterlockedCompareExchange64(reinterpret_cast<volatile long long*>(&_data[old_size]), reinterpret_cast<long long>(v), 0))
        {
            std::this_thread::yield();
        }

        _inserting--;

        if (new_size == _capacity)
        {
            resize();
        }

    }

    bool remove(uint32_t index, void*& value)
    {
        int64_t old_size(0);
        int64_t new_size(0);

        MutexCheckWeak(_removing, _resizing, _getting);

        do
        {
            old_size = _size;
            if (index < old_size)
            {
                new_size = (std::max)(old_size - 1, 0ll);
            }
            else
            {
                _removing--;
                return false;
            }
        } while (!_size.compare_exchange_strong(old_size, new_size));

        while (true)
        {
            volatile void* old_value = _data[index];
            if (old_value &&
                (InterlockedCompareExchange64(reinterpret_cast<volatile long long*>(&_data[index]), 0, reinterpret_cast<long long>(old_value)) == reinterpret_cast<long long>(old_value)))
            {
                value = const_cast<void*>(old_value);
                break;
            }
            else
            {
                std::this_thread::yield();
            }
        }

        if (index != old_size - 1)
        {
            volatile void* old_value(nullptr);
            while (true)
            {
                old_value = _data[old_size - 1];
                if (old_value &&
                    (InterlockedCompareExchange64(reinterpret_cast<volatile long long*>(&_data[old_size - 1]), 0, reinterpret_cast<long long>(old_value)) == reinterpret_cast<long long>(old_value)))
                {
                    break;
                }
                else
                {
                    std::this_thread::yield();
                }
            }

            while (true)
            {
                if (InterlockedCompareExchange64(reinterpret_cast<volatile long long*>(&_data[index]), reinterpret_cast<long long>(old_value), 0) == 0)
                {
                    break;
                }
                else
                {
                    std::this_thread::yield();
                }
            }
        }

        _removing--;

        return true;
    }

    bool get(uint32_t index, void*& value)
    {
        int64_t old_total(0);
        int64_t new_total(0);

        MutexCheckWeak(_getting, _resizing, _removing, _inserting);

        if (index >= _size)
        {
            _getting--;
            return false;
        }

        value = const_cast<void*&>(_data[index]);

        _getting--;

        return true;
    }

    uint64_t size()
    {
        return _size;
    }

private:
    volatile void**         _data;
    std::atomic<int64_t>    _size;
    std::atomic<int64_t>    _capacity;
    std::atomic<int64_t>    _inserting;
    std::atomic<int64_t>    _removing;
    std::atomic<int64_t>    _getting;
    std::atomic<int64_t>    _resizing;

    void resize()
    {
        int64_t old_value = MutexCheckStrong(_resizing, _inserting, _removing, _getting);

        if (old_value != 0)
        {
            _resizing--;
            return;
        }

        int32_t new_capacity = _size * 1.5;

        volatile void** new_place = new volatile void* [new_capacity] { 0 };
        assert(new_place);

        ::memcpy(const_cast<void**>(new_place), const_cast<void**>(_data), _size * sizeof(void*));

        delete[] _data;
        _data = new_place;

        _capacity = new_capacity;

        _resizing--;
    }

    template<typename TCount, typename ...TMutex>
    void MutexCheckWeak(TCount &count, TMutex &...mutex)
    {
        while (true)
        {
            count++;

            TCount old_mutex_count = (0 + ... + mutex);
            if (old_mutex_count)
            {
                count--;

                while (true)
                {
                    std::this_thread::yield();
                    TCount new_mutex_total = (0 + ... + mutex);
                    if (new_mutex_total < old_mutex_count)
                    {
                        break;
                    }
                }
            }
            else
            {
                break;
            }
        }
    }

    template<typename TCount, typename ...TMutex>
    TCount MutexCheckStrong(TCount& count, TMutex &...mutex)
    {
        count++;
        while (true)
        {
            TCount old_mutex_count = (0 + ... + mutex);
            if (old_mutex_count)
            {
                std::this_thread::yield();
            }
            else
            {
                break;
            }
        }
    }

    template<typename TCount, typename ...TMutex>
    TCount MutexCheckStrong(std::atomic<TCount>& count, TMutex &...mutex)
    {
        TCount ret = count++;
        while (true)
        {
            TCount old_mutex_count = (0 + ... + mutex);
            if (old_mutex_count)
            {
                std::this_thread::yield();
            }
            else
            {
                break;
            }
        }

        return ret;
    }

}VEC;

std::atomic<uint64_t> d = 1;

std::mutex LOCK;
std::unordered_set<void*> ST_TEST;

std::atomic<bool> INSERT_COMPLETED = false;

void InsertSet(std::unordered_set<void*> &st, void* v)
{
    std::scoped_lock<std::mutex> raii_lock(LOCK);
    auto pair = st.insert(v);
    assert(pair.second);
}

void InsertThread()
{
    for (int i = 0; i < 100000; i++)
        //for(int i = 100; i > 0; i--)
    {
        void* v = (void*)d.fetch_add(1);

        VEC.insert((void*)v);
    }
}

void RemoveThread()
{
    void* v(0);

    while (!INSERT_COMPLETED || VEC.size())
    {
        if (VEC.remove(0, v))
        {
            InsertSet(ST_TEST, v);
        }
    }
}

int main()
{
    std::thread th1(InsertThread);
    std::thread th2(InsertThread);
    //std::this_thread::sleep_for(std::chrono::milliseconds(1));
    std::thread th3(RemoveThread);
    std::thread th4(RemoveThread);

    th1.join();
    th2.join();

    INSERT_COMPLETED = true;

    th3.join();
    th4.join();

    for (int64_t i = 0; i < 200000; i++)
    {
        auto it = ST_TEST.find((void*)(i + 1));
        assert(it != ST_TEST.end());
    }

    ::getchar();

    return 0;
}
