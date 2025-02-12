#ifndef __INTRUSIVE_GET_TRAITS_H__
#define __INTRUSIVE_GET_TRAITS_H__

#include "base_value_traits.h"
#include "utils.h"

#include <type_traits>


namespace lu {
namespace detail {

HAS_TYPE_ALIAS_MEMBER(is_hook, hook_tags)

template<class ValueType, class ProtoValueTraits>
struct get_value_traits {
    using type = std::conditional_t<is_hook_v<ProtoValueTraits>, HookToValueTraits<ValueType, ProtoValueTraits>,
                                    ProtoValueTraits>;
};

template<class ProtoValueTraits>
struct get_node_traits {
    using type = typename std::conditional_t<is_hook_v<ProtoValueTraits>, typename ProtoValueTraits::hook_tags,
                                             ProtoValueTraits>::node_traits;
};

}// namespace detail
}// namespace lu

#endif