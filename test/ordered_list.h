#ifndef __ORDERED_LIST_H__
#define __ORDERED_LIST_H__

#include <back_off.h>
#include <hazard_pointer.h>
#include <marked_ptr.h>

#include <type_traits>


namespace lu {
    template<class ValueType, class KeyCompare, class KeySelect, class BackOff>
    class OrderedList : private lu::detail::EmptyBaseHolder<KeyCompare>,
                        private lu::detail::EmptyBaseHolder<KeySelect> {

        using KeyCompareHolder = lu::detail::EmptyBaseHolder<KeyCompare>;
        using KeySelectHolder = lu::detail::EmptyBaseHolder<KeySelect>;

        struct Node : public lu::hazard_pointer_obj_base<Node> {
        public:
            template<class... Args>
            explicit Node(Args &&...args)
                : value(std::forward<Args>(args)...) {}

        public:
            ValueType value;
            std::atomic<lu::marked_ptr<Node>> next{};
        };

        using node_pointer = Node *;
        using node_marked_pointer = lu::marked_ptr<Node>;

        struct position {
            node_pointer cur;
            node_pointer next;
            std::atomic<node_marked_pointer> *prev_pointer;

            lu::hazard_pointer cur_guard{lu::make_hazard_pointer()};
            lu::hazard_pointer next_guard{lu::make_hazard_pointer()};
            lu::hazard_pointer prev_guard{lu::make_hazard_pointer()};
        };

        class Iterator {
        friend class OrderedList;

        public:
            using iterator_category = std::forward_iterator_tag;
            using value_type = ValueType;
            using difference_type = std::ptrdiff_t;
            using pointer = const ValueType*;
            using reference = const ValueType&;

        private:
            Iterator(lu::hazard_pointer guard, node_marked_pointer current, OrderedList* list) noexcept
                : guard_(std::move(guard))
                , current_(current)
                , list_(list) {}

        public:
            Iterator(const Iterator& other) noexcept
                : guard_(lu::make_hazard_pointer())
                , current_(other.current_)
                , list_(other.list_) {
                guard_.reset_protection(current_);
            }

            Iterator(Iterator&& other) noexcept {
                std::swap(guard_, other.guard_);
                std::swap(current_, other.current_);
                std::swap(list_, other.list_);
            }

            Iterator& operator=(const Iterator& other) noexcept {
                guard_.reset_protection(other.current_);
                current_ = other.current_;
                list_ = other.list_;
            }

            Iterator& operator=(Iterator&& other) noexcept {
                std::swap(guard_, other.guard_);
                std::swap(current_, other.current_);
                std::swap(list_, other.list_);
            }

            Iterator& operator++() noexcept {
                increment();
                return *this;
            }

            Iterator operator++(int) noexcept {
                Iterator copy(*this);
                increment();
                return copy;
            }

            reference operator*() const noexcept {
                return *this->operator->();
            }

            pointer operator->() const noexcept {
                return &current_->value;
            }

        private:
            void increment() noexcept {
                auto next_guard = lu::make_hazard_pointer();
                auto next = next_guard.protect(current_->next, [](node_marked_pointer ptr) { return ptr.get(); });
                if (next.get_bit()) {
                    position new_pos;
                    list_->find(current_->value, new_pos);
                    guard_ = std::move(new_pos.cur_guard);
                    current_ = new_pos.cur;
                } else {
                    guard_ = std::move(next_guard);
                    current_ = next;
                }
            }

        private:
            lu::hazard_pointer guard_{};
            node_marked_pointer current_{};
            OrderedList* list_{};
        };

    public:
        using value_type = ValueType;
        using key_type = typename KeySelect::type;

        using compare = KeyCompare;
        using key_select = KeySelect;

        constexpr static bool is_key_value = !std::is_same_v<value_type, key_type>;

        using guarded_ptr
                = std::conditional_t<is_key_value, lu::guarded_ptr<ValueType>, lu::guarded_ptr<const ValueType>>;

    private:
        static bool unlink(position &pos) {
            node_marked_pointer next(pos.next);
            if (pos.cur->next.compare_exchange_weak(next, node_marked_pointer(next.get(), 1))) {
                node_marked_pointer cur(pos.cur);
                if (pos.prev_pointer->compare_exchange_weak(cur, node_marked_pointer(pos.next))) {
                    cur->retire();
                }
                return true;
            }
            return false;
        }

        static bool link(position &pos, node_pointer new_node) {
            node_marked_pointer cur(pos.cur);
            new_node->next.store(cur);
            if (pos.prev_pointer->compare_exchange_weak(cur, node_marked_pointer(new_node))) {
                return true;
            } else {
                new_node->next.store({});
                return false;
            }
        }

        template<class Compare>
        static bool find(std::atomic<node_marked_pointer> *head, const value_type &value, position &pos,
                         Compare &&comp) {
            std::atomic<node_marked_pointer> *prev_pointer;
            node_marked_pointer cur{};

            BackOff back_off;

        try_again:
            prev_pointer = head;

            cur = pos.cur_guard.protect(*head, [](node_marked_pointer ptr) { return ptr.get(); });

            while (true) {
                if (!cur) {
                    pos.prev_pointer = prev_pointer;
                    pos.cur = {};
                    pos.next = {};
                    return false;
                }

                node_marked_pointer next
                        = pos.next_guard.protect(cur->next, [](node_marked_pointer ptr) { return ptr.get(); });

                if (prev_pointer->load().all() != cur.get()) {
                    back_off();
                    goto try_again;
                }

                if (next.get_bit()) {
                    node_marked_pointer not_marked_cur(cur.get(), 0);
                    if (prev_pointer->compare_exchange_weak(not_marked_cur, node_marked_pointer(next.get(), 0))) {
                        delete_node(cur);
                    } else {
                        back_off();
                        goto try_again;
                    }
                } else {
                    if (!comp(cur->value, value)) {
                        pos.prev_pointer = prev_pointer;
                        pos.cur = cur;
                        pos.next = next;
                        return !comp(value, cur->value);
                    }
                    prev_pointer = &(cur->next);
                    pos.prev_guard.reset_protection(cur.get());
                }
                pos.cur_guard.reset_protection(next.get());
                cur = next;
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
        bool find(const value_type &value, position &pos) {
            auto comp = KeyCompareHolder::get();
            auto key_select = KeySelectHolder::get();

            auto compare = [&comp, &key_select](const value_type &left, const value_type &right) {
                return comp(key_select(left), key_select(right));
            };

            return find(&head_, value, pos, compare);
        }

        bool insert_node(node_pointer new_node) {
            BackOff back_off;
            position pos;
            while (true) {
                if (find(new_node->value, pos)) {
                    delete new_node;
                    return false;
                }
                if (link(pos, new_node)) {
                    return true;
                }
                back_off();
            }
        }

    public:
        template<class _ValueType, class = std::enable_if_t<!is_key_value>>
        bool insert(_ValueType &&value) {
            return emplace(std::forward<_ValueType>(value));
        }

        template<class _ValueType, class = std::enable_if_t<is_key_value>>
        bool insert(const key_type &key, _ValueType &&value) {
            node_pointer new_node = new Node(key, std::forward<_ValueType>(value));
            if (!insert_node(new_node)) {
                delete new_node;
                return false;
            }
            return true;
        }

        template<class... Args>
        bool emplace(Args &&...args) {
            node_pointer new_node = new Node(std::forward<Args>(args)...);
            if (!insert_node(new_node)) {
                delete new_node;
                return false;
            }
            return true;
        }

        bool erase(const key_type &value) {
            BackOff back_off;
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
            BackOff back_off;
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
            lu::hazard_pointer head_guard = lu::make_hazard_pointer();
            position pos;
            while (true) {
                auto head = head_guard.protect(head_, [](node_marked_pointer ptr) { return ptr.get(); });
                if (!head) {
                    break;
                }
                if (find(head->value, pos) && pos.cur == head.get()) {
                    unlink(pos);
                }
            }
        }

        guarded_ptr find(const key_type &value) {
            position pos;
            if (find(value, pos)) {
                return guarded_ptr(std::move(pos.cur_guard), &pos.cur->value);
            } else {
                return guarded_ptr();
            }
        }

        bool contains(const key_type &value) {
            position pos;
            return find(value, pos);
        }

        bool empty() const {
            return !head_.load();
        }

    private:
        std::atomic<node_marked_pointer> head_{};
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

    template<class ValueType, class KeyCompare = std::less<ValueType>, class BackOff = YieldBackOff>
    using ordered_list_set = OrderedList<ValueType, KeyCompare, SetKeySelect<ValueType>, BackOff>;

    template<class KeyType, class ValueType, class KeyCompare = std::less<ValueType>, class BackOff = YieldBackOff>
    using ordered_list_map
            = OrderedList<std::pair<const KeyType, ValueType>, KeyCompare, MapKeySelect<KeyType, ValueType>, BackOff>;
}// namespace lu

#endif