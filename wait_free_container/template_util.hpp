#pragma once

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
