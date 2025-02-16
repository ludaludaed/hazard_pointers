#ifndef __INTRUSIVE_HASH_TABLE_H__
#define __INTRUSIVE_HASH_TABLE_H__

#include <intrusive/compressed_tuple.h>
#include <intrusive/generic_hook.h>
#include <intrusive/size_traits.h>

#include <cassert>
#include <cstddef>
#include <iterator>
#include <memory>
#include <type_traits>
#include <utility>


namespace lu {
namespace detail {

template<class NodeTraits, bool IsFakeNode = true>
class BucketValue;

template<class NodeTraits>
class BucketValue<NodeTraits, true> {
public:
    using node_traits = NodeTraits;

    using node = typename node_traits::node;
    using node_ptr = typename node_traits::node_ptr;
    using const_node_ptr = typename node_traits::node_ptr;

public:
    BucketValue() = default;

    node_ptr as_node_ptr() noexcept {
        return std::pointer_traits<node_ptr>::pointer_to(*reinterpret_cast<node *>(this));
    }

    const_node_ptr as_node_ptr() const noexcept {
        return std::pointer_traits<const_node_ptr>::pointer_to(*reinterpret_cast<const node *>(this));
    }

    node_ptr get_bucket_begin() noexcept {
        node_ptr this_node_ptr = as_node_ptr();
        node_ptr next = node_traits::get_next(this_node_ptr);
        if (next) {
            return node_traits::get_next(next);
        }
        return node_ptr{};
    }

    const_node_ptr get_bucket_begin() const noexcept {
        const_node_ptr this_node_ptr = as_node_ptr();
        const_node_ptr next = node_traits::get_next(this_node_ptr);
        if (next) {
            return node_traits::get_next(next);
        }
        return const_node_ptr{};
    }

private:
    node_ptr next_{};
};

template<class NodeTraits>
class BucketValue<NodeTraits, false> : private NodeTraits::node {
public:
    using node_traits = NodeTraits;

    using node = typename node_traits::node;
    using node_ptr = typename node_traits::node_ptr;
    using const_node_ptr = typename node_traits::node_ptr;

public:
    BucketValue() = default;

    node_ptr as_node_ptr() noexcept {
        return std::pointer_traits<node_ptr>::pointer_to(*static_cast<node *>(this));
    }

    const_node_ptr as_node_ptr() const noexcept {
        return std::pointer_traits<const_node_ptr>::pointer_to(*static_cast<const node *>(this));
    }

    node_ptr get_bucket_begin() noexcept {
        node_ptr this_node_ptr = as_node_ptr();
        node_ptr next = node_traits::get_next(this_node_ptr);
        if (next) {
            return node_traits::get_next(next);
        }
        return node_ptr{};
    }

    const_node_ptr get_bucket_begin() const noexcept {
        const_node_ptr this_node_ptr = as_node_ptr();
        const_node_ptr next = node_traits::get_next(this_node_ptr);
        if (next) {
            return node_traits::get_next(next);
        }
        return const_node_ptr{};
    }
};

template<class BucketPtr, class SizeType>
class BucketTraitsImpl {
public:
    using bucket_ptr = BucketPtr;
    using size_type = SizeType;

public:
    BucketTraitsImpl(bucket_ptr buckets, size_type size) noexcept
        : buckets_(buckets)
        , size_(size) {}

    BucketTraitsImpl(const BucketTraitsImpl &other) noexcept = default;

    BucketTraitsImpl(BucketTraitsImpl &&other) noexcept = default;

    void swap(BucketTraitsImpl &other) noexcept {
        using std::swap;
        swap(buckets_, other.buckets_);
        swap(size_, other.size_);
    }

    friend void swap(BucketTraitsImpl &left, BucketTraitsImpl &right) noexcept {
        left.swap(right);
    }

    bucket_ptr data() const noexcept {
        return buckets_;
    }

    size_type size() const noexcept {
        return size_;
    }

private:
    bucket_ptr buckets_;
    size_type size_;
};

template<class NodeTraits>
struct HashtableAlgo {
    using node_traits = NodeTraits;

    using node = typename node_traits::node;
    using node_ptr = typename node_traits::node_ptr;
    using const_node_ptr = typename node_traits::const_node_ptr;

    static void init(node_ptr this_node) noexcept {
        node_traits::set_next(this_node, node_ptr{});
        node_traits::set_prev(this_node, node_ptr{});
    }

    static bool inited(const_node_ptr this_node) noexcept {
        return !node_traits::get_prev(this_node) && !node_traits::get_next(this_node);
    }

    static bool is_linked(const_node_ptr this_node) {
        return !inited(this_node);
    }

    static std::size_t distance(const_node_ptr first, const_node_ptr last) {
        std::size_t result = 0;
        while (first != last) {
            ++result;
            first = node_traits::get_next(first);
        }
        return result;
    }

    static bool last_in_bucket(const_node_ptr this_node) noexcept {
        node_ptr next = node_traits::get_next(this_node);
        return !next || this_node != node_traits::get_prev(next);
    }

    static bool first_in_bucket(const_node_ptr this_node) noexcept {
        node_ptr prev = node_traits::get_prev(this_node);
        return !prev || this_node != node_traits::get_next(prev);
    }

    static void unlink(node_ptr this_node) noexcept {
        if (inited(this_node)) {
            return;
        }

        node_ptr prev = node_traits::get_prev(this_node);
        node_ptr next = node_traits::get_next(this_node);

        if (last_in_bucket(this_node) && first_in_bucket(this_node)) {
            node_ptr bucket_ptr = prev;

            if (bucket_ptr) {
                prev = node_traits::get_next(bucket_ptr);
                node_traits::set_next(prev, next);
            }
            if (next) {
                node_ptr next_bucket_ptr = node_traits::get_prev(next);
                node_traits::set_next(next_bucket_ptr, prev);
            }

            node_traits::set_next(bucket_ptr, node_ptr{});
        } else if (first_in_bucket(this_node)) {
            node_ptr bucket_ptr = prev;
            node_traits::set_prev(next, bucket_ptr);

            if (bucket_ptr) {
                prev = node_traits::get_next(bucket_ptr);
                node_traits::set_next(prev, next);
            }
        } else if (last_in_bucket(this_node)) {
            node_traits::set_next(prev, next);

            if (next) {
                node_ptr next_bucket_ptr = node_traits::get_prev(next);
                node_traits::set_next(next_bucket_ptr, prev);
            }
        } else {
            node_traits::set_next(prev, next);
            node_traits::set_prev(next, prev);
        }

        init(this_node);
    }

    static void link(node_ptr head, node_ptr bucket, node_ptr new_node) noexcept {
        node_ptr first_node = node_traits::get_next(bucket);
        if (!first_node) {
            if (!inited(head)) {
                node_ptr prev_head_next = node_traits::get_next(head);
                node_ptr prev_head_bucket = node_traits::get_prev(prev_head_next);

                node_traits::set_next(new_node, prev_head_next);
                node_traits::set_next(prev_head_bucket, new_node);
            }
            node_traits::set_next(head, new_node);

            node_traits::set_next(bucket, head);
            node_traits::set_prev(new_node, bucket);
        } else {
            link_after(first_node, new_node);
        }
    }

    static void link_after(node_ptr prev_node, node_ptr new_node) noexcept {
        node_ptr next_node = node_traits::get_next(prev_node);

        node_traits::set_next(prev_node, new_node);
        node_traits::set_next(new_node, next_node);

        if (!next_node) {
            node_traits::set_prev(new_node, prev_node);
        } else {
            node_traits::set_prev(new_node, node_traits::get_prev(next_node));
            node_traits::set_prev(next_node, new_node);
        }
    }

    static void swap_heads(node_ptr this_node, node_ptr other_node) noexcept {
        if (this_node != other_node) {
            node_ptr this_next = node_traits::get_next(this_node);
            node_ptr other_next = node_traits::get_next(other_node);

            node_ptr this_next_prev{};
            node_ptr other_next_prev{};

            if (this_next) {
                this_next_prev = node_traits::get_prev(this_next);
            }
            if (other_next) {
                other_next_prev = node_traits::get_prev(other_next);
            }

            node_traits::set_next(this_node, other_next);
            node_traits::set_next(other_node, this_next);

            if (this_next_prev) {
                node_traits::set_next(this_next_prev, other_node);
            }
            if (other_next_prev) {
                node_traits::set_next(other_next_prev, this_node);
            }
        }
    }
};

template<class VoidPointer, bool StoreHash>
class HashtableNode {
    template<class, bool>
    friend class HashtableNodeTraits;

    using pointer = typename std::pointer_traits<VoidPointer>::template rebind<HashtableNode>;
    using const_pointer = typename std::pointer_traits<pointer>::template rebind<const HashtableNode>;

    pointer next{};
    pointer prev{};

    std::size_t hash{};
};

template<class VoidPointer>
class HashtableNode<VoidPointer, false> {
    template<class, bool>
    friend class HashtableNodeTraits;

    using pointer = typename std::pointer_traits<VoidPointer>::template rebind<HashtableNode>;
    using const_pointer = typename std::pointer_traits<pointer>::template rebind<const HashtableNode>;

    pointer next{};
    pointer prev{};
};

template<class VoidPointer, bool StoreHash>
struct HashtableNodeTraits {
    using node = HashtableNode<VoidPointer, StoreHash>;
    using node_ptr = typename node::pointer;
    using const_node_ptr = typename node::const_pointer;
    static constexpr bool store_hash = StoreHash;

    static void set_next(node_ptr this_node, node_ptr next) {
        this_node->next = next;
    }

    static node_ptr get_next(const_node_ptr this_node) {
        return this_node->next;
    }

    static void set_prev(node_ptr this_node, node_ptr prev) {
        this_node->prev = prev;
    }

    static node_ptr get_prev(const_node_ptr this_node) {
        return this_node->prev;
    }

    static std::size_t get_hash(const_node_ptr this_node) {
        return this_node->hash;
    }

    static void set_hash(node_ptr this_node, std::size_t hash) {
        this_node->hash = hash;
    }
};

template<class Types, bool IsConst>
class HashIterator {
    template<class, class, class, class, class, class, class>
    friend class IntrusiveHashtable;
    friend class HashIterator<Types, true>;

    class DummyNonConstIter;
    using NonConstIter = typename std::conditional_t<IsConst, HashIterator<Types, false>, DummyNonConstIter>;

    using value_traits = typename Types::value_traits;
    using value_traits_ptr = typename Types::value_traits_ptr;

    using node_traits = typename value_traits::node_traits;
    using node_ptr = typename node_traits::node_ptr;

public:
    using value_type = typename Types::value_type;
    using pointer = std::conditional_t<IsConst, typename Types::const_pointer, typename Types::pointer>;
    using reference = std::conditional_t<IsConst, typename Types::const_reference, typename Types::reference>;
    using difference_type = typename Types::difference_type;
    using iterator_category = std::forward_iterator_tag;

private:
    HashIterator(node_ptr current_node, value_traits_ptr value_traits) noexcept
        : current_node_(current_node)
        , value_traits_(value_traits) {}

public:
    HashIterator() noexcept = default;

    HashIterator(const NonConstIter &other)
        : current_node_(other.current_node_)
        , value_traits_(other.value_traits_) {}

    HashIterator &operator=(const HashIterator &other) {
        current_node_ = other.current_node_;
        value_traits_ = other.value_traits_;
        return *this;
    }

    HashIterator &operator++() noexcept {
        Increment();
        return *this;
    }

    HashIterator operator++(int) noexcept {
        HashIterator result(*this);
        Increment();
        return result;
    }

    inline reference operator*() const noexcept {
        return *operator->();
    }

    inline pointer operator->() const noexcept {
        return value_traits_->to_value_ptr(current_node_);
    }

    friend bool operator==(const HashIterator &left, const HashIterator &right) {
        return left.current_node_ == right.current_node_ && left.value_traits_ == right.value_traits_;
    }

    friend bool operator!=(const HashIterator &left, const HashIterator &right) {
        return !(left == right);
    }

private:
    void Increment() {
        current_node_ = node_traits::get_next(current_node_);
    }

private:
    node_ptr current_node_{};
    value_traits_ptr value_traits_{};
};

template<class Types, class Algo, bool IsConst>
class HashLocalIterator {
    template<class, class, class, class, class, class, class>
    friend class IntrusiveHashtable;

    friend class HashLocalIterator<Types, Algo, true>;

    class DummyNonConstIter;
    using NonConstIter = typename std::conditional_t<IsConst, HashLocalIterator<Types, Algo, false>, DummyNonConstIter>;

    using value_traits = typename Types::value_traits;
    using value_traits_ptr = typename Types::value_traits_ptr;

    using node_traits = typename value_traits::node_traits;
    using node_ptr = typename node_traits::node_ptr;

public:
    using value_type = typename Types::value_type;
    using pointer = std::conditional_t<IsConst, typename Types::const_pointer, typename Types::pointer>;
    using reference = std::conditional_t<IsConst, typename Types::const_reference, typename Types::reference>;
    using difference_type = typename Types::difference_type;
    using iterator_category = std::forward_iterator_tag;

private:
    HashLocalIterator(node_ptr node, value_traits_ptr value_traits) noexcept
        : current_node_(node)
        , value_traits_(value_traits) {}

public:
    HashLocalIterator() noexcept = default;

    HashLocalIterator(const NonConstIter &other)
        : current_node_(other.current_node_)
        , value_traits_(other.value_traits_) {}

    HashLocalIterator &operator=(const NonConstIter &other) {
        current_node_ = other.current_node_;
        value_traits_ = other.value_traits_;
        return *this;
    }

    HashLocalIterator &operator++() noexcept {
        Increment();
        return *this;
    }

    HashLocalIterator operator++(int) noexcept {
        HashLocalIterator result(*this);
        Increment();
        return result;
    }

    inline reference operator*() const noexcept {
        return *operator->();
    }

    inline pointer operator->() const noexcept {
        return value_traits_->to_value_ptr(current_node_);
    }

    friend bool operator==(const HashLocalIterator &left, const HashLocalIterator &right) {
        return left.current_node_ == right.current_node_ && left.value_traits_ == right.value_traits_;
    }

    friend bool operator!=(const HashLocalIterator &left, const HashLocalIterator &right) {
        return !(left == right);
    }

private:
    void Increment() {
        if (Algo::last_in_bucket(current_node_)) {
            current_node_ = node_ptr{};
        } else {
            current_node_ = node_traits::get_next(current_node_);
        }
    }

private:
    node_ptr current_node_{};
    value_traits_ptr value_traits_{};
};

template<bool IsPower2Buckets, bool IsMulti>
struct HashtableFlags {
    static const bool is_power_2_buckets = IsPower2Buckets;
    static const bool is_multi = IsMulti;
};

template<class ValueTraits, class BucketTraits, class KeyOfValue, class KeyHash, class KeyEqual, class SizeType,
         class Flags>
class IntrusiveHashtable {
    using size_traits = SizeTraits<SizeType, !ValueTraits::is_auto_unlink>;
    using Algo = HashtableAlgo<typename ValueTraits::node_traits>;

public:
    using bucket_traits = BucketTraits;
    using value_traits = ValueTraits;
    using node_traits = typename value_traits::node_traits;

    using key_type = typename KeyOfValue::type;
    using value_type = typename value_traits::value_type;

    using key_of_value = KeyOfValue;
    using hasher = KeyHash;
    using key_equal = KeyEqual;

    using pointer = typename value_traits::pointer;
    using const_pointer = typename value_traits::const_pointer;
    using reference = typename value_traits::reference;
    using const_reference = typename value_traits::const_reference;
    using difference_type = typename std::pointer_traits<pointer>::difference_type;
    using size_type = SizeType;

    using node = typename node_traits::node;
    using node_ptr = typename node_traits::node_ptr;
    using const_node_ptr = typename node_traits::const_node_ptr;

    using bucket_ptr = typename bucket_traits::bucket_ptr;
    using bucket_type = typename std::pointer_traits<bucket_ptr>::element_type;

    using iterator = HashIterator<IntrusiveHashtable, false>;
    using const_iterator = HashIterator<IntrusiveHashtable, true>;

    using local_iterator = HashLocalIterator<IntrusiveHashtable, Algo, false>;
    using const_local_iterator = HashLocalIterator<IntrusiveHashtable, Algo, true>;

    using value_traits_ptr = const value_traits *;

private:
    struct NilNodeHolder {
        friend void swap(NilNodeHolder &left, NilNodeHolder &right) {
            auto left_node = std::pointer_traits<node_ptr>::pointer_to(left.node_);
            auto right_node = std::pointer_traits<node_ptr>::pointer_to(right.node_);
            Algo::swap_heads(left_node, right_node);
        }

        node node_{};
    };

public:
    explicit IntrusiveHashtable(const bucket_traits &buckets = {}, const hasher &hash = {}, const key_equal &equal = {},
                                const value_traits &value_traits = {})
        : data_(NilNodeHolder{}, value_traits, buckets, KeyOfValue{}, hash, equal, size_traits{}) {
        Construct();
    }

    template<class Iterator>
    IntrusiveHashtable(Iterator begin, Iterator end, const bucket_traits &buckets = {}, const hasher &hash = {},
                       const key_equal &equal = {}, const value_traits &value_traits = {})
        : data_(NilNodeHolder{}, value_traits, buckets, KeyOfValue{}, hash, equal, size_traits{}) {
        Construct();
        insert(begin, end);
    }

    ~IntrusiveHashtable() {
        clear();
    }

    IntrusiveHashtable(const IntrusiveHashtable &other) = delete;

    IntrusiveHashtable(IntrusiveHashtable &&other) noexcept {
        Construct();
        swap(other);
    }

    IntrusiveHashtable &operator=(const IntrusiveHashtable &other) = delete;

    IntrusiveHashtable &operator=(IntrusiveHashtable &&other) noexcept {
        IntrusiveHashtable temp(std::move(other));
        swap(other);
        return *this;
    }

private:
    void Construct() noexcept {
        Algo::init(GetNilPtr());
    }

    inline value_traits_ptr GetValueTraitsPtr() const noexcept {
        return std::pointer_traits<value_traits_ptr>::pointer_to(lu::get<ValueTraits>(data_));
    }

    inline node_ptr GetNilPtr() noexcept {
        return std::pointer_traits<node_ptr>::pointer_to(lu::get<NilNodeHolder>(data_).node_);
    }

    inline const_node_ptr GetNilPtr() const noexcept {
        return std::pointer_traits<const_node_ptr>::pointer_to(lu::get<NilNodeHolder>(data_).node_);
    }

    node_ptr GetFirst() const noexcept {
        return node_traits::get_next(GetNilPtr());
    }

    node_ptr GetEnd() const noexcept {
        return node_ptr{};
    }

    inline decltype(auto) GetKey(const_node_ptr node) const {
        const value_traits &_value_traits = lu::get<ValueTraits>(data_);
        const key_of_value &_key_of_value = lu::get<KeyOfValue>(data_);

        const_pointer value_ptr = _value_traits.to_value_ptr(node);
        return _key_of_value(*value_ptr);
    }

    inline decltype(auto) GetKey(const_reference value) const {
        const key_of_value &_key_of_value = lu::get<KeyOfValue>(data_);
        return _key_of_value(value);
    }

    inline std::size_t GetHash(node_ptr node) const noexcept {
        if constexpr (node_traits::store_hash) {
            return node_traits::get_hash(node);
        } else {
            const hasher &_key_hash = lu::get<KeyHash>(data_);
            return _key_hash(GetKey(node));
        }
    }

    inline void SetHash(node_ptr node, std::size_t hash) const noexcept {
        if constexpr (node_traits::store_hash) {
            node_traits::set_hash(node, hash);
        }
    }

    size_type GetSize() const noexcept {
        if constexpr (size_traits::is_tracking_size) {
            return lu::get<size_traits>(data_).get_size();
        } else {
            return Algo::distance(GetFirst(), GetEnd());
        }
    }

    bucket_ptr GetBucket(size_type bucket_index) const noexcept {
        const bucket_traits &_bucket_traits = lu::get<BucketTraits>(data_);
        return _bucket_traits.data() + bucket_index;
    }

    size_type GetBucketIdx(std::size_t hash) const noexcept {
        const bucket_traits &_bucket_traits = lu::get<BucketTraits>(data_);
        if constexpr (Flags::is_power_2_buckets) {
            return hash & (_bucket_traits.size() - 1);
        } else {
            return hash % _bucket_traits.size();
        }
    }

    node_ptr GetBucketBegin(size_type bucket_index) const noexcept {
        bucket_ptr bucket = GetBucket(bucket_index);
        return bucket->get_bucket_begin();
    }

private:
    node_ptr Find(const key_type &key, std::size_t hash) const noexcept {
        const key_equal &_key_equal = lu::get<KeyEqual>(data_);

        size_type bucket_index = GetBucketIdx(hash);
        node_ptr current = GetBucketBegin(bucket_index);

        while (current) {
            if (hash == GetHash(current) && _key_equal(GetKey(current), key)) {
                return current;
            }
            node_ptr next = node_traits::get_next(current);
            if (next && !Algo::first_in_bucket(next)) {
                current = next;
            } else {
                current = node_ptr{};
            }
        }

        return node_ptr{};
    }

    node_ptr Find(const key_type &key) const noexcept {
        const hasher &_key_hash = lu::get<KeyHash>(data_);
        std::size_t hash = _key_hash(key);
        return Find(key, hash);
    }

    std::pair<node_ptr, node_ptr> EqualRange(const key_type &key) const noexcept {
        const hasher &_key_hash = lu::get<KeyHash>(data_);
        const key_equal &_key_equal = lu::get<KeyEqual>(data_);

        std::size_t hash = _key_hash(key);

        size_type bucket_index = GetBucketIdx(hash);
        node_ptr current = GetBucketBegin(bucket_index);

        while (current) {
            node_ptr begin = current;
            while (current && hash == GetHash(current) && _key_equal(GetKey(current), key)) {
                current = node_traits::get_next(current);
            }
            if (begin == current) {
                current = node_traits::get_next(current);
            } else {
                return {begin, current};
            }
        }

        return {node_ptr{}, node_ptr{}};
    }

    void InsertByRehash(reference value) {
        const value_traits &_value_traits = lu::get<ValueTraits>(data_);

        node_ptr new_node = _value_traits.to_node_ptr(value);
        std::size_t hash = GetHash(new_node);

        node_ptr position = Find(GetKey(value), hash);

        if (position) {
            Algo::link_after(position, new_node);
        } else {
            size_type bucket_index = GetBucketIdx(hash);
            bucket_ptr bucket = GetBucket(bucket_index);

            Algo::link(GetNilPtr(), bucket->as_node_ptr(), new_node);
        }
    }

    std::pair<iterator, bool> InsertUnique(reference value) {
        const value_traits &_value_traits = lu::get<ValueTraits>(data_);
        const hasher &_key_hash = lu::get<KeyHash>(data_);

        std::size_t hash = _key_hash(GetKey(value));

        node_ptr new_node = _value_traits.to_node_ptr(value);
        assert(!Algo::is_linked(new_node));
        node_ptr position = Find(GetKey(value), hash);
        SetHash(new_node, hash);

        if (position) {
            return {iterator(position, GetValueTraitsPtr()), true};
        } else {
            size_type bucket_index = GetBucketIdx(hash);
            bucket_ptr bucket = GetBucket(bucket_index);

            lu::get<size_traits>(data_).increment();

            Algo::link(GetNilPtr(), bucket->as_node_ptr(), new_node);
            return {iterator(new_node, GetValueTraitsPtr()), false};
        }
    }

    iterator InsertEqual(reference value) {
        const value_traits &_value_traits = lu::get<ValueTraits>(data_);
        const hasher &_key_hash = lu::get<KeyHash>(data_);

        std::size_t hash = _key_hash(GetKey(value));

        node_ptr new_node = _value_traits.to_node_ptr(value);
        assert(!Algo::is_linked(new_node));
        node_ptr position = Find(GetKey(value), hash);
        SetHash(new_node, hash);

        lu::get<size_traits>(data_).increment();

        if (position) {
            Algo::link_after(position, new_node);
            return iterator(new_node, GetValueTraitsPtr());
        } else {
            size_type bucket_index = GetBucketIdx(hash);
            bucket_ptr bucket = GetBucket(bucket_index);

            Algo::link(GetNilPtr(), bucket->as_node_ptr(), new_node);
            return iterator(new_node, GetValueTraitsPtr());
        }
    }

    void Erase(node_ptr node) {
        lu::get<size_traits>(data_).decrement();
        Algo::unlink(node);
    }

    size_type Erase(const key_type &key) {
        const value_traits &_value_traits = lu::get<ValueTraits>(data_);
        const hasher &_key_hash = lu::get<KeyHash>(data_);
        const key_equal &_key_equal = lu::get<KeyEqual>(data_);

        size_type result = 0;
        std::size_t hash = _key_hash(key);

        size_type bucket_index = GetBucketIdx(hash);
        node_ptr current = GetBucketBegin(bucket_index);

        while (current) {
            node_ptr next = node_traits::get_next(current);
            if (hash == GetHash(current) && _key_equal(GetKey(current), key)) {
                result++;
                Erase(current);
            }
            if (next && !Algo::first_in_bucket(next)) {
                current = next;
            } else {
                current = node_ptr{};
            }
        }
        return result;
    }

    size_type Count(const key_type &key) const {
        std::pair<node_ptr, node_ptr> range = EqualRange(key);
        return Algo::distance(range.first, range.second);
    }

public:
    auto insert(reference value) {
        bool v = Flags::is_multi;
        if constexpr (Flags::is_multi) {
            return InsertEqual(value);
        } else {
            return InsertUnique(value);
        }
    }

    template<class Iterator>
    void insert(Iterator begin, Iterator end) {
        for (; begin != end; ++begin) {
            insert(*begin);
        }
    }

    size_type erase(const key_type &key) noexcept {
        return Erase(key);
    }

    void erase(const_iterator position) noexcept {
        Erase(position.current_node_);
    }

    void erase(const_iterator begin, const_iterator end) noexcept {
        for (; begin != end; ++begin) {
            Erase(begin.current_node_);
        }
    }

    void swap(IntrusiveHashtable &other) noexcept {
        data_.swap(other.data_);
    }

    void clear() noexcept {
        node_ptr current = GetNilPtr();
        current = node_traits::get_next(current);
        while (current) {
            node_ptr next = node_traits::get_next(current);
            Erase(current);
            current = next;
        }
    }

    template<class OtherBucketTraits, class OtherKeyOfValue, class OtherKeyHash, class OtherKeyEqual,
             class OtherSizeType, class OtherFlags>
    void merge(IntrusiveHashtable<ValueTraits, OtherBucketTraits, OtherKeyOfValue, OtherKeyHash, OtherKeyEqual,
                                  OtherSizeType, OtherFlags> &other) {
        for (iterator it = other.begin(); it != other.end();) {
            iterator next = std::next(it);
            other.erase(it);
            insert(*it);
            it = next;
        }
    }

    template<class OtherBucketTraits, class OtherKeyOfValue, class OtherKeyHash, class OtherKeyEqual,
             class OtherSizeType, class OtherFlags>
    void merge(IntrusiveHashtable<ValueTraits, OtherBucketTraits, OtherKeyOfValue, OtherKeyHash, OtherKeyEqual,
                                  OtherSizeType, OtherFlags> &&other) {
        merge(other);
    }

    template<class Iterator>
    void assign(Iterator first, Iterator last) noexcept {
        clear();
        insert(first, last);
    }

    void rehash(const bucket_traits &new_bucket_traits) {
        const value_traits &_value_traits = lu::get<ValueTraits>(data_);
        lu::get<BucketTraits>(data_) = new_bucket_traits;

        node_ptr current = GetFirst();
        Algo::init(GetNilPtr());

        while (current) {
            node_ptr next = node_traits::get_next(current);
            Algo::init(current);
            pointer value_ptr = _value_traits.to_value_ptr(current);
            InsertByRehash(*value_ptr);
            current = next;
        }
    }

public:
    iterator find(const key_type &key) noexcept {
        return iterator(Find(key), GetValueTraitsPtr());
    }

    const_iterator find(const key_type &key) const noexcept {
        return const_iterator(Find(key), GetValueTraitsPtr());
    }

    size_type count(const key_type &key) const {
        return Count(key);
    }

    bool contains(const key_type &key) const noexcept {
        return Find(key);
    }

    std::pair<iterator, iterator> equal_range(const key_type &key) {
        std::pair<node_ptr, node_ptr> res = EqualRange(key);
        return {iterator(res.first, GetValueTraitsPtr()), iterator(res.second, GetValueTraitsPtr())};
    }

    std::pair<const_iterator, const_iterator> equal_range(const key_type &key) const {
        std::pair<node_ptr, node_ptr> res = EqualRange(key);
        return {const_iterator(res.first, GetValueTraitsPtr()), const_iterator(res.second, GetValueTraitsPtr())};
    }

    iterator iterator_to(reference value) {
        const value_traits &_value_traits = lu::get<ValueTraits>(data_);
        node_ptr node = _value_traits.to_node_ptr(value);
        return iterator(node, GetValueTraitsPtr());
    }

    const_iterator iterator_to(const_reference value) const {
        const value_traits &_value_traits = lu::get<ValueTraits>(data_);
        node_ptr node = _value_traits.to_node_ptr(value);
        return const_iterator(node, GetValueTraitsPtr());
    }

    local_iterator local_iterator_to(reference value) {
        const value_traits &_value_traits = lu::get<ValueTraits>(data_);
        node_ptr node = _value_traits.to_node_ptr(value);

        while (!Algo::first_in_bucket(node)) {
            node = node_traits::get_prev(node);
        }
        return local_iterator(node, GetValueTraitsPtr());
    }

    const_local_iterator local_iterator_to(const_reference value) const {
        const value_traits &_value_traits = lu::get<ValueTraits>(data_);
        node_ptr node = _value_traits.to_node_ptr(value);

        while (!Algo::first_in_bucket(node)) {
            node = node_traits::get_prev(node);
        }
        return const_local_iterator(node, GetValueTraitsPtr());
    }

public:
    hasher hash_function() const {
        return lu::get<KeyHash>(data_);
    }

    key_equal key_eq() const {
        return lu::get<KeyEqual>(data_);
    }

public:
    iterator begin() {
        return iterator(GetFirst(), GetValueTraitsPtr());
    }

    iterator end() {
        return iterator(GetEnd(), GetValueTraitsPtr());
    }

    const_iterator begin() const {
        return const_iterator(GetFirst(), GetValueTraitsPtr());
    }

    const_iterator end() const {
        return const_iterator(GetEnd(), GetValueTraitsPtr());
    }

    const_iterator cbegin() const {
        return const_iterator(GetFirst(), GetValueTraitsPtr());
    }

    const_iterator cend() const {
        return const_iterator(GetEnd(), GetValueTraitsPtr());
    }

public:
    size_type bucket_count() const noexcept {
        const bucket_traits &_bucket_traits = lu::get<BucketTraits>(data_);
        return _bucket_traits.size();
    }

    size_type bucket_size(size_type bucket_index) const noexcept {
        size_type size = 0;
        for (const_local_iterator it = begin(bucket_index); it != end(bucket_index); ++it) {
            size += 1;
        }
        return size;
    }

    size_type bucket(const key_type &key) const noexcept {
        const hasher &_hasher = lu::get<KeyHash>(data_);
        std::size_t hash = _hasher(key);
        return GetBucketIdx(hash);
    }

    local_iterator begin(size_type bucket_index) {
        return local_iterator(GetBucketBegin(bucket_index), GetValueTraitsPtr());
    }

    local_iterator end(size_type bucket_index) {
        return local_iterator(GetEnd(), GetValueTraitsPtr());
    }

    const_local_iterator begin(size_type bucket_index) const {
        return const_local_iterator(GetBucketBegin(bucket_index), GetValueTraitsPtr());
    }

    const_local_iterator end(size_type bucket_index) const {
        return const_local_iterator(GetEnd(), GetValueTraitsPtr());
    }

    const_local_iterator cbegin(size_type bucket_index) const {
        return const_local_iterator(GetBucketBegin(bucket_index), GetValueTraitsPtr());
    }

    const_local_iterator cend(size_type bucket_index) const {
        return const_local_iterator(GetEnd(), GetValueTraitsPtr());
    }

public:
    size_type size() const noexcept {
        return GetSize();
    }

    bool empty() const noexcept {
        return Algo::inited(GetNilPtr());
    }

public:
    friend bool operator==(const IntrusiveHashtable &left, const IntrusiveHashtable &right) {
        if (left.size() != right.size()) {
            return false;
        }
        const_iterator it = left.begin();

        while (it != left.end()) {
            std::pair<const_iterator, const_iterator> left_equal_range = left.equal_range(left.GetKey(*it));
            std::pair<const_iterator, const_iterator> right_equal_range = right.equal_range(right.GetKey(*it));

            if (std::distance(left_equal_range.first, left_equal_range.second)
                != std::distance(right_equal_range.first, right_equal_range.second)) {
                return false;
            }
            it = left_equal_range.second;
        }
        return true;
    }

    friend bool operator!=(const IntrusiveHashtable &left, const IntrusiveHashtable &right) {
        return !(left == right);
    }

    friend void swap(IntrusiveHashtable &left, IntrusiveHashtable &right) noexcept {
        left.swap(right);
    }

private:
    lu::compressed_tuple<NilNodeHolder, ValueTraits, BucketTraits, KeyOfValue, KeyHash, KeyEqual, size_traits> data_;
};

template<class HookType>
struct HashtableDefaultHook {
    using hashtable_default_hook_type = HookType;
};

template<class VoidPointer, class Tag, bool StoreHash, bool IsAutoUnlink>
class HashtableBaseHook
    : public GenericHook<HashtableAlgo<HashtableNodeTraits<VoidPointer, StoreHash>>,
                         HashtableNodeTraits<VoidPointer, StoreHash>, Tag, IsAutoUnlink>,
      public std::conditional_t<
              std::is_same_v<Tag, DefaultHookTag>,
              HashtableDefaultHook<GenericHook<HashtableAlgo<HashtableNodeTraits<VoidPointer, StoreHash>>,
                                               HashtableNodeTraits<VoidPointer, StoreHash>, Tag, IsAutoUnlink>>,
              NotDefaultHook> {};

template<class ValueType>
struct DefaultKeyOfValue {
    using type = ValueType;

    template<class T, class = std::enable_if_t<std::is_same_v<std::decay_t<T>, type>>>
    T &&operator()(T &&value) const {
        return std::forward<T>(value);
    }
};

struct DefaultHashTableHook {
    template<class ValueType>
    struct GetDefaultHook {
        using type = typename ValueType::hashtable_default_hook_type;
    };

    struct is_default_hook_tag;
};

struct DefaultBucketTraits;

struct HashtableDefaults {
    using proto_value_traits = DefaultHashTableHook;
    using size_type = std::size_t;
    using key_of_value = void;
    using equal = void;
    using hash = void;
    using proto_bucket_traits = DefaultBucketTraits;
    static const bool is_power_2_buckets = false;
};

struct HashtableHookDefaults {
    using void_pointer = void *;
    using tag = DefaultHookTag;
    static const bool store_hash = true;
    static const bool is_auto_unlink = true;
};

template<class ProtoBucketTraits, class, class>
struct GetBucketTraits {
    using type = ProtoBucketTraits;
};

template<class ValueTraits, class SizeType>
struct GetBucketTraits<DefaultBucketTraits, ValueTraits, SizeType> {
    using bucket_type = BucketValue<typename ValueTraits::node_traits>;
    using bucket_pointer = typename std::pointer_traits<typename ValueTraits::pointer>::template rebind<bucket_type>;
    using type = BucketTraitsImpl<bucket_pointer, SizeType>;
};

}// namespace detail
}// namespace lu

#endif
