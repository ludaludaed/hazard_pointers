#ifndef __INTRUSIVE_UNORDERED_SET_H__
#define __INTRUSIVE_UNORDERED_SET_H__

#include "hash.h"
#include "hashtable.h"
#include "pack_options.h"
#include "utils.h"

#include <functional>


namespace lu {
namespace detail {

template<class... Options>
struct make_unordered_set_base_hook {
    using pack_options = typename GetPackOptions<HashtableHookDefaults, Options...>::type;

    using type = HashtableBaseHook<typename pack_options::void_pointer, typename pack_options::tag,
                                   pack_options::store_hash, pack_options::is_auto_unlink>;
};

template<class ValueType, class... Options>
struct make_unordered_set {
    using pack_options = typename GetPackOptions<HashtableDefaults, Options...>::type;

    using key_of_value = get_or_default<typename pack_options::key_of_value, DefaultKeyOfValue<ValueType>>;
    using key_type = typename key_of_value::type;
    using hash = get_or_default<typename pack_options::hash, detail::hash<key_type>>;
    using equal = get_or_default<typename pack_options::equal, std::equal_to<key_type>>;

    using size_type = typename pack_options::size_type;
    using flags = HashtableFlags<pack_options::is_power_2_buckets, false>;

    using value_traits = typename pack_options::proto_value_traits::template Apply<ValueType>::type;
    using bucket_traits = typename pack_options::proto_bucket_traits::template Apply<value_traits, size_type>::type;

    using type = IntrusiveHashtable<value_traits, bucket_traits, key_of_value, hash, equal, size_type, flags>;
};

template<class ValueType, class... Options>
struct make_unordered_multiset {
    using pack_options = typename GetPackOptions<HashtableDefaults, Options...>::type;

    using key_of_value = get_or_default<typename pack_options::key_of_value, DefaultKeyOfValue<ValueType>>;
    using key_type = typename key_of_value::type;
    using hash = get_or_default<typename pack_options::hash, detail::hash<key_type>>;
    using equal = get_or_default<typename pack_options::equal, std::equal_to<key_type>>;

    using size_type = typename pack_options::size_type;
    using flags = HashtableFlags<pack_options::is_power_2_buckets, true>;

    using value_traits = typename pack_options::proto_value_traits::template Apply<ValueType>::type;
    using bucket_traits = typename pack_options::proto_bucket_traits::template Apply<value_traits, size_type>::type;

    using type = IntrusiveHashtable<value_traits, bucket_traits, key_of_value, hash, equal, size_type, flags>;
};

template<class... Options>
struct make_unordered_bucket_type {
    struct empty {};
    using pack_options = typename GetPackOptions<empty, Options...>::type;
    using value_traits = typename pack_options::proto_value_traits::template Apply<void>::type;
    using node_traits = typename value_traits::node_traits;
    using type = BucketValue<node_traits>;
};

}// namespace detail

template<class... Options>
using unordered_set_base_hook = typename detail::make_unordered_set_base_hook<Options...>::type;

template<class ValueType, class... Options>
using unordered_set = typename detail::make_unordered_set<ValueType, Options...>::type;

template<class ValueType, class... Options>
using unordered_multiset = typename detail::make_unordered_multiset<ValueType, Options...>::type;

template<class... Options>
using unordered_bucket_type = typename detail::make_unordered_bucket_type<Options...>::type;

}// namespace lu

#endif