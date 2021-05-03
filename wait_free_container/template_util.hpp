#pragma once
#include <atomic>
#include <thread>

#pragma region(select_type)
template <bool, typename T1, typename T2>
struct select_type 
{ 
};

template <typename T1, typename T2>
struct select_type<true, T1, T2>
{
	using type = T1;
};

template <typename T1, typename T2>
struct select_type<false, T1, T2>
{
	using type = T2;
};
#pragma endregion

#pragma region(mutex_check_template)
template<typename TCount, typename ...TMutex>
TCount mutex_check_weak(std::atomic<TCount>& count, std::atomic<TMutex>&... mutex)
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
TCount mutex_check_strong(std::atomic<TCount>& count, std::atomic<TMutex>&...mutex)
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
TCount mutex_check_cas_weak(std::atomic<TCount>& count, std::atomic<TMutex>&... mutex)
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
void mutex_check_cas_lock_weak(std::atomic<TCount>& count, std::atomic<TMutex>&... mutex)
{
	while (true)
	{
		while (count.exchange(true))
		{
			std::this_thread::yield();
		}

		TCount old_mutex_count = (0 + ... + mutex);
		if (old_mutex_count)
		{
			count = false;

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
void mutex_check_cas_lock_strong(std::atomic<TCount>& count, std::atomic<TMutex>&... mutex) 
{
	while (count.exchange(true))
	{
		std::this_thread::yield();
	}

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

#pragma endregion
