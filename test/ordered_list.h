#ifndef __ORDERED_LIST_H__
#define __ORDERED_LIST_H__

#include <lu/detail/utils.h>
#include <lu/hazard_pointer.h>
#include <lu/intrusive/detail/utils.h>
#include <lu/utils/backoff.h>
#include <lu/utils/marked_ptr.h>

#include <atomic>
#include <cstddef>
#include <functional>
#include <memory>
#include <tuple>
#include <type_traits>
#include <utility>


namespace lu {
namespace detail {

template <class ValueType>
struct OrderedListNode : public lu::hazard_pointer_obj_base<OrderedListNode<ValueType>> {
    template <class... Args>
    explicit OrderedListNode(Args &&...args)
        : value(std::forward<Args>(args)...) {}

    ValueType value;
    std::atomic<lu::marked_ptr<OrderedListNode>> next{};
};

template <class ValueType>
struct OrderedListNodeTraits {
    using node = OrderedListNode<ValueType>;
    using node_ptr = node *;
    using node_marked_ptr = lu::marked_ptr<node>;
};

template <class NodeTraits>
struct OrderedListAlgo {
    using node = typename NodeTraits::node;
    using node_ptr = typename NodeTraits::node_ptr;
    using node_marked_ptr = typename NodeTraits::node_marked_ptr;

    struct position {
        node_ptr cur;
        node_marked_ptr next;
        std::atomic<node_marked_ptr> *prev_pointer;

        lu::hazard_pointer prev_guard{lu::make_hazard_pointer()};
        lu::hazard_pointer cur_guard{lu::make_hazard_pointer()};
        lu::hazard_pointer next_guard{lu::make_hazard_pointer()};
    };

    static bool unlink(position &pos) {
        node_marked_ptr next(pos.next, 0);
        if (pos.cur->next.compare_exchange_weak(next, node_marked_ptr(pos.next, 1))) {
            node_marked_ptr cur(pos.cur, 0);
            if (pos.prev_pointer->compare_exchange_weak(cur, next)) {
                cur->retire();
            }
            return true;
        }
        return false;
    }

    static bool link(position &pos, node_ptr new_node) {
        node_marked_ptr cur(pos.cur, 0);
        new_node->next.store(cur);
        if (pos.prev_pointer->compare_exchange_weak(cur, node_marked_ptr(new_node, 0))) {
            return true;
        } else {
            new_node->next.store(nullptr);
            return false;
        }
    }

    template <class KeyType, class Backoff, class KeyCompare, class KeySelect>
    static bool find(std::atomic<node_marked_ptr> *head, const KeyType &key, position &pos,
                     Backoff &backoff, KeyCompare &&comp, KeySelect &&key_select) {
    try_again:
        pos.prev_pointer = head;
        pos.cur = pos.cur_guard.protect(*head, [](node_marked_ptr ptr) { return ptr.get(); });

        while (true) {
            if (!pos.cur) {
                pos.cur = {};
                pos.next = {};
                return false;
            }

            pos.next = pos.next_guard.protect(pos.cur->next,
                                              [](node_marked_ptr ptr) { return ptr.get(); });

            if (pos.prev_pointer->load().raw() != pos.cur) {
                backoff();
                goto try_again;
            }

            if (pos.next.is_marked()) {
                node_marked_ptr not_marked_cur(pos.cur, 0);
                if (!pos.prev_pointer->compare_exchange_weak(not_marked_cur,
                                                             node_marked_ptr(pos.next, 0))) {
                    backoff();
                    goto try_again;
                }
                pos.cur->retire();
            } else {
                if (!comp(key_select(pos.cur->value), key)) {
                    return !comp(key, key_select(pos.cur->value));
                }
                pos.prev_pointer = &(pos.cur->next);
                pos.prev_guard.reset_protection(pos.cur);
            }
            pos.cur_guard.reset_protection(pos.next.get());
            pos.cur = pos.next;
        }
    }
};

template <class ValueType, class KeyCompare, class KeySelect, class Backoff>
class OrderedList {
    template <class Types, bool IsConst>
    class Iterator {
        friend class OrderedList;

        class DummyNonConstIter;
        using NonConstIter = std::conditional_t<IsConst, Iterator<Types, false>, DummyNonConstIter>;

        using node_ptr = typename Types::node_ptr;
        using node_marked_ptr = typename Types::node_marked_ptr;
        using position = typename Types::position;

    public:
        using value_type = typename Types::value_type;
        using difference_type = typename Types::difference_type;
        using pointer = std::conditional_t<IsConst, typename Types::const_pointer,
                                           typename Types::pointer>;
        using reference = std::conditional_t<IsConst, typename Types::const_reference,
                                             typename Types::reference>;
        using iterator_category = std::forward_iterator_tag;

    private:
        Iterator(lu::hazard_pointer guard, node_ptr current, const OrderedList *list) noexcept
            : guard_(std::move(guard))
            , current_(current)
            , list_(list) {}

    public:
        Iterator() = default;

        Iterator(const Iterator &other) noexcept
            : guard_(lu::make_hazard_pointer())
            , current_(other.current_)
            , list_(other.list_) {
            guard_.reset_protection(current_);
        }

        Iterator(const NonConstIter &other) noexcept
            : guard_(lu::make_hazard_pointer())
            , current_(other.current_)
            , list_(other.list_) {
            guard_.reset_protection(current_);
        }

        Iterator(Iterator &&other) noexcept { swap(other); }

        Iterator(NonConstIter &&other) noexcept { swap(other); }

        Iterator &operator=(const Iterator &other) noexcept {
            Iterator temp(other);
            swap(temp);
            return *this;
        }

        Iterator &operator=(const NonConstIter &other) noexcept {
            Iterator temp(other);
            swap(temp);
            return *this;
        }

        Iterator &operator=(Iterator &&other) noexcept {
            Iterator temp(std::move(other));
            swap(temp);
            return *this;
        }

        Iterator &operator=(NonConstIter &&other) noexcept {
            Iterator temp(std::move(other));
            swap(temp);
            return *this;
        }

        Iterator &operator++() noexcept {
            increment();
            return *this;
        }

        Iterator operator++(int) noexcept {
            Iterator copy(*this);
            increment();
            return copy;
        }

        reference operator*() const noexcept { return *this->operator->(); }

        pointer operator->() const noexcept { return &current_->value; }

        friend bool operator==(const Iterator &left, const Iterator &right) {
            return left.current_ == right.current_;
        }

        friend bool operator!=(const Iterator &left, const Iterator &right) {
            return !(left == right);
        }

        void swap(Iterator &other) {
            std::swap(guard_, other.guard_);
            std::swap(current_, other.current_);
            std::swap(list_, other.list_);
        }

    private:
        void increment() noexcept {
            auto next_guard = lu::make_hazard_pointer();
            auto next = next_guard.protect(current_->next,
                                           [](node_marked_ptr ptr) { return ptr.get(); });

            if (next.is_marked()) {
                next_guard = lu::hazard_pointer();
                position new_pos;
                list_->find(list_->key_select_(current_->value), new_pos);

                guard_ = std::move(new_pos.cur_guard);
                current_ = new_pos.cur;
            } else {
                guard_ = std::move(next_guard);
                current_ = next;
            }
        }

    private:
        lu::hazard_pointer guard_{};
        node_ptr current_{};
        const OrderedList *list_{};
    };

public:
    using value_type = ValueType;
    using key_type = typename KeySelect::type;

    using difference_type = std::ptrdiff_t;
    using pointer = value_type *;
    using const_pointer = const value_type *;
    using reference = value_type &;
    using const_reference = const value_type &;

    using compare = KeyCompare;
    using key_select = KeySelect;

    using accessor = lu::guarded_ptr<
            std::conditional_t<std::is_same_v<value_type, key_type>, const value_type, value_type>>;

    using iterator = Iterator<OrderedList, std::is_same_v<value_type, key_type>>;
    using const_iterator = Iterator<OrderedList, true>;

private:
    using node_traits = OrderedListNodeTraits<value_type>;
    using node = typename node_traits::node;
    using node_ptr = typename node_traits::node_ptr;
    using node_marked_ptr = typename node_traits::node_marked_ptr;

    using Algo = OrderedListAlgo<node_traits>;
    using position = typename Algo::position;

public:
    explicit OrderedList(const compare &compare = {})
        : key_compare_(compare) {}

    OrderedList(const OrderedList &other) = delete;

    OrderedList(OrderedList &&other) = delete;

    ~OrderedList() {
        auto current = head_.load();
        while (current) {
            auto next = current->next.load();
            delete current.get();
            current = next;
        }
    }

private:
    bool find(const key_type &key, position &pos) const {
        Backoff backoff;
        return find(key, pos, backoff);
    }

    bool find(const key_type &key, position &pos, Backoff &backoff) const {
        auto head_ptr = const_cast<std::atomic<node_marked_ptr> *>(&head_);
        return Algo::find(head_ptr, key, pos, backoff, key_compare_, key_select_);
    }

public:
    bool insert(const value_type &value) { return emplace(value); }

    bool insert(value_type &&value) { return emplace(std::move(value)); }

    template <class... Args>
    bool emplace(Args &&...args) {
        Backoff backoff;
        position pos;
        std::unique_ptr<node> new_node = nullptr;

        if constexpr (key_select::template inplace_extractable<Args...>) {
            const key_type &key = key_select_(args...);
            while (true) {
                if (find(key, pos, backoff)) {
                    return false;
                }
                if (!new_node) {
                    new_node = std::make_unique<node>(std::forward<Args>(args)...);
                }
                if (Algo::link(pos, new_node.get())) {
                    new_node.release();
                    return true;
                }
                backoff();
            }
        } else {
            new_node = std::make_unique<node>(std::forward<Args>(args)...);
            const key_type &key = key_select_(new_node->value);
            while (true) {
                if (find(key, pos, backoff)) {
                    return false;
                }
                if (Algo::link(pos, new_node.get())) {
                    new_node.release();
                    return true;
                }
                backoff();
            }
        }
    }

    template <class KeyType, class... Args>
    bool try_emplace(KeyType &&key, Args &&...args) {
        Backoff backoff;
        position pos;
        std::unique_ptr<node> new_node = nullptr;
        while (true) {
            if (find(key, pos, backoff)) {
                return false;
            }
            if (!new_node) {
                new_node = std::make_unique<node>(
                        std::piecewise_construct,
                        std::forward_as_tuple(std::forward<KeyType>(key)),
                        std::forward_as_tuple(std::forward<Args>(args)...));
            }
            if (Algo::link(pos, new_node.get())) {
                new_node.release();
                return true;
            }
            backoff();
        }
    }

    bool erase(const key_type &key) {
        Backoff backoff;
        position pos;
        while (find(key, pos, backoff)) {
            if (Algo::unlink(pos)) {
                return true;
            }
            backoff();
        }
        return false;
    }

    accessor extract(const key_type &key) {
        Backoff backoff;
        position pos;
        while (find(key, pos, backoff)) {
            if (Algo::unlink(pos)) {
                return accessor(std::move(pos.cur_guard), &pos.cur->value);
            }
            backoff();
        }
        return accessor();
    }

    void clear() {
        Backoff backoff;
        lu::hazard_pointer head_guard = lu::make_hazard_pointer();
        position pos;
        while (true) {
            auto head = head_guard.protect(head_, [](node_marked_ptr ptr) { return ptr.get(); });
            if (!head) {
                break;
            }
            if (find(key_select_(head->value), pos, backoff) && pos.cur == head.get()) {
                Algo::unlink(pos);
            }
        }
    }

    iterator find(const key_type &key) {
        position pos;
        if (find(key, pos)) {
            return iterator(std::move(pos.cur_guard), pos.cur, this);
        }
        return end();
    }

    const_iterator find(const key_type &key) const {
        position pos;
        if (find(key, pos)) {
            return const_iterator(std::move(pos.cur_guard), pos.cur, this);
        }
        return end();
    }

    iterator find_no_less(const key_type &key) {
        position pos;
        find(key, pos);
        if (pos.cur) {
            return iterator(std::move(pos.cur_guard), pos.cur, this);
        }
        return end();
    }

    const_iterator find_no_less(const key_type &key) const {
        position pos;
        find(key, pos);
        if (pos.cur) {
            return const_iterator(std::move(pos.cur_guard), pos.cur, this);
        }
        return end();
    }

    bool contains(const key_type &key) const {
        position pos;
        return find(key, pos);
    }

    bool empty() const { return !head_.load(); }

    iterator begin() {
        auto head_guard = lu::make_hazard_pointer();
        auto head = head_guard.protect(head_, [](node_marked_ptr ptr) { return ptr.get(); });
        return iterator(std::move(head_guard), head, this);
    }

    iterator end() { return iterator(); }

    const_iterator cbegin() const {
        auto head_guard = lu::make_hazard_pointer();
        auto head = head_guard.protect(head_, [](node_marked_ptr ptr) { return ptr.get(); });
        return const_iterator(std::move(head_guard), head, this);
    }

    const_iterator cend() const { return const_iterator(); }

    const_iterator begin() const { return cbegin(); }

    const_iterator end() const { return cend(); }

private:
    CACHE_LINE_ALIGNAS std::atomic<node_marked_ptr> head_{};
    NO_UNIQUE_ADDRESS compare key_compare_{};
    NO_UNIQUE_ADDRESS key_select key_select_{};
};

struct OrderedListDefaults {
    using compare = void;
    using backoff = void;
};

}// namespace detail
}// namespace lu

namespace lu {
namespace detail {

template <class ValueType, class... Options>
struct make_ordered_list_set {
    using pack_options = typename GetPackOptions<OrderedListDefaults, Options...>::type;

    using compare = GetOrDefault<typename pack_options::compare, std::less<const ValueType>>;
    using backoff = GetOrDefault<typename pack_options::backoff, lu::none_backoff>;

    struct KeySelect {
        using type = ValueType;

        template <class... Args>
        struct InplaceExtractable {
            static constexpr bool value = false;
        };

        template <class Arg>
        struct InplaceExtractable<Arg> {
            static constexpr bool value = std::is_same_v<std::remove_cvref_t<Arg>, ValueType>;
        };

        template <class... Args>
        static constexpr bool inplace_extractable = InplaceExtractable<Args...>::value;

        template <class T, class = std::enable_if_t<std::is_same_v<std::remove_cvref_t<T>, type>>>
        T &&operator()(T &&value) const noexcept {
            return std::forward<T>(value);
        }
    };

    using type = OrderedList<ValueType, compare, KeySelect, backoff>;
};

template <class KeyType, class ValueType, class... Options>
struct make_ordered_list_map {
    using pack_options = typename GetPackOptions<OrderedListDefaults, Options...>::type;

    using compare = GetOrDefault<typename pack_options::compare, std::less<const KeyType>>;
    using backoff = GetOrDefault<typename pack_options::backoff, lu::none_backoff>;

    using value_type = std::pair<const KeyType, ValueType>;

    struct KeySelect {
        using type = KeyType;

        template <class... Args>
        struct InplaceExtractable {
            static constexpr bool value = false;
        };

        template <class Arg1, class Arg2>
        struct InplaceExtractable<Arg1, Arg2> {
            static constexpr bool value = std::is_same_v<std::remove_cvref_t<Arg1>, KeyType>;
        };

        template <class Arg1, class Arg2>
        struct InplaceExtractable<std::pair<Arg1, Arg2>> {
            static constexpr bool value = std::is_same_v<std::remove_cvref_t<Arg1>, KeyType>;
        };

        template <class... Args>
        static constexpr bool inplace_extractable = InplaceExtractable<Args...>::value;

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

    using type = OrderedList<value_type, compare, KeySelect, backoff>;
};

}// namespace detail

template <class ValueType, class... Options>
using ordered_list_set = typename detail::make_ordered_list_set<ValueType, Options...>::type;

template <class KeyType, class ValueType, class... Options>
using ordered_list_map =
        typename detail::make_ordered_list_map<KeyType, ValueType, Options...>::type;

}// namespace lu

#endif
