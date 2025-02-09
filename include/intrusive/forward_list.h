#ifndef __INTRUSIVE_FORWARD_LIST_H__
#define __INTRUSIVE_FORWARD_LIST_H__

#include "pack_options.h"
#include "slist.h"


namespace lu {
namespace detail {

template<class... Options>
struct make_forward_list_base_hook {
    using pack_options = typename GetPackOptions<SlistHookDefaults, Options...>::type;

    using type = SlistBaseHook<typename pack_options::void_pointer, typename pack_options::tag,
                               pack_options::is_auto_unlink>;
};

template<class ValueType, class... Options>
struct make_forward_list {
    using pack_options = typename GetPackOptions<SlistDefaults, Options...>::type;

    using value_traits = typename pack_options::proto_value_traits::template Apply<ValueType>::type;
    using size_type = typename pack_options::size_type;

    using type = IntrusiveSlist<value_traits, size_type>;
};

}// namespace detail

template<class... Options>
using forward_list_base_hook = typename detail::make_forward_list_base_hook<Options...>::type;

template<class ValueType, class... Options>
using forward_list = typename detail::make_forward_list<ValueType, Options...>::type;

}// namespace lu

#endif