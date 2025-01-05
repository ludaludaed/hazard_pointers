#ifndef __INTRUSIVE_COMPRESSED_TUPLE_H__
#define __INTRUSIVE_COMPRESSED_TUPLE_H__

#include "typelist.h"

#include <tuple>
#include <type_traits>


namespace lu {

template<class>
struct make_tuple;

template<class... Types>
struct make_tuple<typelist<Types...>> {
    using type = std::tuple<Types...>;
};

template<class, std::size_t>
struct get_compressed_index;

template<class T, class... Types>
struct get_compressed_index<typelist<T, Types...>, 0> {
    static constexpr std::size_t value = 0;
};

template<class T, class... Types, std::size_t Index>
struct get_compressed_index<typelist<T, Types...>, Index> {
    static constexpr std::size_t value
            = get_compressed_index<typelist<Types...>, Index - 1>::value + !std::is_empty_v<T>;
};

template<class T>
struct is_not_empty {
    static constexpr bool value = !std::is_empty_v<T>;
};

template<class... Types>
class compressed_tuple {
    template<std::size_t>
    friend auto &&get(const compressed_tuple<Types...> &);

    using original_types = typelist<Types...>;
    using compressed_types = select<original_types, is_not_empty>::type;
    using tuple_type = make_tuple<compressed_types>::type;

public:
    template<class... _Types>
    compressed_tuple(_Types &&...types)
        : data_() {} // TODO

private:
    tuple_type data_;
};

template<std::size_t Index, class... Types>
auto &&get(const compressed_tuple<Types...> &tuple) {
    using return_t = get_nth<typelist<Types...>, Index>;
    if constexpr (std::is_empty_v<return_t>) {
        return return_t{};
    } else {
        return std::get<get_compressed_index<typelist<Types...>, Index>::value>(tuple.data_);
    }
}

}// namespace lu

#endif