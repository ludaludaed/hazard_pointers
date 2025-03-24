#ifndef __INTRUSIVE_HASH_H__
#define __INTRUSIVE_HASH_H__

#include <type_traits>


namespace lu_adl {

template<class ValueType>
std::size_t hash_value(const ValueType &value) {
  return std::hash<ValueType>()(value);
}

}// namespace lu_adl

namespace lu {
namespace detail {

template<class ValueType>
struct hash {
  std::size_t operator()(const ValueType &value) const {
    using lu_adl::hash_value;
    return hash_value(value);
  }
};

}// namespace detail
}// namespace lu

#endif
