#ifndef __ORDERED_LIST_H__
#define __ORDERED_LIST_H__

#include <back_off.h>
#include <hazard_pointer.h>
#include <intrusive/utils.h>
#include <marked_ptr.h>

#include <cstddef>
#include <type_traits>


namespace lu {
namespace detail {

template<class ValueType>
struct OrderedListNode : public lu::hazard_pointer_obj_base<OrderedListNode<ValueType>> {
public:
    template<class... Args>
    explicit OrderedListNode(Args &&...args)
        : value(std::forward<Args>(args)...) {}

public:
    ValueType value;
    std::atomic<lu::marked_ptr<OrderedListNode>> next{};
};

template<class ValueType, class KeyCompare, class KeySelect, class Backoff>
class OrderedList : private lu::detail::EmptyBaseHolder<KeyCompare>, private lu::detail::EmptyBaseHolder<KeySelect> {

    using KeyCompareHolder = lu::detail::EmptyBaseHolder<KeyCompare>;
    using KeySelectHolder = lu::detail::EmptyBaseHolder<KeySelect>;

    using node_type = OrderedListNode<ValueType>;
    using node_ptr = node_type *;
    using node_marked_ptr = lu::marked_ptr<node_type>;

    using list_ptr = const OrderedList *;

    struct position {
        node_ptr cur;
        node_marked_ptr next;
        std::atomic<node_marked_ptr> *prev_pointer;

        lu::hazard_pointer cur_guard{lu::make_hazard_pointer()};
        lu::hazard_pointer next_guard{lu::make_hazard_pointer()};
        lu::hazard_pointer prev_guard{lu::make_hazard_pointer()};
    };

    template<class Types, bool IsConst>
    class OrderedListIterator {
        template<class, class, class, class>
        friend class OrderedList;

        class DummyNonConstIter;
        using NonConstIter = std::conditional_t<IsConst, OrderedListIterator<Types, false>, DummyNonConstIter>;

        using list_ptr = typename Types::list_ptr;
        using node_ptr = typename Types::node_ptr;
        using node_marked_ptr = typename Types::node_marked_ptr;
        using position = typename Types::position;

    public:
        using value_type = typename Types::value_type;
        using difference_type = typename Types::difference_type;
        using pointer = std::conditional_t<IsConst, typename Types::const_pointer, typename Types::pointer>;
        using reference = std::conditional_t<IsConst, typename Types::const_reference, typename Types::reference>;
        using iterator_category = std::forward_iterator_tag;


    private:
        OrderedListIterator(lu::hazard_pointer guard, node_ptr current, list_ptr list) noexcept
            : guard_(std::move(guard))
            , current_(current)
            , list_(list) {}

    public:
        OrderedListIterator() = default;

        OrderedListIterator(const OrderedListIterator &other) noexcept
            : guard_(lu::make_hazard_pointer())
            , current_(other.current_)
            , list_(other.list_) {
            guard_.reset_protection(current_);
        }

        OrderedListIterator(const NonConstIter &other) noexcept
            : guard_(lu::make_hazard_pointer())
            , current_(other.current_)
            , list_(other.list_) {
            guard_.reset_protection(current_);
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
            return &current_->value;
        }

        friend bool operator==(const OrderedListIterator &left, const OrderedListIterator &right) {
            return left.current_ == right.current_;
        }

        friend bool operator!=(const OrderedListIterator &left, const OrderedListIterator &right) {
            return !(left == right);
        }

        void swap(OrderedListIterator &other) {
            std::swap(guard_, other.guard_);
            std::swap(current_, other.current_);
            std::swap(list_, other.list_);
        }

    private:
        void increment() noexcept {
            auto next_guard = lu::make_hazard_pointer();
            auto next = next_guard.protect(current_->next, [](node_marked_ptr ptr) { return ptr.get(); });
            if (next.is_marked()) {
                position new_pos;
                list_->find(list_->select_key(current_->value), new_pos);
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
        list_ptr list_{};
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

    static constexpr bool is_key_value = !std::is_same_v<value_type, key_type>;

    using guarded_ptr = std::conditional_t<is_key_value, lu::guarded_ptr<ValueType>, lu::guarded_ptr<const ValueType>>;

    using iterator = OrderedListIterator<OrderedList, !is_key_value>;
    using const_iterator = OrderedListIterator<OrderedList, true>;

private:
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
            new_node->next.store({});
            return false;
        }
    }

    template<class _KeyCompare, class _KeySelect>
    static bool find(std::atomic<node_marked_ptr> *head, const key_type &key, position &pos, _KeyCompare &&comp,
                     _KeySelect &&key_select) {
        Backoff back_off;

    try_again:
        pos.prev_pointer = head;

        pos.cur = pos.cur_guard.protect(*head, [](node_marked_ptr ptr) { return ptr.get(); });

        while (true) {
            if (!pos.cur) {
                pos.cur = {};
                pos.next = {};
                return false;
            }

            pos.next = pos.next_guard.protect(pos.cur->next, [](node_marked_ptr ptr) { return ptr.get(); });

            if (pos.prev_pointer->load().raw() != pos.cur) {
                back_off();
                goto try_again;
            }

            if (pos.next.is_marked()) {
                node_marked_ptr not_marked_cur(pos.cur, 0);
                if (pos.prev_pointer->compare_exchange_weak(not_marked_cur, node_marked_ptr(pos.next, 0))) {
                    pos.cur->retire();
                } else {
                    back_off();
                    goto try_again;
                }
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

public:
    explicit OrderedList(const compare &compare = {}, const key_select &key_select = {})
        : KeyCompareHolder(compare)
        , KeySelectHolder(key_select) {}

    OrderedList(const OrderedList &other) = delete;

    OrderedList(OrderedList &&other) = delete;

    ~OrderedList() {
        clear();
    }

private:
    decltype(auto) select_key(const value_type &value) const {
        auto key_select = KeySelectHolder::get();
        return key_select(value);
    }

    bool find(const key_type &key, position &pos) const {
        auto comp = KeyCompareHolder::get();
        auto key_select = KeySelectHolder::get();

        return find(const_cast<std::atomic<node_marked_ptr> *>(&head_), key, pos, comp, key_select);
    }

    bool insert_node(node_ptr new_node) {
        auto key_select = KeySelectHolder::get();
        Backoff back_off;
        position pos;
        while (true) {
            if (find(key_select(new_node->value), pos)) {
                return false;
            }
            if (link(pos, new_node)) {
                return true;
            }
            back_off();
        }
    }

public:
    bool insert(const value_type &value) {
        return emplace(value);
    }

    bool insert(value_type &&value) {
        return emplace(std::move(value));
    }

    template<class... Args>
    bool emplace(Args &&...args) {
        node_ptr new_node = new node_type(std::forward<Args>(args)...);
        if (!insert_node(new_node)) {
            delete new_node;
            return false;
        }
        return true;
    }

    bool erase(const key_type &value) {
        Backoff back_off;
        position pos;
        while (find(value, pos)) {
            if (unlink(pos)) {
                return true;
            }
            back_off();
        }
        return false;
    }

    guarded_ptr extract(const key_type &value) {
        Backoff back_off;
        position pos;
        while (find(value, pos)) {
            if (unlink(pos)) {
                return guarded_ptr(std::move(pos.cur_guard), &pos.cur->value);
            }
            back_off();
        }
        return guarded_ptr();
    }

    void clear() {
        auto key_select = KeySelectHolder::get();
        lu::hazard_pointer head_guard = lu::make_hazard_pointer();
        position pos;
        while (true) {
            auto head = head_guard.protect(head_, [](node_marked_ptr ptr) { return ptr.get(); });
            if (!head) {
                break;
            }
            if (find(key_select(head->value), pos) && pos.cur == head.get()) {
                unlink(pos);
            }
        }
    }

    guarded_ptr find(const key_type &value) const {
        position pos;
        if (find(value, pos)) {
            return guarded_ptr(std::move(pos.cur_guard), &pos.cur->value);
        } else {
            return guarded_ptr();
        }
    }

    guarded_ptr find_no_less(const key_type &value) const {
        position pos;
        find(value, pos);
        if (pos.cur) {
            return guarded_ptr(std::move(pos.cur_guard), &pos.cur->value);
        } else {
            return guarded_ptr();
        }
    }

    bool contains(const key_type &value) const {
        position pos;
        return find(value, pos);
    }

    bool empty() const {
        return !head_.load();
    }

    iterator begin() {
        auto head_guard = lu::make_hazard_pointer();
        auto head = head_guard.protect(head_, [](node_marked_ptr ptr) { return ptr.get(); });
        return iterator(std::move(head_guard), head, this);
    }

    iterator end() {
        return iterator();
    }

    const_iterator cbegin() const {
        auto head_guard = lu::make_hazard_pointer();
        auto head = head_guard.protect(head_, [](node_marked_ptr ptr) { return ptr.get(); });
        return const_iterator(std::move(head_guard), head, this);
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
    std::atomic<node_marked_ptr> head_{};
};

template<class KeyType, class ValueType>
struct MapKeySelect {
    using type = KeyType;

    const KeyType &operator()(const std::pair<KeyType, ValueType> &value) {
        return value.first;
    }
};

template<class KeyType>
struct SetKeySelect {
    using type = KeyType;

    template<class T, class = std::enable_if_t<std::is_same_v<std::decay_t<T>, KeyType>>>
    T &&operator()(T &&value) {
        return std::forward<T>(value);
    }
};

struct OrderedListDefaults {
    using compare = void;
    using backoff = void;
};

}// namespace detail
}// namespace lu

namespace lu {
namespace detail {

template<class ValueType, class... Options>
struct make_ordered_list_set {
    using pack_options = typename GetPackOptions<OrderedListDefaults, Options...>::type;

    using compare = get_or_default<typename pack_options::compare, std::less<const ValueType>>;
    using backoff = get_or_default<typename pack_options::backoff, lu::none_backoff>;
    using key_select = SetKeySelect<ValueType>;

    using type = OrderedList<ValueType, compare, key_select, backoff>;
};

template<class KeyType, class ValueType, class... Options>
struct make_ordered_list_map {
    using pack_options = typename GetPackOptions<OrderedListDefaults, Options...>::type;

    using compare = get_or_default<typename pack_options::compare, std::less<const KeyType>>;
    using backoff = get_or_default<typename pack_options::backoff, lu::none_backoff>;
    using key_select = MapKeySelect<const KeyType, ValueType>;

    using type = OrderedList<std::pair<const KeyType, ValueType>, compare, key_select, backoff>;
};

}// namespace detail

template<class ValueType, class... Options>
using ordered_list_set = typename detail::make_ordered_list_set<ValueType, Options...>::type;

template<class KeyType, class ValueType, class... Options>
using ordered_list_map = typename detail::make_ordered_list_map<KeyType, ValueType, Options...>::type;

}// namespace lu

#endif