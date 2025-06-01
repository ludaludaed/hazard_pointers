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

template <class ValueType, class KeyCompare, class KeySelect, class Backoff>
struct OrderedListBase {
    using value_type = ValueType;
    using key_type = typename KeySelect::type;

    using difference_type = std::ptrdiff_t;
    using pointer = value_type *;
    using const_pointer = const value_type *;
    using reference = value_type &;
    using const_reference = const value_type &;

    using key_compare = KeyCompare;
    using key_select = KeySelect;

    using node = OrderedListNode<ValueType>;
    using node_ptr = node *;
    using node_marked_ptr = lu::marked_ptr<node>;

    using node_accessor = lu::guarded_ptr<node>;

    struct position {
        node_ptr cur{};
        node_marked_ptr next{};
        std::atomic<node_marked_ptr> *prev_pointer{};

        lu::hazard_pointer prev_guard{lu::make_hazard_pointer()};
        lu::hazard_pointer cur_guard{lu::make_hazard_pointer()};
        lu::hazard_pointer next_guard{lu::make_hazard_pointer()};
    };

    explicit OrderedListBase(const key_compare &compare = {})
        : key_compare_(compare) {}

    ~OrderedListBase() {
        auto current = head_.load();
        while (current) {
            auto next = current->next.load();
            delete current.get();
            current = next;
        }
    }

    bool unlink(const position &pos) {
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

    bool link(const position &pos, node_ptr new_node) {
        node_marked_ptr cur(pos.cur, 0);
        new_node->next.store(cur);
        if (pos.prev_pointer->compare_exchange_weak(cur, node_marked_ptr(new_node, 0))) {
            return true;
        } else {
            new_node->next.store(nullptr);
            return false;
        }
    }

    bool find(const key_type &key, position &pos) const {
        Backoff backoff;
        return find(key, pos, backoff);
    }

    bool find(const key_type &key, position &pos, Backoff &backoff) const {
        auto head = const_cast<std::atomic<node_marked_ptr> *>(&head_);

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
                if (!key_compare_(key_select_(pos.cur->value), key)) {
                    return !key_compare_(key, key_select_(pos.cur->value));
                }
                pos.prev_pointer = &(pos.cur->next);
                pos.prev_guard.reset_protection(pos.cur);
            }
            pos.cur_guard.reset_protection(pos.next.get());
            pos.cur = pos.next;
        }
    }

    bool insert_node(std::unique_ptr<node> new_node) {
        Backoff backoff;
        position pos;
        const key_type &key = key_select_(new_node->value);

        while (true) {
            if (find(key, pos, backoff)) {
                return false;
            }
            if (link(pos, new_node.get())) {
                new_node.release();
                return true;
            }
            backoff();
        }
    }

    template <class NodeFactory>
    bool insert_node(const key_type &key, NodeFactory &&factory) {
        Backoff backoff;
        position pos;
        std::unique_ptr<node> new_node;

        while (true) {
            if (find(key, pos, backoff)) {
                return false;
            }
            if (!new_node) {
                new_node = factory();
            }
            if (link(pos, new_node.get())) {
                new_node.release();
                return true;
            }
            backoff();
        }
    }

    node_accessor extract_node(const key_type &key) {
        Backoff backoff;
        position pos;
        while (find(key, pos, backoff)) {
            if (unlink(pos)) {
                return node_accessor(std::move(pos.cur_guard), pos.cur);
            }
            backoff();
        }
        return node_accessor();
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
                unlink(pos);
            }
        }
    }

    bool contains(const key_type &key) const {
        position pos;
        return find(key, pos);
    }

    bool empty() const {
        return !head_.load();
    }

    node_accessor front() const {
        auto head_guard = lu::make_hazard_pointer();
        auto head = head_guard.protect(head_, [](node_marked_ptr ptr) { return ptr.get(); });
        if (head) {
            return node_accessor(std::move(head_guard), head);
        } else {
            return node_accessor();
        }
    }

    CACHE_LINE_ALIGNAS std::atomic<node_marked_ptr> head_{};
    NO_UNIQUE_ADDRESS key_compare key_compare_{};
    NO_UNIQUE_ADDRESS key_select key_select_{};
};

template <class Container, bool IsConst>
class OrderedListIterator {
    template <class, class, class, class>
    friend class OrderedList;

    class DummyNonConstIter;
    using NonConstIter
            = std::conditional_t<IsConst, OrderedListIterator<Container, false>, DummyNonConstIter>;

    using node_ptr = typename Container::node_ptr;
    using node_marked_ptr = typename Container::node_marked_ptr;
    using node_accessor = typename Container::node_accessor;

    using position = typename Container::position;

public:
    using value_type = typename Container::value_type;
    using difference_type = typename Container::difference_type;
    using pointer = std::conditional_t<IsConst, typename Container::const_pointer,
                                       typename Container::pointer>;
    using reference = std::conditional_t<IsConst, typename Container::const_reference,
                                         typename Container::reference>;
    using iterator_category = std::forward_iterator_tag;

private:
    OrderedListIterator(node_accessor current_node, const Container *list) noexcept
        : current_node_(std::move(current_node))
        , list_(list) {}

public:
    OrderedListIterator() = default;

    OrderedListIterator(const OrderedListIterator &other) noexcept
        : list_(other.list_) {
        if (other.current_node_) {
            auto guard = lu::make_hazard_pointer();
            guard.reset_protection(other.current_node_.get());
            current_node_ = node_accessor(std::move(guard), other.current_node_.get());
        }
    }

    OrderedListIterator(const NonConstIter &other) noexcept
        : list_(other.list_) {
        if (other.current_node_) {
            auto guard = lu::make_hazard_pointer();
            guard.reset_protection(other.current_node_.get());
            current_node_ = node_accessor(std::move(guard), other.current_node_.get());
        }
    }

    OrderedListIterator(OrderedListIterator &&other) noexcept {
        swap(other);
    }

    OrderedListIterator(NonConstIter &&other) noexcept {
        swap(other);
    }

    OrderedListIterator &operator=(const OrderedListIterator &other) noexcept {
        OrderedListIterator temp(other);
        swap(temp);
        return *this;
    }

    OrderedListIterator &operator=(const NonConstIter &other) noexcept {
        OrderedListIterator temp(other);
        swap(temp);
        return *this;
    }

    OrderedListIterator &operator=(OrderedListIterator &&other) noexcept {
        OrderedListIterator temp(std::move(other));
        swap(temp);
        return *this;
    }

    OrderedListIterator &operator=(NonConstIter &&other) noexcept {
        OrderedListIterator temp(std::move(other));
        swap(temp);
        return *this;
    }

    OrderedListIterator &operator++() noexcept {
        increment();
        return *this;
    }

    OrderedListIterator operator++(int) noexcept {
        OrderedListIterator copy(*this);
        increment();
        return copy;
    }

    reference operator*() const noexcept {
        return *this->operator->();
    }

    pointer operator->() const noexcept {
        return &current_node_->value;
    }

    friend bool operator==(const OrderedListIterator &left, const OrderedListIterator &right) {
        return left.current_node_ == right.current_node_;
    }

    friend bool operator!=(const OrderedListIterator &left, const OrderedListIterator &right) {
        return !(left == right);
    }

    void swap(OrderedListIterator &other) {
        using std::swap;
        swap(current_node_, other.current_node_);
        swap(list_, other.list_);
    }

    friend void swap(OrderedListIterator &left, OrderedListIterator &right) {
        left.swap(right);
    }

private:
    void increment() noexcept {
        position pos;
        auto next = pos.next_guard.protect(current_node_->next,
                                           [](node_marked_ptr ptr) { return ptr.get(); });

        if (next.is_marked()) {
            list_->find(list_->key_select_(current_node_->value), pos);
            current_node_ = node_accessor(std::move(pos.cur_guard), pos.cur);
        } else {
            current_node_ = node_accessor(std::move(pos.next_guard), next);
        }
    }

private:
    node_accessor current_node_{};
    const Container *list_{};
};

template <class ValueType, class KeyCompare, class KeySelect, class Backoff>
class OrderedList : private OrderedListBase<ValueType, KeyCompare, KeySelect, Backoff> {
    template <class, bool>
    friend class OrderedListIterator;

    using Base = OrderedListBase<ValueType, KeyCompare, KeySelect, Backoff>;

    using position = typename Base::position;

    using node = typename Base::node;
    using node_ptr = typename Base::node_ptr;
    using node_marked_ptr = typename Base::node_marked_ptr;
    using node_accessor = typename Base::node_accessor;

public:
    using value_type = typename Base::value_type;
    using key_type = typename Base::key_type;

    using difference_type = typename Base::difference_type;
    using pointer = typename Base::pointer;
    using const_pointer = typename Base::const_pointer;
    using reference = typename Base::reference;
    using const_reference = typename Base::const_reference;

    using key_compare = typename Base::key_compare;
    using key_select = typename Base::key_select;

    using accessor = lu::guarded_ptr<
            std::conditional_t<std::is_same_v<value_type, key_type>, const value_type, value_type>>;

    using iterator = OrderedListIterator<Base, std::is_same_v<value_type, key_type>>;
    using const_iterator = OrderedListIterator<Base, true>;

public:
    using Base::Base;
    using Base::clear;
    using Base::contains;
    using Base::empty;

    bool insert(const value_type &value) {
        return emplace(value);
    }

    bool insert(value_type &&value) {
        return emplace(std::move(value));
    }

    template <class... Args>
    bool emplace(Args &&...args) {
        if constexpr (std::is_invocable_v<key_select, Args...>) {
            const key_type &key = Base::key_select_(args...);
            auto node_factory = [args = std::forward_as_tuple(args...)]() {
                return std::apply(
                        [](auto &&...args) {
                            return std::make_unique<node>(std::forward<decltype(args)>(args)...);
                        },
                        std::move(args));
            };
            return Base::insert_node(key, std::move(node_factory));
        } else {
            auto new_node = std::make_unique<node>(std::forward<Args>(args)...);
            return Base::insert_node(std::move(new_node));
        }
    }

    bool erase(const key_type &key) {
        return bool(Base::extract_node(key));
    }

    accessor extract(const key_type &key) {
        auto [guard, node_ptr] = Base::extract_node(key).unpack();
        return accessor(std::move(guard), std::addressof(node_ptr->value));
    }

    iterator find(const key_type &key) {
        position pos;
        if (!Base::find(key, pos)) {
            return end();
        }
        return iterator(node_accessor(std::move(pos.cur_guard), pos.cur), this);
    }

    const_iterator find(const key_type &key) const {
        position pos;
        if (!Base::find(key, pos)) {
            return end();
        }
        return const_iterator(node_accessor(std::move(pos.cur_guard), pos.cur), this);
    }

    iterator lower_bound(const key_type &key) {
        position pos;
        Base::find(key, pos);
        if (!pos.cur) {
            return end();
        }
        return iterator(node_accessor(std::move(pos.cur_guard), pos.cur), this);
    }

    const_iterator lower_bound(const key_type &key) const {
        position pos;
        Base::find(key, pos);
        if (!pos.cur) {
            return end();
        }
        return const_iterator(node_accessor(std::move(pos.cur_guard), pos.cur), this);
    }

    iterator begin() {
        return iterator(Base::front(), this);
    }

    iterator end() {
        return iterator();
    }

    const_iterator cbegin() const {
        return const_iterator(Base::front(), this);
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
};

struct OrderedListDefaults {
    using key_compare = void;
    using backoff = void;
};

}// namespace detail
}// namespace lu

namespace lu {
namespace detail {

template <class ValueType, class... Options>
struct make_ordered_list_set {
    using pack_options = typename GetPackOptions<OrderedListDefaults, Options...>::type;

    using key_compare
            = GetOrDefault<typename pack_options::key_compare, std::less<const ValueType>>;
    using backoff = GetOrDefault<typename pack_options::backoff, lu::none_backoff>;

    struct KeySelect {
        using type = ValueType;

        template <class T, class = std::enable_if_t<std::is_same_v<std::remove_cvref_t<T>, type>>>
        const T &operator()(const T &value) const noexcept {
            return value;
        }
    };

    using type = OrderedList<ValueType, key_compare, KeySelect, backoff>;
};

template <class KeyType, class ValueType, class... Options>
struct make_ordered_list_map {
    using pack_options = typename GetPackOptions<OrderedListDefaults, Options...>::type;

    using key_compare = GetOrDefault<typename pack_options::key_compare, std::less<const KeyType>>;
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

    using type = OrderedList<value_type, key_compare, KeySelect, backoff>;
};

}// namespace detail

template <class ValueType, class... Options>
using ordered_list_set = typename detail::make_ordered_list_set<ValueType, Options...>::type;

template <class KeyType, class ValueType, class... Options>
using ordered_list_map =
        typename detail::make_ordered_list_map<KeyType, ValueType, Options...>::type;

}// namespace lu

#endif
