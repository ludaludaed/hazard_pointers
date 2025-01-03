#ifndef __INTRUSIVE_APPLIERS_H__
#define __INTRUSIVE_APPLIERS_H__

#include "base_value_traits.h"

namespace lu {
namespace detail {

template<class HookType>
struct BaseHookApplier {
    template<class ValueType>
    struct Apply {
        using type = typename HookToValueTraits<ValueType, HookType>::type;
    };
};

template<class ValueTraits>
struct ValueTraitsApplier {
    template<class... Dummy>
    struct Apply {
        using type = ValueTraits;
    };
};

template<class BucketTraits>
struct BucketTraitsApplier {
    template<class... Dummy>
    struct Apply {
        using type = BucketTraits;
    };
};

}// namespace detail
}// namespace lu

#endif