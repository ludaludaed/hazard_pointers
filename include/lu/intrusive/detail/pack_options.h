#ifndef __INTRUSIVE_PACK_OPTIONS_H__
#define __INTRUSIVE_PACK_OPTIONS_H__

namespace lu {
namespace detail {

template <class... Args>
struct typelist;

template <class Typelist>
struct DoPack;

template <>
struct DoPack<typelist<>>;

template <class Head>
struct DoPack<typelist<Head>> {
    using type = Head;
};

template <class Head, class... Tail>
struct DoPack<typelist<Head, Tail...>> {
    using type = typename Head::template pack<typename DoPack<typelist<Tail...>>::type>;
};

template <class Defaults, class... Types>
struct GetPackOptions {
    using type = typename DoPack<typelist<Types..., Defaults>>::type;
};

}// namespace detail
}// namespace lu

#endif
