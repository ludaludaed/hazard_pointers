#ifndef __INTRUSIVE_SLIST_H__
#define __INTRUSIVE_SLIST_H__

#include "base_value_traits.h"
#include "empty_base_holder.h"
#include "generic_hook.h"
#include "size_traits.h"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <functional>
#include <memory>
#include <type_traits>
#include <utility>


namespace lu {
namespace detail {

template<class NodeTraits>
class CircularSlistAlgo {
public:
    using node = typename NodeTraits::node;
    using node_ptr = typename NodeTraits::node_ptr;
    using const_node_ptr = typename NodeTraits::const_node_ptr;

    using node_traits = NodeTraits;

public:
    static void init_head(node_ptr this_node) noexcept {
        node_traits::set_next(this_node, this_node);
    }

    static void init(node_ptr this_node) noexcept {
        node_traits::set_next(this_node, node_ptr{});
    }

    static bool is_empty(const_node_ptr this_node) noexcept {
        return this_node == node_traits::get_next(this_node);
    }

    static bool is_linked(const_node_ptr this_node) {
        return !unique(this_node);
    }

    static bool unique(const_node_ptr this_node) noexcept {
        const_node_ptr next = node_traits::get_next(this_node);
        return (!next) || (next == this_node);
    }

    static std::size_t count(const_node_ptr this_node) noexcept {
        std::size_t result = 1;
        const_node_ptr current = node_traits::get_next(this_node);
        while (current && current != this_node) {
            ++result;
            current = node_traits::get_next(current);
        }
        return result;
    }

    static std::size_t distance(const_node_ptr first, const_node_ptr last) {
        std::size_t result = 0;
        const_node_ptr current = first;
        while (current != last) {
            ++result;
            current = node_traits::get_next(current);
        }
        return result;
    }

    static node_ptr get_end(const_node_ptr this_node) noexcept {
        return erase_const(this_node);
    }

    static node_ptr get_prev(node_ptr head_node, node_ptr this_node) noexcept {
        node_ptr current = head_node;
        while (this_node != node_traits::get_next(current)) {
            current = node_traits::get_next(current);
        }
        return current;
    }

    static node_ptr get_prev(node_ptr this_node) noexcept {
        return get_prev(this_node, this_node);
    }

    static node_ptr get_last(node_ptr this_node) noexcept {
        return get_prev(this_node, this_node);
    }

    static node_ptr next(node_ptr this_node, std::size_t steps = 1) noexcept {
        while (steps != 0) {
            this_node = node_traits::get_next(this_node);
            --steps;
        }
        return this_node;
    }

    static void unlink(node_ptr this_node) noexcept {
        node_ptr prev_node = get_prev(this_node, this_node);
        unlink_after(prev_node);
    }

    static void unlink_after(node_ptr before) noexcept {
        node_ptr this_node = node_traits::get_next(before);
        node_traits::set_next(before, node_traits::get_next(this_node));
        init(this_node);
    }

    static std::size_t unlink_after(node_ptr before_begin, node_ptr end) {
        std::size_t num{};
        node_ptr current = node_traits::get_next(before_begin);
        while (current != end) {
            node_ptr next = node_traits::get_next(current);
            init(current);
            ++num;
            current = next;
        }
        node_traits::set_next(before_begin, end);
        return num;
    }

    static void link_after(node_ptr before, node_ptr new_node) noexcept {
        node_ptr next_node = node_traits::get_next(before);
        node_traits::set_next(new_node, next_node);
        node_traits::set_next(before, new_node);
    }

    static void swap_nodes(node_ptr this_node, node_ptr other_node) noexcept {
        if (other_node != this_node) {
            node_ptr this_next = node_traits::get_next(this_node);
            node_ptr other_next = node_traits::get_next(other_node);

            if (!unique(other_node)) {
                node_traits::set_next(this_next == other_node ? other_node : get_prev(other_node), this_node);
            }
            if (!unique(this_node)) {
                node_traits::set_next(other_next == this_node ? this_node : get_prev(this_node), other_node);
            }

            node_traits::set_next(this_node, other_node == other_next
                                                     ? this_node
                                                     : (other_next == this_node ? other_node : other_next));
            node_traits::set_next(other_node, this_node == this_next
                                                      ? other_node
                                                      : (this_next == other_node ? this_node : this_next));
        }
    }

    static node_ptr reverse(node_ptr head_node) noexcept {
        node_ptr prev{};
        node_ptr current = head_node;
        while (node_traits::get_next(current)) {
            node_ptr next = node_traits::get_next(current);
            node_traits::set_next(current, prev);
            prev = current;
            current = next;
        }
        node_traits::set_next(head_node, prev);
        return head_node;
    }

    static inline void transfer_after(node_ptr where, node_ptr before_first, node_ptr before_last) noexcept {
        if (where != before_first && where != before_last && before_first != before_last) {
            node_ptr first = node_traits::get_next(before_last);
            node_ptr start = node_traits::get_next(before_first);
            node_ptr where_next = node_traits::get_next(where);

            node_traits::set_next(before_first, first);
            node_traits::set_next(where, start);
            node_traits::set_next(before_last, where_next);
        }
    }

    template<class Compare>
    static inline void merge(node_ptr left_head, node_ptr right_head, Compare &&comp) noexcept {
        node_ptr left_prev = left_head;
        node_ptr left_current = node_traits::get_next(left_head);

        while (left_current != left_head && !unique(right_head)) {
            if (comp(node_traits::get_next(right_head), left_current)) {
                transfer_after(left_prev, right_head, node_traits::get_next(right_head));
                left_prev = node_traits::get_next(left_prev);
            } else {
                left_prev = node_traits::get_next(left_prev);
                left_current = node_traits::get_next(left_current);
            }
        }
        transfer_after(left_prev, right_head, get_prev(right_head, right_head));
    }

    template<class Compare>
    static inline void sort(node_ptr head, Compare &&comp) noexcept {
        std::size_t size = count(head) - 1;

        node_traits::set_next(get_last(head), node_ptr{});

        node_ptr new_begin = sort(node_traits::get_next(head), size, std::forward<Compare>(comp));
        node_ptr new_last = get_prev(new_begin, node_ptr{});

        node_traits::set_next(head, new_begin);
        node_traits::set_next(new_last, head);
    }

private:
    template<class Compare>
    static inline node_ptr linear_merge(node_ptr left_first, node_ptr right_first, Compare &&comp) noexcept {
        if (!left_first) {
            return right_first;
        }
        if (!right_first) {
            return left_first;
        }
        node_ptr start;
        if (comp(left_first, right_first)) {
            start = left_first;
            left_first = node_traits::get_next(left_first);
        } else {
            start = right_first;
            right_first = node_traits::get_next(right_first);
        }
        node_ptr current{start};

        while (left_first && right_first) {
            if (!comp(right_first, left_first)) {
                node_traits::set_next(current, left_first);
                left_first = node_traits::get_next(left_first);
            } else {
                node_traits::set_next(current, right_first);
                right_first = node_traits::get_next(right_first);
            }
            current = node_traits::get_next(current);
        }
        if (left_first) {
            node_traits::set_next(current, left_first);
        } else {
            node_traits::set_next(current, right_first);
        }
        return start;
    }

    template<class Compare>
    static inline node_ptr sort(node_ptr begin_node, std::size_t size, Compare &&comp) noexcept {
        if (size == 0) {
            return node_ptr{};
        } else if (size == 1) {
            return begin_node;
        }
        std::size_t left_size = size / 2;
        std::size_t right_size = size - left_size;
        node_ptr mid = next(begin_node, left_size - 1);

        node_ptr begin_right = node_traits::get_next(mid);
        node_traits::set_next(mid, node_ptr{});

        node_ptr left = sort(begin_node, left_size, std::forward<Compare>(comp));
        node_ptr right = sort(begin_right, right_size, std::forward<Compare>(comp));
        return linear_merge(left, right, std::forward<Compare>(comp));
    }
};

template<class VoidPointer>
class SlistNode {
    template<class>
    friend class SlistNodeTraits;

    using pointer = typename std::pointer_traits<VoidPointer>::template rebind<SlistNode>;
    using const_pointer = typename std::pointer_traits<pointer>::template rebind<const SlistNode>;

    pointer next{};
};

template<class VoidPointer>
class SlistNodeTraits {
public:
    using node = SlistNode<VoidPointer>;
    using node_ptr = typename node::pointer;
    using const_node_ptr = typename node::const_pointer;

public:
    static void set_next(node_ptr this_node, node_ptr next) {
        this_node->next = next;
    }

    static node_ptr get_next(const_node_ptr this_node) {
        return this_node->next;
    }
};

template<class Types, bool IsConst>
class SlistIterator {
    template<class, class>
    friend class IntrusiveSlist;
    friend class SlistIterator<Types, true>;

    class DummyNonConstIter;
    using NonConstIter = typename std::conditional_t<IsConst, SlistIterator<Types, false>, DummyNonConstIter>;

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
    SlistIterator(node_ptr current_node, value_traits_ptr value_traits) noexcept
        : current_node_(current_node)
        , value_traits_(value_traits) {}

public:
    SlistIterator() noexcept = default;

    SlistIterator(const NonConstIter &other)
        : current_node_(other.current_node_)
        , value_traits_(other.value_traits_) {}

    SlistIterator &operator=(const NonConstIter &other) {
        current_node_ = other.current_node_;
        value_traits_ = other.value_traits_;
        return *this;
    }

    SlistIterator &operator++() noexcept {
        Increment();
        return *this;
    }

    SlistIterator operator++(int) noexcept {
        SlistIterator result(*this);
        Increment();
        return result;
    }

    inline reference operator*() const noexcept {
        return *operator->();
    }

    inline pointer operator->() const noexcept {
        return value_traits_->to_value_ptr(current_node_);
    }

    friend bool operator==(const SlistIterator &left, const SlistIterator &right) {
        return left.current_node_ == right.current_node_ && left.value_traits_ == right.value_traits_;
    }

    friend bool operator!=(const SlistIterator &left, const SlistIterator &right) {
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

template<class ValueTraits, class SizeType>
class IntrusiveSlist : private EmptyBaseHolder<ValueTraits>,
                       private EmptyBaseHolder<SizeTraits<SizeType, !ValueTraits::is_auto_unlink>> {
private:
    using ValueTraitsHolder = EmptyBaseHolder<ValueTraits>;
    using SizeTraitsHolder = EmptyBaseHolder<SizeTraits<SizeType, !ValueTraits::is_auto_unlink>>;

    using SizeTraits = SizeTraits<SizeType, !ValueTraits::is_auto_unlink>;
    using Algo = CircularSlistAlgo<typename ValueTraits::node_traits>;

public:
    using value_traits = ValueTraits;
    using node_traits = typename value_traits::node_traits;

    using value_type = typename value_traits::value_type;

    using pointer = typename value_traits::pointer;
    using const_pointer = typename value_traits::const_pointer;
    using reference = typename value_traits::reference;
    using const_reference = typename value_traits::const_reference;

    using difference_type = typename std::pointer_traits<pointer>::difference_type;
    using size_type = SizeType;

    using node = typename node_traits::node;
    using node_ptr = typename node_traits::node_ptr;
    using const_node_ptr = typename node_traits::const_node_ptr;

    using iterator = SlistIterator<IntrusiveSlist, false>;
    using const_iterator = SlistIterator<IntrusiveSlist, true>;

    using value_traits_ptr = const value_traits *;

public:
    explicit IntrusiveSlist(const value_traits &value_traits = {})
        : ValueTraitsHolder(value_traits) {
        Construct();
    }

    template<class Iterator>
    IntrusiveSlist(Iterator begin, Iterator end, const value_traits &value_traits = {})
        : ValueTraitsHolder(value_traits) {
        Construct();
        insert_after(before_begin(), begin, end);
    }

    IntrusiveSlist(const IntrusiveSlist &other) = delete;

    IntrusiveSlist(IntrusiveSlist &&other) noexcept {
        Construct();
        swap(other);
    }

    IntrusiveSlist &operator=(const IntrusiveSlist &other) = delete;

    IntrusiveSlist &operator=(IntrusiveSlist &&other) noexcept {
        IntrusiveSlist temp(std::move(other));
        swap(temp);
        return *this;
    }

    ~IntrusiveSlist() {
        clear();
    }

private:
    void Construct() noexcept {
        Algo::init_head(GetNilPtr());
    }

    inline value_traits_ptr GetValueTraitsPtr() const noexcept {
        return std::pointer_traits<value_traits_ptr>::pointer_to(ValueTraitsHolder::get());
    }

    node_ptr GetNilPtr() const noexcept {
        return Algo::get_end(std::pointer_traits<const_node_ptr>::pointer_to(nil_node_));
    }

    node_ptr GetEnd() const noexcept {
        return Algo::get_end(GetNilPtr());
    }

    node_ptr GetFirst() const noexcept {
        return node_traits::get_next(GetNilPtr());
    }

    node_ptr GetLast() const noexcept {
        return Algo::get_last(GetNilPtr());
    }

    size_type GetSize() const noexcept {
        if constexpr (SizeTraits::is_tracking_size) {
            return SizeTraitsHolder::get().get_size();
        } else {
            return Algo::count(GetNilPtr()) - 1;
        }
    }

    void SetSize(size_type new_size) noexcept {
        return SizeTraitsHolder::get().set_size(new_size);
    }

    node_ptr InsertAfter(node_ptr prev, node_ptr new_node) noexcept {
        Algo::link_after(prev, new_node);
        SizeTraitsHolder::get().increment();
        return new_node;
    }

    node_ptr EraseAfter(node_ptr prev) noexcept {
        Algo::unlink_after(prev);
        SizeTraitsHolder::get().decrement();
        return node_traits::get_next(prev);
    }

    node_ptr EraseAfter(node_ptr before_first, node_ptr last) noexcept {
        size_t count = Algo::unlink_after(before_first, last);
        SizeTraitsHolder::get().decrease(count);
        return last;
    }

    void SpliceAfter(node_ptr where, IntrusiveSlist &other, node_ptr before_first, node_ptr before_last) noexcept {
        if constexpr (SizeTraits::is_tracking_size) {
            size_type distance(Algo::distance(node_traits::get_next(before_first), node_traits::get_next(before_last)));

            other.SizeTraits::get().decrease(distance);
            SizeTraits::get().increase(distance);
        }
        Algo::transfer_after(where, before_first, before_last);
    }

    void SpliceAfter(node_ptr where, IntrusiveSlist &other, node_ptr before) noexcept {
        SpliceAfter(where, other, before, node_traits::get_next(before));
    }

    void SpliceAfter(node_ptr where, IntrusiveSlist &other) noexcept {
        SpliceAfter(where, other, other.GetNilPtr(), other.GetLast());
    }

    template<class Comp>
    void Sort(Comp &&comp) {
        const value_traits &_value_traits = ValueTraitsHolder::get();
        auto node_comp = [&_value_traits, &comp](node_ptr left, node_ptr right) {
            return comp(*_value_traits.to_value_ptr(left), *_value_traits.to_value_ptr(right));
        };
        Algo::sort(GetNilPtr(), node_comp);
    }

    template<class Comp>
    void Merge(IntrusiveSlist &other, Comp &&comp) noexcept {
        if constexpr (SizeTraits::is_tracking_size) {
            size_type new_size = GetSize() + other.GetSize();
            SetSize(new_size);
            other.SetSize(0);
        }
        const value_traits &_value_traits = ValueTraitsHolder::get();
        auto node_comp = [&_value_traits, &comp](node_ptr left, node_ptr right) {
            return comp(*_value_traits.to_value_ptr(left), *_value_traits.to_value_ptr(right));
        };
        Algo::merge(GetNilPtr(), other.GetNilPtr(), node_comp);
    }

    template<class BinaryPredicate>
    size_type Unique(BinaryPredicate &&predicate) noexcept {
        std::size_t n = 0;
        const_iterator current = cbegin();
        while (current != cend()) {
            const_iterator start = current;
            ++current;
            while (current != cend() && predicate(*current, *start)) {
                ++n;
                ++current;
            }
            erase_after(start, current);
        }
        return size_type(n);
    }

    template<class UnaryPredicate>
    size_type RemoveIf(UnaryPredicate &&predicate) noexcept {
        std::size_t n = 0;
        const_iterator prev = cbefore_begin();
        const_iterator current = cbegin();

        while (current != cend()) {
            if (predicate(*current)) {
                current = erase_after(prev);
                ++n;
            } else {
                prev = current;
                ++current;
            }
        }
        return size_type(n);
    }

public:
    template<class Iterator>
    void assign(Iterator first, Iterator last) {
        clear();
        insert_after(cbefore_begin(), first, last);
    }

    void reverse() noexcept {
        Algo::reverse(GetNilPtr());
    }

    reference front() noexcept {
        const value_traits &_value_traits = ValueTraitsHolder::get();
        return *_value_traits.to_value_ptr(GetFirst());
    }

    const_reference front() const noexcept {
        const value_traits &_value_traits = ValueTraitsHolder::get();
        return *_value_traits.to_value_ptr(GetFirst());
    }

    void push_front(reference value) noexcept {
        const value_traits &_value_traits = ValueTraitsHolder::get();
        InsertAfter(GetNilPtr(), _value_traits.to_node_ptr(value));
    }

    void pop_front() noexcept {
        EraseAfter(GetNilPtr());
    }

    iterator insert_after(const_iterator before, reference value) noexcept {
        const value_traits &_value_traits = ValueTraitsHolder::get();
        node_ptr position = before.current_node_;
        node_ptr new_node = _value_traits.to_node_ptr(value);
        InsertAfter(position, new_node);
        return iterator(new_node, GetValueTraitsPtr());
    }

    template<class Iterator>
    void insert_after(const_iterator before, Iterator first, Iterator last) noexcept {
        const_iterator current = before;
        for (; first != last; ++first) {
            current = insert_after(current, *first);
        }
    }

    iterator erase_after(const_iterator before) noexcept {
        node_ptr prev = before.current_node_;
        node_ptr after = EraseAfter(prev);
        return iterator(after, GetValueTraitsPtr());
    }

    iterator erase_after(const_iterator before_first, const_iterator last) noexcept {
        node_ptr first_node = before_first.current_node_;
        node_ptr last_node = last.current_node_;
        node_ptr result = EraseAfter(first_node, last_node);
        return iterator(result, GetValueTraitsPtr());
    }

    void clear() {
        EraseAfter(GetNilPtr(), GetEnd());
    }

    void swap(IntrusiveSlist &other) noexcept {
        Algo::swap_nodes(GetNilPtr(), other.GetNilPtr());
        std::swap(ValueTraitsHolder::get(), other.ValueTraitsHolder::get());
        std::swap(SizeTraitsHolder::get(), other.SizeTraitsHolder::get());
    }

    void splice_after(const_iterator position, IntrusiveSlist &other) noexcept {
        node_ptr where_node = position.current_node_;
        SpliceAfter(where_node, other);
    }

    void splice_after(const_iterator position, IntrusiveSlist &&other) noexcept {
        splice_after(position, other);
    }

    void splice_after(const_iterator position, IntrusiveSlist &other, const_iterator before) noexcept {
        node_ptr where_node = position.current_node_;
        node_ptr before_node = before.current_node_;
        SpliceAfter(where_node, other, before_node);
    }

    void splice_after(const_iterator position, IntrusiveSlist &&other, const_iterator before) noexcept {
        splice_after(position, other, before);
    }

    void splice_after(const_iterator position, IntrusiveSlist &other, const_iterator before_first,
                      const_iterator last) noexcept {
        node_ptr where_node = position.current_node_;
        node_ptr before_first_node = before_first.current_node_;
        node_ptr before_last_node = Algo::get_prev(before_first_node, last.current_node_);
        SpliceAfter(where_node, other, before_first_node, before_last_node);
    }

    void splice_after(const_iterator position, IntrusiveSlist &&other, const_iterator before_first,
                      const_iterator last) noexcept {
        splice_after(position, other, before_first, last);
    }

    size_type remove(const_reference value) noexcept {
        return remove_if([&value](const_reference other) { return std::equal_to<value_type>()(value, other); });
    }

    template<class UnaryPredicate>
    size_type remove_if(UnaryPredicate &&pred) noexcept {
        return RemoveIf(std::forward<UnaryPredicate>(pred));
    }

    size_type unique() noexcept {
        return unique(std::equal_to<value_type>());
    }

    template<class BinaryPredicate>
    size_type unique(BinaryPredicate &&pred) noexcept {
        return Unique(std::forward<BinaryPredicate>(pred));
    }

    void merge(IntrusiveSlist &other) noexcept {
        Merge(other, std::less<value_type>());
    }

    void merge(IntrusiveSlist &&other) noexcept {
        merge(other);
    }

    template<class Compare>
    void merge(IntrusiveSlist &other, Compare &&comp) noexcept {
        Merge(other, std::forward<Compare>(comp));
    }

    template<class Compare>
    void merge(IntrusiveSlist &&other, Compare &&comp) noexcept {
        merge(other, std::forward<Compare>(comp));
    }

    void sort() noexcept {
        sort(std::less<value_type>());
    }

    template<class Compare>
    void sort(Compare &&comp) noexcept {
        Sort(std::forward<Compare>(comp));
    }

public:
    iterator before_begin() noexcept {
        return iterator(GetNilPtr(), GetValueTraitsPtr());
    }

    iterator last() noexcept {
        return iterator(GetLast(), GetValueTraitsPtr());
    }

    iterator begin() noexcept {
        return iterator(GetFirst(), GetValueTraitsPtr());
    }

    iterator end() noexcept {
        return iterator(GetEnd(), GetValueTraitsPtr());
    }

    const_iterator before_begin() const noexcept {
        return const_iterator(GetNilPtr(), GetValueTraitsPtr());
    }

    const_iterator last() const noexcept {
        return const_iterator(GetLast(), GetValueTraitsPtr());
    }

    const_iterator begin() const noexcept {
        return const_iterator(GetFirst(), GetValueTraitsPtr());
    }

    const_iterator end() const noexcept {
        return const_iterator(GetEnd(), GetValueTraitsPtr());
    }

    const_iterator cbefore_begin() const noexcept {
        return const_iterator(GetNilPtr(), GetValueTraitsPtr());
    }

    const_iterator clast() const noexcept {
        return const_iterator(GetLast(), GetValueTraitsPtr());
    }

    const_iterator cbegin() const noexcept {
        return const_iterator(GetFirst(), GetValueTraitsPtr());
    }

    const_iterator cend() const noexcept {
        return const_iterator(GetEnd(), GetValueTraitsPtr());
    }

public:
    bool empty() const noexcept {
        return Algo::unique(GetNilPtr());
    }

    size_type size() const noexcept {
        return GetSize();
    }

public:
    friend bool operator==(const IntrusiveSlist &left, const IntrusiveSlist &right) noexcept {
        return std::equal(left.begin(), left.end(), right.begin(), right.end());
    }

    friend bool operator!=(const IntrusiveSlist &left, const IntrusiveSlist &right) noexcept {
        return !(left == right);
    }

    friend bool operator<(const IntrusiveSlist &left, const IntrusiveSlist &right) noexcept {
        return std::lexicographical_compare(left.begin(), left.end(), right.begin(), right.end());
    }

    friend bool operator>(const IntrusiveSlist &left, const IntrusiveSlist &right) noexcept {
        return right < left;
    }

    friend bool operator<=(const IntrusiveSlist &left, const IntrusiveSlist &right) noexcept {
        return !(right < left);
    }

    friend bool operator>=(const IntrusiveSlist &left, const IntrusiveSlist &right) noexcept {
        return !(left < right);
    }

    friend void swap(IntrusiveSlist &left, IntrusiveSlist &right) noexcept {
        left.swap(right);
    }

private:
    node nil_node_;
};

struct DefaultSlistHookApplier {
    template<class ValueType>
    struct Apply {
        using type = typename HookToValueTraits<ValueType, typename ValueType::slist_default_hook_type>::type;
    };
};

template<class HookType>
struct DefaultSlistHook {
    using slist_default_hook_type = HookType;
};

template<class VoidPointer, class Tag, bool IsAutoUnlink>
class SlistBaseHook
    : public GenericHook<CircularSlistAlgo<SlistNodeTraits<VoidPointer>>, SlistNodeTraits<VoidPointer>, Tag,
                         IsAutoUnlink>,
      public std::conditional_t<std::is_same_v<Tag, DefaultHookTag>,
                                DefaultSlistHook<GenericHook<CircularSlistAlgo<SlistNodeTraits<VoidPointer>>,
                                                             SlistNodeTraits<VoidPointer>, Tag, IsAutoUnlink>>,
                                NotDefaultHook> {};

struct SlistDefaults {
    using proto_value_traits = DefaultSlistHookApplier;
    using size_type = std::size_t;
};

struct SlistHookDefaults {
    using void_pointer = void *;
    using tag = DefaultHookTag;
    static const bool is_auto_unlink = true;
};

}// namespace detail
}// namespace lu

#endif