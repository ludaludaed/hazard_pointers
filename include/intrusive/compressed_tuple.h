#ifndef __INTRUSIVE_COMPRESSED_TUPLE_H__
#define __INTRUSIVE_COMPRESSED_TUPLE_H__

#include "intrusive/typelist.h"
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

template<class T>
struct is_empty {
    static constexpr bool value = std::is_empty_v<T>;
};

template<class T1, class T2>
struct aligment_compare {
    static constexpr bool value = alignof(T1) > alignof(T2);
};

template<std::size_t I, class T>
struct pack;

template<std::size_t I, class T>
struct is_not_empty<pack<I, T>> {
    static constexpr bool value = is_not_empty<T>::value;
};

template<std::size_t I, class T>
struct is_empty<pack<I, T>> {
    static constexpr bool value = is_empty<T>::value;
};

template<std::size_t I1, std::size_t I2, class T1, class T2>
struct aligment_compare<pack<I1, T1>, pack<I2, T2>> {
    static constexpr bool value = aligment_compare<T1, T2>::value;
};

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

template<std::size_t I, class T>
struct tuple_unit {
    constexpr tuple_unit() = default;

    template<class _T>
    constexpr tuple_unit(_T &&value)
        : data(std::forward<_T>(value)) {}

    void swap(tuple_unit &other) {
        std::swap(data, other.data);
    }

    T data;
};

template<std::size_t I, class T>
constexpr T &get(tuple_unit<I, T> &unit) {
    return unit.data;
}

template<std::size_t I, class T>
constexpr T &&get(tuple_unit<I, T> &&unit) {
    return std::move(unit.data);
}

template<std::size_t I, class T>
constexpr const T &get(const tuple_unit<I, T> &unit) {
    return unit.data;
}

template<std::size_t I, class T>
constexpr const T &&get(const tuple_unit<I, T> &&unit) {
    return std::move(unit.data);
}

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
    constexpr tuple_base(_Ts &&...args)
        : tuple_unit<Is, Ts>(std::forward<_Ts>(args))... {}

    void swap(tuple_base &other) {
        ((tuple_unit<Is, Ts>::swap(other), 0), ...);
    }
};

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

    using not_empty_packs = sort_t<select_t<original_packs, detail::is_not_empty>, detail::aligment_compare>;

    using not_empty_indices = detail::get_indices_t<not_empty_packs, detail::pack>;
    using not_empty_types = detail::get_types_t<not_empty_packs, detail::pack>;

    using empty_packs = select_t<original_packs, detail::is_empty>;

    using empty_indices = detail::get_indices_t<empty_packs, detail::pack>;
    using empty_types = detail::get_types_t<empty_packs, detail::pack>;

    using tuple_base = detail::tuple_base<not_empty_indices, not_empty_types>;

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
    compressed_tuple() = default;

    template<class... _Ts>
    constexpr compressed_tuple(_Ts &&...ts)
        : compressed_tuple(std::forward_as_tuple(std::forward<_Ts>(ts)...), not_empty_indices{}) {}

    void swap(compressed_tuple &other) {
        base_.swap(other.base_);
    }

    friend void swap(compressed_tuple &left, compressed_tuple &right) {
        left.swap(right);
    }

private:
    tuple_base base_;
};

template<std::size_t I, class... Ts>
constexpr tuple_element_t<I, compressed_tuple<Ts...>> &get(compressed_tuple<Ts...> &tuple) {
    using tuple_element = tuple_element_t<I, compressed_tuple<Ts...>>;
    using tuple_unit = detail::tuple_unit<I, tuple_element>;
    if constexpr (std::is_empty_v<tuple_element>) {
        auto ptr = reinterpret_cast<tuple_element *>(&tuple.base_);
        return static_cast<tuple_element &>(*ptr);
    } else {
        return detail::get(static_cast<tuple_unit &>(tuple.base_));
    }
}

template<std::size_t I, class... Ts>
constexpr tuple_element_t<I, compressed_tuple<Ts...>> &&get(compressed_tuple<Ts...> &&tuple) {
    using tuple_element = tuple_element_t<I, compressed_tuple<Ts...>>;
    using tuple_unit = detail::tuple_unit<I, tuple_element>;
    if constexpr (std::is_empty_v<tuple_element>) {
        auto ptr = reinterpret_cast<tuple_element *>(&tuple.base_);
        return static_cast<tuple_element &&>(*ptr);
    } else {
        return detail::get(static_cast<tuple_unit &&>(tuple.base_));
    }
}

template<std::size_t I, class... Ts>
constexpr const tuple_element_t<I, compressed_tuple<Ts...>> &get(const compressed_tuple<Ts...> &tuple) {
    using tuple_element = tuple_element_t<I, compressed_tuple<Ts...>>;
    using tuple_unit = detail::tuple_unit<I, tuple_element>;
    if constexpr (std::is_empty_v<tuple_element>) {
        auto ptr = reinterpret_cast<const tuple_element *>(&tuple.base_);
        return static_cast<const tuple_element &>(*ptr);
    } else {
        return detail::get(static_cast<const tuple_unit &>(tuple.base_));
    }
}

template<std::size_t I, class... Ts>
constexpr const tuple_element_t<I, compressed_tuple<Ts...>> &&get(const compressed_tuple<Ts...> &&tuple) {
    using tuple_element = tuple_element_t<I, compressed_tuple<Ts...>>;
    using tuple_unit = detail::tuple_unit<I, tuple_element>;
    if constexpr (std::is_empty_v<tuple_element>) {
        auto ptr = reinterpret_cast<const tuple_element *>(&tuple.base_);
        return static_cast<const tuple_element &&>(*ptr);
    } else {
        return detail::get(static_cast<const tuple_unit &&>(tuple.base_));
    }
}

template<class T, class... Ts>
constexpr T &get(compressed_tuple<Ts...> &tuple) {
    using tuple_unit = detail::tuple_unit<tuple_index_v<T, compressed_tuple<Ts...>>, T>;
    static_assert(lu::num_of_type_v<T, typelist<Ts...>> == 1, "type of T not unique.");
    if constexpr (std::is_empty_v<T>) {
        auto ptr = reinterpret_cast<T *>(&tuple.base_);
        return static_cast<T &>(*ptr);
    } else {
        return detail::get(static_cast<tuple_unit &>(tuple.base_));
    }
}

template<class T, class... Ts>
constexpr T &&get(compressed_tuple<Ts...> &&tuple) {
    using tuple_unit = detail::tuple_unit<tuple_index_v<T, compressed_tuple<Ts...>>, T>;
    static_assert(lu::num_of_type_v<T, typelist<Ts...>> == 1, "type of T not unique.");
    if constexpr (std::is_empty_v<T>) {
        auto ptr = reinterpret_cast<T *>(&tuple.base_);
        return static_cast<T &&>(*ptr);
    } else {
        return detail::get(static_cast<tuple_unit &&>(tuple.base_));
    }
}

template<class T, class... Ts>
constexpr const T &get(const compressed_tuple<Ts...> &tuple) {
    using tuple_unit = detail::tuple_unit<tuple_index_v<T, compressed_tuple<Ts...>>, T>;
    static_assert(lu::num_of_type_v<T, typelist<Ts...>> == 1, "type of T not unique.");
    if constexpr (std::is_empty_v<T>) {
        auto ptr = reinterpret_cast<const T *>(&tuple.base_);
        return static_cast<const T &>(*ptr);
    } else {
        return detail::get(static_cast<const tuple_unit &>(tuple.base_));
    }
}

template<class T, class... Ts>
constexpr const T &&get(const compressed_tuple<Ts...> &&tuple) {
    using tuple_unit = detail::tuple_unit<tuple_index_v<T, compressed_tuple<Ts...>>, T>;
    static_assert(lu::num_of_type_v<T, typelist<Ts...>> == 1, "type of T not unique.");
    if constexpr (std::is_empty_v<T>) {
        auto ptr = reinterpret_cast<const T *>(&tuple.base_);
        return static_cast<const T &&>(*ptr);
    } else {
        return detail::get(static_cast<const tuple_unit &&>(tuple.base_));
    }
}

template<class... Ts>
compressed_tuple<std::unwrap_ref_decay_t<Ts>...> make_compressed_tuple(Ts &&...args) {
    return compressed_tuple<std::unwrap_ref_decay_t<Ts>...>(std::forward<Ts>(args)...);
}

}// namespace lu

#endif