#ifndef __INTRUSIVE_PACK_OPTIONS_H__
#define __INTRUSIVE_PACK_OPTIONS_H__

namespace lu {
    template<class... Types>
    struct typelist {};

    template<class Typelist>
    struct do_pack;

    template<>
    struct do_pack<typelist<>>;

    template<class Head>
    struct do_pack<typelist<Head>> {
        using type = Head;
    };

    template<class Head, class... Tail>
    struct do_pack<typelist<Head, Tail...>> {
        using type = typename Head::template pack<typename do_pack<typelist<Tail...>>::type>;
    };

    template<class Defaults, class... Types>
    struct get_pack_options {
        using type = typename do_pack<typelist<Types..., Defaults>>::type;
    };
}// namespace lu

#endif