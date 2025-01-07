#ifndef __INTRUSIVE_COMPRESSED_TUPLE_H__
#define __INTRUSIVE_COMPRESSED_TUPLE_H__

#include "typelist.h"

#include <tuple>
#include <type_traits>
#include <utility>


namespace lu {
namespace detail {

template<class T>
struct is_not_empty {
    static constexpr bool value = !std::is_empty_v<T>;
};

template<class T1, class T2>
struct aligment_compare {
    static constexpr bool value = alignof(T1) > alignof(T2);
};

template<std::size_t I, class T>
struct tuple_unit {
    constexpr tuple_unit() = default;

    template<class _T>
    constexpr tuple_unit(_T&& value)
        : data(std::forward<_T>(value)) {}

    void swap(tuple_unit &other) {
        std::swap(data, other.data);
    }

    T data;
};

template<class Is, class Ts>
struct tuple_base;

template<>
struct tuple_base<std::index_sequence<>, typelist<>> {
    constexpr tuple_base() = default;

    constexpr void swap(tuple_base &other) {}
};

template<std::size_t... Is, class... Ts>
struct tuple_base<std::index_sequence<Is...>, typelist<Ts...>> : tuple_unit<Is, Ts>... {
    constexpr tuple_base() = default;

    template<class... _Ts>
    constexpr tuple_base(_Ts&&... args)
        : tuple_unit<Is, Ts>(std::forward<_Ts>(args))... {}

    void swap(tuple_base &other) {
        ((tuple_unit<Is, Ts>::swap(other), 0), ...);
    }
};

template<std::size_t I, class T>
struct pack {};

template<std::size_t I, class T>
struct is_not_empty<pack<I, T>> {
    static constexpr bool value = is_not_empty<T>::value;
};

template<std::size_t I1, std::size_t I2, class T1, class T2>
struct aligment_compare<pack<I1, T1>, pack<I2, T2>> {
    static constexpr bool value = aligment_compare<T1, T2>::value;
};

template<class T, template<std::size_t, class> class Pack>
struct get_indices;

template<template<std::size_t, class> class Pack>
struct get_indices<typelist<>, Pack> {
    using type = std::index_sequence<>;
};

template<std::size_t... Is, class... Ts, template<std::size_t, class> class Pack>
struct get_indices<typelist<Pack<Is, Ts>...>, Pack> {
    using type = std::index_sequence<Is...>;
};
template<class T, template<std::size_t, class> class Pack>
using get_indices_t = typename get_indices<T, Pack>::type;

template<class T, template<std::size_t, class> class Pack>
struct get_types;

template<template<std::size_t, class> class Pack>
struct get_types<typelist<>, Pack> {
    using type = typelist<>;
};

template<std::size_t... Is, class... Ts, template<std::size_t, class> class Pack>
struct get_types<typelist<Pack<Is, Ts>...>, Pack> {
    using type = typelist<Ts...>;
};

template<class T, template<std::size_t, class> class Pack>
using get_types_t = typename get_types<T, Pack>::type;

}// namespace detail

template<class... Ts>
class compressed_tuple;

template<std::size_t, class>
struct tuple_element_t;

template<std::size_t I, class... Ts>
struct tuple_element_t<I, compressed_tuple<Ts...>> {
    using type = get_nth_t<I, typelist<Ts...>>;
};

template<class... Ts>
class compressed_tuple {
    using original_packs = pack_with_index_t<typelist<Ts...>, detail::pack>;
    using compressed_packs = sort_t<select_t<original_packs, detail::is_not_empty>, detail::aligment_compare>;

    using indices = detail::get_indices_t<compressed_packs, detail::pack>;
    using types = detail::get_types_t<compressed_packs, detail::pack>;

    using tuple_base = detail::tuple_base<indices, types>;

    template<std::size_t I, class... _Ts>
    friend tuple_element_t<I, compressed_tuple<_Ts...>> &get(compressed_tuple<_Ts...> &);

    template<std::size_t I, class... _Ts>
    friend tuple_element_t<I, compressed_tuple<_Ts...>> &&get(compressed_tuple<_Ts...> &&);

    template<std::size_t I, class... _Ts>
    friend const tuple_element_t<I, compressed_tuple<_Ts...>> &get(const compressed_tuple<_Ts...> &);

    template<std::size_t I, class... _Ts>
    friend const tuple_element_t<I, compressed_tuple<_Ts...>> &&get(const compressed_tuple<_Ts...> &&);

    template<class T, class... _Ts>
    friend T &get(compressed_tuple<_Ts...> &);

    template<class T, class... _Ts>
    friend T &&get(compressed_tuple<_Ts...> &&);

    template<class T, class... _Ts>
    friend const T &get(const compressed_tuple<_Ts...> &);

    template<class T, class... _Ts>
    friend const T &&get(const compressed_tuple<_Ts...> &&);

public:
    using compressed_types = detail::get_types_t<compressed_packs, detail::pack>;

private:
    template<class Args, std::size_t... Indexes>
    constexpr compressed_tuple(Args args, std::index_sequence<Indexes...>)
        : base_(std::forward<std::tuple_element_t<Indexes, Args>>(std::get<Indexes>(args))...) {}

public:
    compressed_tuple() = default;

    template<class... _Ts>
    constexpr compressed_tuple(_Ts &&...ts)
        : compressed_tuple(std::forward_as_tuple(std::forward<_Ts>(ts)...), indices{}) {}

    void swap(compressed_tuple &other) {
        base_.swap(other.base_);
    }

    friend void swap(compressed_tuple &left, compressed_tuple &right) {
        left.swap(right);
    }

private:
    tuple_base base_;
};

template<class... Ts>
compressed_tuple<Ts...> make_compressed_tuple(Ts &&...args) {
    return compressed_tuple<Ts...>(std::forward<Ts>(args)...);
}

}// namespace lu

#endif