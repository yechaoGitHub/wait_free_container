#pragma once

template <bool>
struct select_type { 
    template <class T1, class>
    using type = T1;
};

template <>
struct select_type<false> {
    template <class, class T2>
    using type = T2;
};