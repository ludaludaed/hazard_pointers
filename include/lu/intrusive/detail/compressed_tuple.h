#ifndef __INTRUSIVE_COMPRESSED_TUPLE_H__
#define __INTRUSIVE_COMPRESSED_TUPLE_H__

#include <lu/intrusive/detail/typelist.h>

#include <type_traits>
#include <utility>

#if defined(_MSC_VER) && _MSC_VER >= 1900
#define EMPTY_BASES __declspec(empty_bases)
#else
#define EMPTY_BASES
#endif

namespace lu {
namespace detail {

template <std::size_t I, class T>
struct pack;

template <class T, template <std::size_t, class> class Pack>
struct get_indices;

template <std::size_t... Is, class... Ts, template <std::size_t, class> class Pack>
struct get_indices<typelist<Pack<Is, Ts>...>, Pack> {
    using type = std::index_sequence<Is...>;
};

template <class T, template <std::size_t, class> class Pack>
using get_indices_t = typename get_indices<T, Pack>::type;

template <class T, template <std::size_t, class> class Pack>
struct get_types;

template <std::size_t... Is, class... Ts, template <std::size_t, class> class Pack>
struct get_types<typelist<Pack<Is, Ts>...>, Pack> {
    using type = typelist<Ts...>;
};

template <class T, template <std::size_t, class> class Pack>
using get_types_t = typename get_types<T, Pack>::type;

template <std::size_t I, class T, bool = !std::is_empty_v<T> || std::is_final_v<T>>
class EMPTY_BASES tuple_unit;

template <std::size_t I, class T>
class tuple_unit<I, T, true> {
public:
    constexpr tuple_unit() noexcept = default;

    template <class _T, class = std::enable_if_t<std::conjunction_v<
                                std::is_constructible<T, _T>,
                                std::negation<std::is_same<std::remove_cvref_t<_T>, tuple_unit>>>>>
    constexpr tuple_unit(_T &&value) noexcept(std::is_nothrow_constructible_v<T, _T>)
        : data_(std::forward<_T>(value)) {}

    constexpr void swap(tuple_unit &other) noexcept(std::is_nothrow_swappable_v<T>) {
        using std::swap;
        swap(get(), other.get());
    }

    constexpr T &get() noexcept { return data_; }

    constexpr const T &get() const noexcept { return data_; }

private:
    T data_;
};

template <std::size_t I, class T>
class tuple_unit<I, T, false> : private T {
public:
    constexpr tuple_unit() noexcept = default;

    template <class _T, class = std::enable_if_t<std::conjunction_v<
                                std::is_constructible<T, _T>,
                                std::negation<std::is_same<std::remove_cvref_t<_T>, tuple_unit>>>>>
    constexpr tuple_unit(_T &&value) noexcept(std::is_nothrow_constructible_v<T, _T>)
        : T(std::forward<_T>(value)) {}

    constexpr void swap(tuple_unit &other) noexcept(std::is_nothrow_swappable_v<T>) {
        using std::swap;
        swap(get(), other.get());
    }

    constexpr T &get() noexcept { return static_cast<T &>(*this); }

    constexpr const T &get() const noexcept { return static_cast<const T &>(*this); }
};

template <class Is, class... Ts>
struct tuple_base;

template <>
struct tuple_base<std::index_sequence<>> {
    constexpr tuple_base() noexcept = default;

    constexpr void swap(tuple_base &other) noexcept {}
};

template <std::size_t... Is, class... Ts>
struct EMPTY_BASES tuple_base<std::index_sequence<Is...>, Ts...> : tuple_unit<Is, Ts>... {
    static_assert(sizeof...(Is) == sizeof...(Ts),
                  "the number of indexes must be equal to the number of types.");

    constexpr tuple_base() noexcept = default;

    template <std::size_t... _Is, class... _Ts, class... _Us,
              class = std::enable_if_t<
                      std::conjunction_v<std::is_base_of<tuple_unit<_Is, _Ts>, tuple_base>...>>>
    constexpr tuple_base(std::index_sequence<_Is...>, typelist<_Ts...>, _Us &&...args) noexcept(
            std::conjunction_v<std::is_nothrow_constructible<tuple_unit<_Is, _Ts>, _Us>...>)
        : tuple_unit<_Is, _Ts>(std::forward<_Us>(args))... {}

    constexpr void swap(tuple_base &other) noexcept(std::conjunction_v<std::is_nothrow_swappable<Ts>...>) {
        (tuple_unit<Is, Ts>::swap(other), ...);
    }
};

template <class Is, class Ts>
struct make_tuple_base;

template <std::size_t... Is, class... Ts>
struct make_tuple_base<std::index_sequence<Is...>, typelist<Ts...>> {
    using type = tuple_base<std::index_sequence<Is...>, Ts...>;
};

template <class Is, class Ts>
using make_tuple_base_t = typename make_tuple_base<Is, Ts>::type;

}// namespace detail

template <class... Ts>
class compressed_tuple;

template <std::size_t, class>
struct tuple_element;

template <std::size_t I, class... Ts>
struct tuple_element<I, compressed_tuple<Ts...>> {
    using type = get_nth_t<I, typelist<Ts...>>;
};

template <std::size_t I, class T>
using tuple_element_t = typename tuple_element<I, T>::type;

template <class T, class U>
struct tuple_index;

template <class T, class... Us>
struct tuple_index<T, compressed_tuple<Us...>> {
    static constexpr std::size_t value = find_index_v<T, typelist<Us...>>;
};

template <class T, class U>
static constexpr std::size_t tuple_index_v = tuple_index<T, U>::value;

template <class... Ts>
class compressed_tuple {
    using original_indices = std::make_index_sequence<sizeof...(Ts)>;
    using original_types = typelist<Ts...>;
    using original_packs = pack_with_index_t<original_indices, original_types, detail::pack>;

    template <class T1, class T2>
    struct compare;

    template <class T1, std::size_t I1, class T2, std::size_t I2>
    struct compare<detail::pack<I1, T1>, detail::pack<I2, T2>> {
        static constexpr bool value = alignof(T1) > alignof(T2);
    };

    using compressed_packs = sort_t<original_packs, compare>;

    using compressed_indices = detail::get_indices_t<compressed_packs, detail::pack>;
    using compressed_types = detail::get_types_t<compressed_packs, detail::pack>;

    using tuple_base = detail::make_tuple_base_t<compressed_indices, compressed_types>;

    template <class... _Ts>
    struct is_this_tuple {
        static constexpr bool value = false;
    };

    template <class _T>
    struct is_this_tuple<_T> {
        static constexpr bool value = std::is_same_v<std::remove_cvref_t<_T>, compressed_tuple>;
    };

private:
    template <std::size_t I, class... _Ts>
    friend constexpr tuple_element_t<I, compressed_tuple<_Ts...>> &get(compressed_tuple<_Ts...> &) noexcept;

    template <std::size_t I, class... _Ts>
    friend constexpr tuple_element_t<I, compressed_tuple<_Ts...>> &&get(compressed_tuple<_Ts...> &&) noexcept;

    template <std::size_t I, class... _Ts>
    friend constexpr const tuple_element_t<I, compressed_tuple<_Ts...>> &
            get(const compressed_tuple<_Ts...> &) noexcept;

    template <std::size_t I, class... _Ts>
    friend constexpr const tuple_element_t<I, compressed_tuple<_Ts...>> &&
            get(const compressed_tuple<_Ts...> &&) noexcept;

    template <class T, class... _Ts>
    friend constexpr T &get(compressed_tuple<_Ts...> &) noexcept;

    template <class T, class... _Ts>
    friend constexpr T &&get(compressed_tuple<_Ts...> &&) noexcept;

    template <class T, class... _Ts>
    friend constexpr const T &get(const compressed_tuple<_Ts...> &) noexcept;

    template <class T, class... _Ts>
    friend constexpr const T &&get(const compressed_tuple<_Ts...> &&) noexcept;

public:
    constexpr compressed_tuple() = default;

    template <class... _Us, class = std::enable_if_t<std::conjunction_v<
                                    std::bool_constant<sizeof...(Ts) == sizeof...(_Us)>,
                                    std::is_constructible<Ts, _Us>..., std::negation<is_this_tuple<_Us...>>>>>
    constexpr explicit(std::negation_v<std::conjunction<std::is_convertible<_Us, Ts>...>>)
            compressed_tuple(_Us &&...args) noexcept(
                    std::is_nothrow_constructible_v<tuple_base, original_indices, original_types, _Us...>)
        : base_(original_indices(), original_types(), std::forward<_Us>(args)...) {}

    constexpr void swap(compressed_tuple &other) { base_.swap(other.base_); }

    friend constexpr void swap(compressed_tuple &left,
                               compressed_tuple &right) noexcept(std::is_nothrow_swappable_v<tuple_base>) {
        left.swap(right);
    }

private:
    tuple_base base_;
};

template <std::size_t I, class... Ts>
constexpr tuple_element_t<I, compressed_tuple<Ts...>> &get(compressed_tuple<Ts...> &tuple) noexcept {
    using tuple_element = tuple_element_t<I, compressed_tuple<Ts...>>;
    using tuple_unit = detail::tuple_unit<I, tuple_element>;
    return static_cast<tuple_unit &>(tuple.base_).get();
}

template <std::size_t I, class... Ts>
constexpr tuple_element_t<I, compressed_tuple<Ts...>> &&get(compressed_tuple<Ts...> &&tuple) noexcept {
    using tuple_element = tuple_element_t<I, compressed_tuple<Ts...>>;
    using tuple_unit = detail::tuple_unit<I, tuple_element>;
    return std::move(static_cast<tuple_unit &>(tuple.base_).get());
}

template <std::size_t I, class... Ts>
constexpr const tuple_element_t<I, compressed_tuple<Ts...>> &
        get(const compressed_tuple<Ts...> &tuple) noexcept {
    using tuple_element = tuple_element_t<I, compressed_tuple<Ts...>>;
    using tuple_unit = detail::tuple_unit<I, tuple_element>;
    return static_cast<const tuple_unit &>(tuple.base_).get();
}

template <std::size_t I, class... Ts>
constexpr const tuple_element_t<I, compressed_tuple<Ts...>> &&
        get(const compressed_tuple<Ts...> &&tuple) noexcept {
    using tuple_element = tuple_element_t<I, compressed_tuple<Ts...>>;
    using tuple_unit = detail::tuple_unit<I, tuple_element>;
    return std::move(static_cast<const tuple_unit &>(tuple.base_).get());
}

template <class T, class... Ts>
constexpr T &get(compressed_tuple<Ts...> &tuple) noexcept {
    static_assert(lu::num_of_type_v<T, typelist<Ts...>> == 1, "type of T not unique.");
    return get<tuple_index_v<T, compressed_tuple<Ts...>>>(tuple);
}

template <class T, class... Ts>
constexpr T &&get(compressed_tuple<Ts...> &&tuple) noexcept {
    static_assert(lu::num_of_type_v<T, typelist<Ts...>> == 1, "type of T not unique.");
    return get<tuple_index_v<T, compressed_tuple<Ts...>>>(std::move(tuple));
}

template <class T, class... Ts>
constexpr const T &get(const compressed_tuple<Ts...> &tuple) noexcept {
    static_assert(lu::num_of_type_v<T, typelist<Ts...>> == 1, "type of T not unique.");
    return get<tuple_index_v<T, compressed_tuple<Ts...>>>(tuple);
}

template <class T, class... Ts>
constexpr const T &&get(const compressed_tuple<Ts...> &&tuple) noexcept {
    static_assert(lu::num_of_type_v<T, typelist<Ts...>> == 1, "type of T not unique.");
    return get<tuple_index_v<T, compressed_tuple<Ts...>>>(std::move(tuple));
}

template <class... Ts>
compressed_tuple<std::unwrap_ref_decay_t<Ts>...> make_compressed_tuple(Ts &&...args) noexcept {
    return compressed_tuple<std::unwrap_ref_decay_t<Ts>...>(std::forward<Ts>(args)...);
}

}// namespace lu

#endif
