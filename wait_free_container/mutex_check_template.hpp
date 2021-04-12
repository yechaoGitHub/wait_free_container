#pragma once
#include <atomic>
#include <thread>

template<typename TCount, typename ...TMutex>
TCount mutex_check_weak(TCount& count, TMutex &...mutex)
{
    TCount ret = count;

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

    return ret;
}

template<typename TCount, typename ...TMutex>
TCount mutex_check_weak(std::atomic<TCount>& count, TMutex &...mutex)
{
    TCount ret = count;

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

    return ret;
}

template<typename TCount, typename ...TMutex>
TCount mutex_check_strong(TCount& count, TMutex &...mutex)
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

template<typename TCount, typename ...TMutex>
TCount mutex_check_strong(std::atomic<TCount>& count, TMutex &...mutex)
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

