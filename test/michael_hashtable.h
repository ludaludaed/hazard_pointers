#ifndef __MICHAEL_HASH_TABLE_H__
#define __MICHAEL_HASH_TABLE_H__

#include <lu/detail/utils.h>
#include <lu/hazard_pointer.h>
#include <lu/intrusive/detail/utils.h>
#include <lu/utils/backoff.h>
#include <lu/utils/marked_ptr.h>

#include "ordered_list.h"

#include <array>
#include <atomic>
#include <utility>

namespace lu {
namespace detail {

template <class ValueType>
struct MichaelHashTableNode : public lu::hazard_pointer_obj_base<MichaelHashTableNode<ValueType>> {
    template <class... Args>
    explicit MichaelHashTableNode(Args &&...args)
        : value(std::forward<Args>(args)...) {}

    ValueType value;
    std::atomic<lu::marked_ptr<MichaelHashTableNode>> next{};
};

template <class ValueType>
struct MichaelHashTableNodeTraits {
    using node = MichaelHashTableNode<ValueType>;
    using node_ptr = node *;
    using node_marked_ptr = lu::marked_ptr<node>;
};

template <class NodeTraits>
struct MichaelHashTableAlgo : public OrderedListAlgo<NodeTraits> {};

template <class ValueType, class KeyHash, class KeyCompare, class KeySelect, class Backoff,
          std::size_t NumOfBuckets>
class MichaelHashTable {
    using node_traits = MichaelHashTableNodeTraits<ValueType>;

    using node = typename node_traits::node;
    using node_ptr = typename node_traits::node_ptr;
    using node_marked_ptr = typename node_traits::node_marked_ptr;

    using Algo = MichaelHashTableAlgo<node_traits>;
    using position = typename Algo::position;

    struct Bucket {
        CACHE_LINE_ALIGNAS std::atomic<node_marked_ptr> head{};
    };

private:
    template <class Types, bool IsConst>
    class Iterator;

public:
    using value_type = ValueType;
    using key_type = typename KeySelect::type;

    using difference_type = std::ptrdiff_t;
    using pointer = value_type *;
    using const_pointer = const value_type *;
    using reference = value_type &;
    using const_reference = const value_type &;

    using compare = KeyCompare;
    using hasher = KeyHash;
    using key_select = KeySelect;

    static constexpr bool is_key_value = !std::is_same_v<value_type, key_type>;

    using accessor
            = std::conditional_t<is_key_value, lu::guarded_ptr<ValueType>, lu::guarded_ptr<const ValueType>>;

    using iterator = Iterator<MichaelHashTable, !is_key_value>;
    using const_iterator = Iterator<MichaelHashTable, true>;

public:
    explicit MichaelHashTable(const hasher &hasher = {}, const compare &compare = {},
                              const key_select &key_select = {})
        : key_hash_(hasher)
        , key_compare_(compare)
        , key_select_(key_select) {}

    MichaelHashTable(const MichaelHashTable &) = delete;

    MichaelHashTable(MichaelHashTable &&) = delete;

    ~MichaelHashTable() {
        for (auto it = buckets_.begin(); it != buckets_.end(); ++it) {
            auto current = it->head_.load();
            while (current) {
                auto next = current->next.load();
                delete current.get();
                current = next;
            }
        }
    }

private:
    static std::size_t get_bucket_idx(std::size_t hash) noexcept {
        if constexpr ((NumOfBuckets & (NumOfBuckets - 1)) == 0) {
            return hash & (NumOfBuckets - 1);
        } else {
            return hash % NumOfBuckets;
        }
    }

public:
    bool insert(const value_type &value);

    bool insert(value_type &&value);

    template <class... Args>
    bool emplace(Args &&...args);

    bool erase(const key_type &key);

    accessor extract(const key_type &key);

    iterator find(const key_type &key);

    const_iterator find(const key_type &key) const;

    bool contains(const key_type &key) const;

    iterator begin();

    iterator end();

    const_iterator begin() const;

    const_iterator end() const;

private:
    std::array<Bucket, NumOfBuckets> buckets_{};
    NO_UNIQUE_ADDRESS hasher key_hash_;
    NO_UNIQUE_ADDRESS compare key_compare_;
    NO_UNIQUE_ADDRESS key_select key_select_;
};

}// namespace detail
}// namespace lu

#endif