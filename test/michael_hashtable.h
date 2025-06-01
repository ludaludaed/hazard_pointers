#ifndef __MICHAEL_HASH_TABLE_H__
#define __MICHAEL_HASH_TABLE_H__

#include <lu/detail/utils.h>
#include <lu/hazard_pointer.h>
#include <lu/intrusive/detail/get_traits.h>
#include <lu/intrusive/detail/utils.h>
#include <lu/utils/backoff.h>
#include <lu/utils/marked_ptr.h>

#include "ordered_list.h"

#include <array>
#include <memory>
#include <optional>
#include <utility>


namespace lu {
namespace detail {

template <class Types, bool IsConst>
class MichaelHashTableIterator {
    template <class, class, class, class, class, std::size_t>
    friend class MichaelHashTable;

    class DummyNonConstIter;
    using NonConstIter = std::conditional_t<IsConst, MichaelHashTableIterator<Types, false>,
                                            DummyNonConstIter>;

    using buckets = typename Types::buckets;

    using node_ptr = typename Types::node_ptr;
    using node_marked_ptr = typename Types::node_marked_ptr;
    using node_accessor = typename Types::node_accessor;

    using position = typename Types::position;

public:
    using value_type = typename Types::value_type;
    using difference_type = typename Types::difference_type;
    using pointer
            = std::conditional_t<IsConst, typename Types::const_pointer, typename Types::pointer>;
    using reference = std::conditional_t<IsConst, typename Types::const_reference,
                                         typename Types::reference>;
    using iterator_category = std::forward_iterator_tag;

private:
    MichaelHashTableIterator(std::size_t bucket_idx, const buckets *buckets) noexcept
        : bucket_idx_(bucket_idx)
        , buckets_(buckets) {
        current_node_ = (*buckets_)[bucket_idx_]->front();
        if (!current_node_) {
            move_to_next_bucket();
        }
    }

    MichaelHashTableIterator(node_accessor current_node, std::size_t bucket_idx,
                             const buckets *buckets) noexcept
        : bucket_idx_(bucket_idx)
        , buckets_(buckets)
        , current_node_(std::move(current_node)) {}

public:
    MichaelHashTableIterator() = default;

    MichaelHashTableIterator(const MichaelHashTableIterator &other) noexcept
        : bucket_idx_(other.bucket_idx_)
        , buckets_(other.buckets_) {
        if (other.current_node_) {
            auto guard = lu::make_hazard_pointer();
            guard.reset_protection(other.current_node_.get());
            current_node_ = node_accessor(std::move(guard), other.current_node_.get());
        }
    }

    MichaelHashTableIterator(const NonConstIter &other) noexcept
        : bucket_idx_(other.bucket_idx_)
        , buckets_(other.buckets_) {
        if (other.current_node_) {
            auto guard = lu::make_hazard_pointer();
            guard.reset_protection(other.current_node_.get());
            current_node_ = node_accessor(std::move(guard), other.current_node_.get());
        }
    }

    MichaelHashTableIterator(MichaelHashTableIterator &&other) noexcept {
        swap(other);
    }

    MichaelHashTableIterator(NonConstIter &&other) noexcept {
        swap(other);
    }

    MichaelHashTableIterator &operator=(const MichaelHashTableIterator &other) noexcept {
        MichaelHashTableIterator temp(other);
        swap(temp);
        return *this;
    }

    MichaelHashTableIterator &operator=(const NonConstIter &other) noexcept {
        MichaelHashTableIterator temp(other);
        swap(temp);
        return *this;
    }

    MichaelHashTableIterator &operator=(MichaelHashTableIterator &&other) noexcept {
        MichaelHashTableIterator temp(std::move(other));
        swap(temp);
        return *this;
    }

    MichaelHashTableIterator &operator=(NonConstIter &&other) noexcept {
        MichaelHashTableIterator temp(std::move(other));
        swap(temp);
        return *this;
    }

    MichaelHashTableIterator &operator++() noexcept {
        increment();
        return *this;
    }

    MichaelHashTableIterator operator++(int) noexcept {
        MichaelHashTableIterator copy(*this);
        increment();
        return copy;
    }

    reference operator*() const noexcept {
        return *this->operator->();
    }

    pointer operator->() const noexcept {
        return &current_node_->value;
    }

    friend bool operator==(const MichaelHashTableIterator &left,
                           const MichaelHashTableIterator &right) {
        return left.current_node_ == right.current_node_;
    }

    friend bool operator!=(const MichaelHashTableIterator &left,
                           const MichaelHashTableIterator &right) {
        return !(left == right);
    }

    void swap(MichaelHashTableIterator &other) {
        using std::swap;
        swap(current_node_, other.current_node_);
        swap(bucket_idx_, other.bucket_idx_);
        swap(buckets_, other.buckets_);
    }

    friend void swap(MichaelHashTableIterator &left, MichaelHashTableIterator &right) {
        left.swap(right);
    }

private:
    void increment() noexcept {
        position pos;
        auto next = pos.next_guard.protect(current_node_->next,
                                           [](node_marked_ptr ptr) { return ptr.get(); });
        if (next.is_marked()) {
            auto &bucket = (*buckets_)[bucket_idx_];
            bucket->find(bucket->key_select_(current_node_->value), pos);
            current_node_ = node_accessor(std::move(pos.cur_guard), pos.cur);
        } else {
            current_node_ = node_accessor(std::move(pos.next_guard), next);
        }
        if (!current_node_) {
            move_to_next_bucket();
        }
    }

    void move_to_next_bucket() noexcept {
        while (!current_node_ && bucket_idx_ + 1 < buckets_->size()) {
            bucket_idx_++;
            current_node_ = (*buckets_)[bucket_idx_]->front();
        }
    }

private:
    node_accessor current_node_{};
    std::size_t bucket_idx_{};
    const buckets *buckets_{};
};

template <class ValueType, class KeyHash, class KeyCompare, class KeySelect, class Backoff,
          std::size_t NumOfBuckets>
class MichaelHashTable {
    template <class, bool>
    friend class MichaelHashTableIterator;

    using bucket_type = OrderedListBase<ValueType, KeyCompare, KeySelect, Backoff>;

    using position = typename bucket_type::position;

    using node = typename bucket_type::node;
    using node_ptr = typename bucket_type::node_ptr;
    using node_marked_ptr = typename bucket_type::node_marked_ptr;
    using node_accessor = typename bucket_type::node_accessor;

    using buckets = std::array<std::optional<bucket_type>, NumOfBuckets>;

public:
    using value_type = typename bucket_type::value_type;
    using key_type = typename bucket_type::key_type;

    using difference_type = typename bucket_type::difference_type;
    using pointer = typename bucket_type::pointer;
    using const_pointer = typename bucket_type::const_pointer;
    using reference = typename bucket_type::reference;
    using const_reference = typename bucket_type::const_reference;

    using key_compare = typename bucket_type::key_compare;
    using hasher = KeyHash;
    using key_select = KeySelect;

    using accessor = lu::guarded_ptr<
            std::conditional_t<std::is_same_v<value_type, key_type>, const value_type, value_type>>;

    using iterator
            = MichaelHashTableIterator<MichaelHashTable, std::is_same_v<value_type, key_type>>;
    using const_iterator = MichaelHashTableIterator<MichaelHashTable, true>;

public:
    explicit MichaelHashTable(const hasher &hasher = {}, const key_compare &compare = {})
        : key_hash_(hasher) {
        for (auto it = buckets_.begin(); it != buckets_.end(); ++it) {
            it->emplace(compare);
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

    bucket_type &get_bucket(const key_type &key) noexcept {
        std::size_t hash = key_hash_(key);
        return *buckets_[get_bucket_idx(hash)];
    }

    const bucket_type &get_bucket(const key_type &key) const noexcept {
        std::size_t hash = key_hash_(key);
        return *buckets_[get_bucket_idx(hash)];
    }

public:
    bool insert(const value_type &value) {
        return emplace(value);
    }

    bool insert(value_type &&value) {
        return emplace(std::move(value));
    }

    template <class... Args>
    bool emplace(Args &&...args) {
        if constexpr (std::is_invocable_v<key_select, Args...>) {
            const key_type &key = key_select_(args...);
            auto &bucket = get_bucket(key);
            auto node_factory = [args = std::forward_as_tuple(args...)]() {
                return std::apply(
                        [](auto &&...args) {
                            return std::make_unique<node>(std::forward<decltype(args)>(args)...);
                        },
                        std::move(args));
            };
            return bucket.insert_node(key, std::move(node_factory));
        } else {
            auto new_node = std::make_unique<node>(std::forward<Args>(args)...);
            auto &bucket = get_bucket(key_select_(new_node->value));
            return bucket.insert_node(std::move(new_node));
        }
    }

    bool erase(const key_type &key) {
        auto &bucket = get_bucket(key);
        return bool(bucket.extract_node(key));
    }

    accessor extract(const key_type &key) {
        auto &bucket = get_bucket(key);
        auto [guard, node] = bucket.extract_node(key).unpack();
        return accessor(std::move(guard), std::addressof(node->value));
    }

    bool contains(const key_type &key) const {
        auto &bucket = get_bucket(key);
        return bucket.contains(key);
    }

    iterator find(const key_type &key) {
        std::size_t bucket_idx = get_bucket_idx(key_hash_(key));
        auto &bucket = *buckets_[bucket_idx];
        position pos;

        if (!bucket.find(key, pos)) {
            return end();
        }
        return iterator(node_accessor(std::move(pos.cur_guard), pos.cur), bucket_idx, &buckets_);
    }

    const_iterator find(const key_type &key) const {
        std::size_t bucket_idx = get_bucket_idx(key_hash_(key));
        auto &bucket = *buckets_[bucket_idx];
        position pos;

        if (!bucket.find(key, pos)) {
            return end();
        }
        return const_iterator(node_accessor(std::move(pos.cur_guard), pos.cur),
                              bucket_idx,
                              &buckets_);
    }

    iterator begin() {
        return iterator(0, &buckets_);
    }

    iterator end() {
        return iterator();
    }

    const_iterator cbegin() const {
        return const_iterator(0, &buckets_);
    }

    const_iterator cend() const {
        return const_iterator();
    }

    const_iterator begin() const {
        return cbegin();
    }

    const_iterator end() const {
        return cend();
    }

private:
    buckets buckets_{};
    NO_UNIQUE_ADDRESS hasher key_hash_{};
    NO_UNIQUE_ADDRESS key_select key_select_{};
};

struct MichaelHashTableDefaults {
    using key_compare = void;
    using hasher = void;
    using backoff = void;
};

}// namespace detail
}// namespace lu

namespace lu {
namespace detail {

template <class ValueType, std::size_t NumOfBuckets, class... Options>
struct make_michael_set {
    using pack_options = typename GetPackOptions<MichaelHashTableDefaults, Options...>::type;

    using key_compare
            = GetOrDefault<typename pack_options::key_compare, std::less<const ValueType>>;
    using hasher = GetOrDefault<typename pack_options::hasher, std::hash<ValueType>>;
    using backoff = GetOrDefault<typename pack_options::backoff, lu::none_backoff>;

    struct KeySelect {
        using type = ValueType;

        template <class T, class = std::enable_if_t<std::is_same_v<std::remove_cvref_t<T>, type>>>
        const T &operator()(const T &value) const noexcept {
            return value;
        }
    };

    using type = MichaelHashTable<ValueType, hasher, key_compare, KeySelect, backoff, NumOfBuckets>;
};

template <class KeyType, class ValueType, std::size_t NumOfBuckets, class... Options>
struct make_michael_map {
    using pack_options = typename GetPackOptions<MichaelHashTableDefaults, Options...>::type;

    using key_compare = GetOrDefault<typename pack_options::key_compare, std::less<const KeyType>>;
    using hasher = GetOrDefault<typename pack_options::hasher, std::hash<ValueType>>;
    using backoff = GetOrDefault<typename pack_options::backoff, lu::none_backoff>;

    using value_type = std::pair<const KeyType, ValueType>;

    struct KeySelect {
        using type = KeyType;

        template <class FirstType, class SecondType,
                  class = std::enable_if_t<std::is_same_v<std::remove_cvref_t<FirstType>, KeyType>>>
        const KeyType &operator()(const std::pair<FirstType, SecondType> &value) const {
            return value.first;
        }

        template <class FirstType, class SecondType,
                  class = std::enable_if_t<std::is_same_v<std::remove_cvref_t<FirstType>, KeyType>>>
        const KeyType &operator()(const FirstType &key, const SecondType &) const {
            return key;
        }
    };

    using type
            = MichaelHashTable<value_type, hasher, key_compare, KeySelect, backoff, NumOfBuckets>;
};

}// namespace detail

template <class ValueType, std::size_t NumOfBuckets, class... Options>
using michael_set = typename detail::make_michael_set<ValueType, NumOfBuckets, Options...>::type;

template <class KeyType, class ValueType, std::size_t NumOfBuckets, class... Options>
using michael_map =
        typename detail::make_michael_map<KeyType, ValueType, NumOfBuckets, Options...>::type;

}// namespace lu

#endif