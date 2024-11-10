#ifndef __INTRUSIVE_EMPTY_BASE_TAGS_H__
#define __INTRUSIVE_EMPTY_BASE_TAGS_H__

namespace lu {
    namespace detail {
        class ValueTraitsTag {};

        class BucketTraitsTag {};

        class keyOfValueTag {};

        class KeyHashTag {};

        class KeyEqualTag {};
    }// namespace detail
}// namespace lu

#endif