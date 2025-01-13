#ifndef __INTRUSIVE_COMPRESSED_TUPLE_H__
#define __INTRUSIVE_COMPRESSED_TUPLE_H__

#include "intrusive/typelist.h"
#include "typelist.h"

#include <tuple>
#include <type_traits>
#include <utility>

#if defined(_MSC_VER) && _MSC_VER >= 1900
#define EMPTY_BASES __declspec(empty_bases)
#else
#define EMPTY_BASES
#endif

namespace lu {
namespace detail {

template<std::size_t I, class T>
struct pack;

template<class T, template<std::size_t, class> class Pack>
struct get_indices;

template<std::size_t... Is, class... Ts, template<std::size_t, class> class Pack>
struct get_indices<typelist<Pack<Is, Ts>...>, Pack> {
    using type = std::index_sequence<Is...>;
};
template<class T, template<std::size_t, class> class Pack>
using get_indices_t = typename get_indices<T, Pack>::type;

template<class T, template<std::size_t, class> class Pack>
struct get_types;

template<std::size_t... Is, class... Ts, template<std::size_t, class> class Pack>
struct get_types<typelist<Pack<Is, Ts>...>, Pack> {
    using type = typelist<Ts...>;
};

template<class T, template<std::size_t, class> class Pack>
using get_types_t = typename get_types<T, Pack>::type;

template<std::size_t I, class T, bool = !std::is_empty_v<T> || std::is_final_v<T>>
class EMPTY_BASES tuple_unit;

template<std::size_t I, class T>
class tuple_unit<I, T, true> {
public:
    constexpr tuple_unit() = default;

    template<class _T,
             class = std::enable_if_t<std::conjunction_v<
                     std::negation<std::is_same<std::remove_cvref_t<_T>, tuple_unit>>, std::is_constructible<T, _T>>>>
    constexpr tuple_unit(_T &&value)
        : data_(std::forward<_T>(value)) {}

    constexpr void swap(tuple_unit &other) {
        using std::swap;
        swap(get(), other.get());
    }

    constexpr T &get() {
        return data_;
    }

    constexpr const T &get() const {
        return data_;
    }

private:
    T data_;
};

template<std::size_t I, class T>
class tuple_unit<I, T, false> : private T {
public:
    constexpr tuple_unit() = default;

    template<class _T,
             class = std::enable_if_t<std::conjunction_v<
                     std::negation<std::is_same<std::remove_cvref_t<_T>, tuple_unit>>, std::is_constructible<T, _T>>>>
    constexpr tuple_unit(_T &&value)
        : T(std::forward<_T>(value)) {}

    constexpr void swap(tuple_unit &other) {
        using std::swap;
        swap(get(), other.get());
    }

    constexpr T &get() {
        return static_cast<T &>(*this);
    }

    constexpr const T &get() const {
        return static_cast<const T &>(*this);
    }
};

template<class Is, class... Ts>
struct tuple_base;

template<>
struct tuple_base<std::index_sequence<>> {
    constexpr tuple_base() = default;

    constexpr void swap(tuple_base &other) {}
};

template<std::size_t... Is, class... Ts>
struct EMPTY_BASES tuple_base<std::index_sequence<Is...>, Ts...> : tuple_unit<Is, Ts>... {
    static_assert(sizeof...(Is) == sizeof...(Ts), "the number of indexes must be equal to the number of types.");

    constexpr tuple_base() = default;

    template<class... _Ts>
    constexpr tuple_base(_Ts &&...args)
        : tuple_unit<Is, Ts>(std::forward<_Ts>(args))... {}

    constexpr void swap(tuple_base &other) {
        (tuple_unit<Is, Ts>::swap(other), ...);
    }
};

template<class Is, class Ts>
struct make_tuple_base;

template<std::size_t... Is, class... Ts>
struct make_tuple_base<std::index_sequence<Is...>, typelist<Ts...>> {
    using type = tuple_base<std::index_sequence<Is...>, Ts...>;
};

template<class Is, class Ts>
using make_tuple_base_t = typename make_tuple_base<Is, Ts>::type;

}// namespace detail

template<class... Ts>
class compressed_tuple;

template<std::size_t, class>
struct tuple_element;

template<std::size_t I, class... Ts>
struct tuple_element<I, compressed_tuple<Ts...>> {
    using type = get_nth_t<I, typelist<Ts...>>;
};

template<std::size_t I, class T>
using tuple_element_t = typename tuple_element<I, T>::type;

template<class T, class U>
struct tuple_index;

template<class T, class... Us>
struct tuple_index<T, compressed_tuple<Us...>> {
    static constexpr std::size_t value = find_index_v<T, typelist<Us...>>;
};

template<class T, class U>
static constexpr std::size_t tuple_index_v = tuple_index<T, U>::value;

template<class... Ts>
class compressed_tuple {
    using original_packs = pack_with_index_t<std::make_index_sequence<sizeof...(Ts)>, typelist<Ts...>, detail::pack>;

    template<class T1, class T2>
    struct compare;

    template<class T1, std::size_t I1, class T2, std::size_t I2>
    struct compare<detail::pack<I1, T1>, detail::pack<I2, T2>> {
        static constexpr bool value = alignof(T1) > alignof(T2);
    };

    using compressed_packs = sort_t<original_packs, compare>;

    using compressed_indices = detail::get_indices_t<compressed_packs, detail::pack>;
    using compressed_types = detail::get_types_t<compressed_packs, detail::pack>;

    using tuple_base = detail::make_tuple_base_t<compressed_indices, compressed_types>;

    template<class... _Ts>
    struct is_this_tuple {
        static constexpr bool value = false;
    };

    template<class _T>
    struct is_this_tuple<_T> {
        static constexpr bool value = std::is_same_v<std::remove_cvref_t<_T>, compressed_tuple>;
    };

private:
    template<std::size_t I, class... _Ts>
    friend constexpr tuple_element_t<I, compressed_tuple<_Ts...>> &get(compressed_tuple<_Ts...> &);

    template<std::size_t I, class... _Ts>
    friend constexpr tuple_element_t<I, compressed_tuple<_Ts...>> &&get(compressed_tuple<_Ts...> &&);

    template<std::size_t I, class... _Ts>
    friend constexpr const tuple_element_t<I, compressed_tuple<_Ts...>> &get(const compressed_tuple<_Ts...> &);

    template<std::size_t I, class... _Ts>
    friend constexpr const tuple_element_t<I, compressed_tuple<_Ts...>> &&get(const compressed_tuple<_Ts...> &&);

    template<class T, class... _Ts>
    friend constexpr T &get(compressed_tuple<_Ts...> &);

    template<class T, class... _Ts>
    friend constexpr T &&get(compressed_tuple<_Ts...> &&);

    template<class T, class... _Ts>
    friend constexpr const T &get(const compressed_tuple<_Ts...> &);

    template<class T, class... _Ts>
    friend constexpr const T &&get(const compressed_tuple<_Ts...> &&);

private:
    template<class Args, std::size_t... Indices>
    constexpr compressed_tuple(Args args, std::index_sequence<Indices...>)
        : base_(std::forward<std::tuple_element_t<Indices, Args>>(std::get<Indices>(args))...) {}

public:
    constexpr compressed_tuple() = default;

    template<class... _Ts,
             class = std::enable_if_t<std::conjunction_v<std::bool_constant<sizeof...(Ts) == sizeof...(_Ts)>,
                                                         std::negation<is_this_tuple<_Ts...>>>>>
    constexpr explicit(std::negation_v<std::conjunction<std::is_convertible<_Ts, Ts>...>>) compressed_tuple(_Ts &&...ts)
        : compressed_tuple(std::forward_as_tuple(std::forward<_Ts>(ts)...), compressed_indices()) {}

    constexpr void swap(compressed_tuple &other) {
        base_.swap(other.base_);
    }

    friend constexpr void swap(compressed_tuple &left, compressed_tuple &right) {
        left.swap(right);
    }

private:
    tuple_base base_;
};

template<std::size_t I, class... Ts>
constexpr tuple_element_t<I, compressed_tuple<Ts...>> &get(compressed_tuple<Ts...> &tuple) {
    using tuple_element = tuple_element_t<I, compressed_tuple<Ts...>>;
    using tuple_unit = detail::tuple_unit<I, tuple_element>;
    return static_cast<tuple_unit &>(tuple.base_).get();
}

template<std::size_t I, class... Ts>
constexpr tuple_element_t<I, compressed_tuple<Ts...>> &&get(compressed_tuple<Ts...> &&tuple) {
    using tuple_element = tuple_element_t<I, compressed_tuple<Ts...>>;
    using tuple_unit = detail::tuple_unit<I, tuple_element>;
    return std::move(static_cast<tuple_unit &>(tuple.base_).get());
}

template<std::size_t I, class... Ts>
constexpr const tuple_element_t<I, compressed_tuple<Ts...>> &get(const compressed_tuple<Ts...> &tuple) {
    using tuple_element = tuple_element_t<I, compressed_tuple<Ts...>>;
    using tuple_unit = detail::tuple_unit<I, tuple_element>;
    return static_cast<const tuple_unit &>(tuple.base_).get();
}

template<std::size_t I, class... Ts>
constexpr const tuple_element_t<I, compressed_tuple<Ts...>> &&get(const compressed_tuple<Ts...> &&tuple) {
    using tuple_element = tuple_element_t<I, compressed_tuple<Ts...>>;
    using tuple_unit = detail::tuple_unit<I, tuple_element>;

    return std::move(static_cast<const tuple_unit &>(tuple.base_).get());
}

template<class T, class... Ts>
constexpr T &get(compressed_tuple<Ts...> &tuple) {
    static_assert(lu::num_of_type_v<T, typelist<Ts...>> == 1, "type of T not unique.");
    return get<tuple_index_v<T, compressed_tuple<Ts...>>>(tuple);
}

template<class T, class... Ts>
constexpr T &&get(compressed_tuple<Ts...> &&tuple) {
    static_assert(lu::num_of_type_v<T, typelist<Ts...>> == 1, "type of T not unique.");
    return get<tuple_index_v<T, compressed_tuple<Ts...>>>(std::move(tuple));
}

template<class T, class... Ts>
constexpr const T &get(const compressed_tuple<Ts...> &tuple) {
    static_assert(lu::num_of_type_v<T, typelist<Ts...>> == 1, "type of T not unique.");
    return get<tuple_index_v<T, compressed_tuple<Ts...>>>(tuple);
}

template<class T, class... Ts>
constexpr const T &&get(const compressed_tuple<Ts...> &&tuple) {
    static_assert(lu::num_of_type_v<T, typelist<Ts...>> == 1, "type of T not unique.");
    return get<tuple_index_v<T, compressed_tuple<Ts...>>>(std::move(tuple));
}

template<class... Ts>
compressed_tuple<std::unwrap_ref_decay_t<Ts>...> make_compressed_tuple(Ts &&...args) {
    return compressed_tuple<std::unwrap_ref_decay_t<Ts>...>(std::forward<Ts>(args)...);
}

}// namespace lu

#endif