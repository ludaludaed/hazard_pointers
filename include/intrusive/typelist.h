#ifndef __INTRUSIVE_TYPELIST_H__
#define __INTRUSIVE_TYPELIST_H__

#include <cuchar>
#include <type_traits>
#include <utility>

namespace lu {

template<class... Ts>
struct typelist {};

template<std::size_t I, class T>
struct get_nth;

template<class T, class... Ts>
struct get_nth<0, typelist<T, Ts...>> {
    using type = T;
};

template<std::size_t I, class T, class... Ts>
struct get_nth<I, typelist<T, Ts...>> {
    using type = get_nth<I - 1, typelist<Ts...>>::type;
};

template<std::size_t I, class T>
using get_nth_t = typename get_nth<I, T>::type;

template<std::size_t I, class T, class U>
struct find_index;

template<std::size_t I, class T>
struct find_index<I, T, typelist<>> {
    static_assert(!(I == 0), "empty type list.");
    static_assert(!(I != 0), "type not exist.");
};

template<std::size_t I, class T, class U, class... Us>
struct find_index<I, T, typelist<U, Us...>> {
    static constexpr std::size_t value
            = std::conditional_t<std::is_same_v<T, U>, std::integral_constant<std::size_t, I>,
                                 find_index<I + 1, T, typelist<Us...>>>::value;
};

template<class T, class U>
static constexpr std::size_t find_index_v = find_index<0, T, U>::value;

template<class T>
struct size_of;

template<class... Ts>
struct size_of<typelist<Ts...>> {
    static constexpr std::size_t value = sizeof...(Ts);
};

template<class T>
static constexpr std::size_t size_of_v = size_of<T>::value;

template<class...>
struct concat;

template<class... Ts>
struct concat<typelist<Ts...>> {
    using type = typelist<Ts...>;
};

template<class... Ts1, class... Ts2, class... Ts3>
struct concat<typelist<Ts1...>, typelist<Ts2...>, Ts3...> {
    using type = typename concat<typelist<Ts1..., Ts2...>, Ts3...>::type;
};

template<class... Ts>
using concat_t = typename concat<Ts...>::type;

template<class T, template<class> class Selector>
struct select;

template<template<class> class Selector>
struct select<typelist<>, Selector> {
    using type = typelist<>;
};

template<class T, class... Ts, template<class> class Selector>
struct select<typelist<T, Ts...>, Selector> {
    using selected = std::conditional_t<Selector<T>::value, typelist<T>, typelist<>>;
    using type = concat_t<selected, typename select<typelist<Ts...>, Selector>::type>;
};

template<class T, template<class> class Selector>
using select_t = typename select<T, Selector>::type;

template<class T, template<class, class> class Compare>
struct sort;

template<template<class, class> class Compare>
struct sort<typelist<>, Compare> {
    using type = typelist<>;
};

template<class T, class... Ts, template<class, class> class Compare>
struct sort<typelist<T, Ts...>, Compare> {
    template<class _T>
    struct predicate {
        static constexpr bool value = Compare<_T, T>::value;
    };

    template<class _T>
    struct invert_predicate {
        static constexpr bool value = !Compare<_T, T>::value;
    };

    using left = select_t<typelist<Ts...>, predicate>;
    using right = select_t<typelist<Ts...>, invert_predicate>;

    using type = concat_t<typename sort<left, Compare>::type, typelist<T>, typename sort<right, Compare>::type>;
};

template<class T, template<class, class> class Compare>
using sort_t = typename sort<T, Compare>::type;

template<class I, class T, template<std::size_t, class> class Pack>
struct pack_with_index;

template<std::size_t... Is, class... Ts, template<std::size_t, class> class Pack>
struct pack_with_index<std::index_sequence<Is...>, typelist<Ts...>, Pack> {
    static_assert(sizeof...(Is) == sizeof...(Ts), "the number of indexes must be equal to the number of types.");
    using type = typelist<Pack<Is, Ts>...>;
};

template<class I, class T, template<std::size_t, class> class Pack>
using pack_with_index_t = typename pack_with_index<I, T, Pack>::type;

template<class T, class U>
struct num_of_type;

template<class T>
struct num_of_type<T, typelist<>> {
    static constexpr std::size_t value = 0;
};

template<class T, class U, class... Us>
struct num_of_type<T, typelist<U, Us...>> {
    static constexpr std::size_t value
            = std::conditional_t<std::is_same_v<T, U>, std::integral_constant<std::size_t, 1>,
                                 std::integral_constant<std::size_t, 0>>::value
              + num_of_type<T, typelist<Us...>>::value;
};

template<class T, class U>
static constexpr std::size_t num_of_type_v = num_of_type<T, U>::value;

}// namespace lu

#endif