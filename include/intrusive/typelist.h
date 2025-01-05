#ifndef __INTRUSIVE_TYPELIST_H__
#define __INTRUSIVE_TYPELIST_H__

#include <cuchar>
#include <type_traits>

namespace lu {

template<class... Types>
struct typelist {};

template<class T, std::size_t Index>
struct get_nth;

template<class T, class... Types>
struct get_nth<typelist<T, Types...>, 0> {
    using type = T;
};

template<class T, class... Types, std::size_t Index>
struct get_nth<typelist<T, Types...>, Index> {
    using type = get_nth<typelist<Types...>, Index - 1>::type;
};

template<class T>
struct size;

template<class... Types>
struct size<typelist<Types...>> {
    static constexpr std::size_t value = sizeof...(Types);
};

template<class...>
struct concat;

template<class... Types>
struct concat<typelist<Types...>> {
    using type = typelist<Types...>;
};

template<class... Types1, class... Types2>
struct concat<typelist<Types1...>, typelist<Types2...>> {
    using type = typelist<Types1..., Types2...>;
};

template<class T, template<class> class Selector>
struct select;

template<template<class> class Selector>
struct select<typelist<>, Selector> {
    using type = typelist<>;
};

template<class T, class... Types, template<class> class Selector>
struct select<typelist<T, Types...>, Selector> {
    using selected = std::conditional_t<Selector<T>::value, typelist<T>, typelist<>>;
    using type = concat<selected, typename select<typelist<Types...>, Selector>::type>::type;
};

}// namespace lu

#endif