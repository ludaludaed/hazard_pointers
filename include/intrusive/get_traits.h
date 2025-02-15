#ifndef __INTRUSIVE_GET_TRAITS_H__
#define __INTRUSIVE_GET_TRAITS_H__

#include "intrusive/base_value_traits.h"
#include "utils.h"

#include <type_traits>


namespace lu {
namespace detail {

HAS_DEFINE(is_hook, hook_tags)
HAS_DEFINE(is_default_hook, is_default_hook_tag)

template<class Option, class DefaultOption>
using GetOrDefault = std::conditional_t<!std::is_void_v<Option>, Option, DefaultOption>;

enum ProtoValueTraitsType {
    IS_DEFAULT_HOOK = 0,
    IS_HOOK,
    IS_VALUE_TRAITS,
};

template<class ProtoValueTraits>
struct ProtoValueTraitsTypeDispatch {
    static constexpr ProtoValueTraitsType value = is_default_hook_v<ProtoValueTraits> ? IS_DEFAULT_HOOK
                                                  : is_hook_v<ProtoValueTraits>       ? IS_HOOK
                                                                                      : IS_VALUE_TRAITS;
};

template<class ValueType, class ProtoValueTraits,
         ProtoValueTraitsType = ProtoValueTraitsTypeDispatch<ProtoValueTraits>::value>
struct GetValueTraits;

template<class ValueType, class ProtoValueTraits>
struct GetValueTraits<ValueType, ProtoValueTraits, IS_DEFAULT_HOOK> {
    using hook_type = typename ProtoValueTraits::template GetDefaultHook<ValueType>::type;
    using type = typename HookToValueTraits<ValueType, hook_type>::type;
};

template<class ValueType, class ProtoValueTraits>
struct GetValueTraits<ValueType, ProtoValueTraits, IS_HOOK> {
    using type = typename HookToValueTraits<ValueType, ProtoValueTraits>::type;
};

template<class ValueType, class ProtoValueTraits>
struct GetValueTraits<ValueType, ProtoValueTraits, IS_VALUE_TRAITS> {
    using type = ProtoValueTraits;
};

template<class ProtoValueTraits, ProtoValueTraitsType = ProtoValueTraitsTypeDispatch<ProtoValueTraits>::value>
struct GetNodeTraits;

template<class ProtoValueTraits>
struct GetNodeTraits<ProtoValueTraits, IS_HOOK> {
    using type = typename ProtoValueTraits::hook_tags::node_traits;
};

template<class ProtoValueTraits>
struct GetNodeTraits<ProtoValueTraits, IS_VALUE_TRAITS> {
    using type = typename ProtoValueTraits::node_traits;
};

}// namespace detail
}// namespace lu

#endif