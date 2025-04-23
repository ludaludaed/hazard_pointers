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
        CACHE_LINE_ALIGNAS std::atomic<node_marked_ptr> head_{};
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

    using guarded_ptr
            = std::conditional_t<is_key_value, lu::guarded_ptr<ValueType>, lu::guarded_ptr<const ValueType>>;

    using iterator = Iterator<MichaelHashTable, !is_key_value>;
    using const_iterator = Iterator<MichaelHashTable, true>;

public:
    explicit MichaelHashTable(const hasher &hasher = {}, const compare &compare = {},
                              const key_select &key_select = {})
        : hash_(hasher)
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

    bool find(std::atomic<node_marked_ptr> *head, const key_type &key, position &pos) const {
        Backoff backoff;
        return find(key, pos, backoff);
    }

    bool find(std::atomic<node_marked_ptr> *head, const key_type &key, position &pos,
              Backoff &backoff) const {
        return Algo::find(head, key, pos, backoff, key_compare_, key_select_);
    }

    bool insert_node(std::atomic<node_marked_ptr> *head, node_ptr new_node) noexcept {
        Backoff backoff;
        position pos;
        while (true) {
            if (find(head, key_select_(new_node->value), pos, backoff)) {
                return false;
            }
            if (Algo::link(pos, new_node)) {
                return true;
            }
            backoff();
        }
    }

    bool erase(std::atomic<node_marked_ptr> *head, const key_type &key) {
        Backoff backoff;
        position pos;
        while (find(head, key, pos, backoff)) {
            if (Algo::unlink(pos)) {
                return true;
            }
            backoff();
        }
        return false;
    }

private:
    std::array<Bucket, NumOfBuckets> buckets_{};
    NO_UNIQUE_ADDRESS hasher hash_;
    NO_UNIQUE_ADDRESS compare key_compare_;
    NO_UNIQUE_ADDRESS key_select key_select_;
};

}// namespace detail
}// namespace lu

#endif