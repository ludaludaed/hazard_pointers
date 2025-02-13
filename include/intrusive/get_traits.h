#ifndef __INTRUSIVE_GET_TRAITS_H__
#define __INTRUSIVE_GET_TRAITS_H__

#include "utils.h"

#include <type_traits>


namespace lu {
namespace detail {

HAS_DEFINE(is_hook, hook_tags)
HAS_DEFINE(is_default_hook, is_default_hook_tag)

template<class Option, class DefaultOption>
using GetOrDefault = std::conditional_t<!std::is_void_v<Option>, Option, DefaultOption>;

template<class ValueType, class ProtoValueTraits>
struct GetValueTraits {
    // TODO
};

template<class ProtoValueTraits>
struct GetNodeTraits {
    // TODO
};

}// namespace detail
}// namespace lu

#endif