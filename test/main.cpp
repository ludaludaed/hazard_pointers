#include "intrusive/empty_base_holder.h"
#include "intrusive/options.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <forward_list>
#include <fstream>
#include <functional>
#include <future>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <ostream>
#include <set>
#include <string_view>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <hazard_pointer.h>
#include <intrusive/forward_list.h>
#include <intrusive/hashtable.h>
#include <intrusive/unordered_set.h>
#include <marked_ptr.h>
#include <shared_ptr.h>

struct A : lu::forward_list_hook<> {
    int a;
};

void slist_test() {
    auto comp = [](const A &left, const A &right) {
        return left.a < right.a;
    };

    std::vector<A>
            vec(20);
    for (int i = 0; i < vec.size(); ++i) {
        vec[i].a = rand() % 100;
    }

    std::reverse(vec.begin(), vec.end());

    lu::forward_list<A> list_a;
    lu::forward_list<A> list_b;

    for (int i = 0; i < 10; ++i) {
        list_a.push_front(vec[i]);
    }

    for (int i = 10; i < 20; ++i) {
        list_b.push_front(vec[i]);
    }

    for (auto it = list_a.begin(); it != list_a.end(); ++it) {
        std::cout << it->a << " ";
    }

    std::cout << std::endl;

    for (auto it = list_b.begin(); it != list_b.end(); ++it) {
        std::cout << it->a << " ";
    }

    std::cout << std::endl;

    list_a.sort(comp);
    list_b.sort(comp);

    std::swap(list_a, list_b);

    for (auto it = list_a.begin(); it != list_a.end(); ++it) {
        std::cout << it->a << " ";
    }

    std::cout << std::endl;

    for (auto it = list_b.begin(); it != list_b.end(); ++it) {
        std::cout << it->a << " ";
    }

    std::cout << std::endl;

    list_a.merge(list_b, comp);

    for (auto it = list_a.begin(); it != list_a.end(); ++it) {
        std::cout << it->a << " ";
    }

    std::cout << std::endl;

    for (auto it = list_b.begin(); it != list_b.end(); ++it) {
        std::cout << it->a << " ";
    }

    std::cout << std::endl;

    std::cout << list_a.empty() << " " << list_b.empty() << std::endl;
    std::cout << list_a.size() << " " << list_b.size() << std::endl;
}

struct EmptyBackOff {
    void operator()() const {}
};

struct YieldBackOff {
    void operator()() const {
        std::this_thread::yield();
    }
};

namespace atomic_shared_ptr {
    template<class ValueType, class BackOff>
    class TreiberStack {
        struct Node {
            ValueType value{};
            lu::shared_ptr<Node> next{};

            template<class... Args>
            Node(Args &&...args)
                : value(std::forward<Args>(args)...) {
            }
        };

    public:
        void push(ValueType value) {
            BackOff back_off;
            auto new_node = lu::make_shared<Node>(value);
            auto head = head_.load();
            new_node->next = head;
            while (true) {
                if (head_.compare_exchange_weak(new_node->next, new_node)) {
                    return;
                }
                back_off();
            }
        }

        std::optional<ValueType> pop() {
            BackOff back_off;
            while (true) {
                lu::shared_ptr<Node> head = head_.load();
                if (!head) {
                    return std::nullopt;
                }
                if (head_.compare_exchange_weak(head, head->next)) {
                    return {head->value};
                }
                back_off();
            }
        }

    private:
        lu::atomic_shared_ptr<Node> head_{};
    };

    template<class ValueType, class BackOff>
    class MSQueue {
        struct Node {
            ValueType value{};
            lu::atomic_shared_ptr<Node> next{};

            Node() = default;

            template<class... Args>
            Node(Args &&...args)
                : value(std::forward<Args>(args)...) {
            }
        };

    public:
        MSQueue() {
            auto dummy = lu::make_shared<Node>();
            head_.store(dummy);
            tail_.store(dummy);
        }

        void push(ValueType value) {
            BackOff back_off;
            auto new_item = lu::make_shared<Node>(value);
            while (true) {
                auto tail = tail_.load();
                if (tail->next.load()) {
                    tail_.compare_exchange_weak(tail, tail->next.load());
                } else {
                    lu::shared_ptr<Node> null;
                    if (tail->next.compare_exchange_weak(null, new_item)) {
                        tail_.compare_exchange_weak(tail, new_item);
                        return;
                    }
                }
                back_off();
            }
        }

        std::optional<ValueType> pop() {
            BackOff back_off;
            while (true) {
                auto head = head_.load();
                auto head_next = head->next.load();
                if (!head_next) {
                    return std::nullopt;
                }
                if (head_.compare_exchange_weak(head, head_next)) {
                    return {head_next->value};
                }
                back_off();
            }
        }

    private:
        lu::atomic_shared_ptr<Node> head_;
        lu::atomic_shared_ptr<Node> tail_;
    };

    template<class ValueType>
    class OrderedList {
        struct Node {
            ValueType value;
            lu::atomic_marked_shared_ptr<Node> next{};
        };

        struct position {
            lu::shared_ptr<Node> cur{};
            lu::shared_ptr<Node> next{};

            lu::shared_ptr<Node> prev{};// guard for prev node
            lu::atomic_marked_shared_ptr<Node> *prev_pointer;
        };

    private:
        static bool unlink(position &pos) {
            lu::shared_ptr<Node> next(pos.next);
            if (pos.cur->next.compare_exchange_weak(next, lu::marked_shared_ptr<Node>(next, 1))) {
                lu::shared_ptr<Node> cur(pos.cur);
                pos.prev->compare_exchange_weak(cur, next);
                return true;
            }
            return false;
        }

        static bool link(position &pos, lu::shared_ptr<Node> new_node) {
            lu::shared_ptr<Node> cur = pos.cur;
            new_node->next.store(cur);
            if (pos.prev_pointer->compare_exchange_weak(cur, new_node)) {
                return true;
            } else {
                new_node->next.store({});
                return false;
            }
        }

        template<class _ValueType, class Compare>
        static bool find(lu::atomic_marked_shared_ptr<Node> *head, const _ValueType &value, position &pos, Compare &&comp) {
            lu::atomic_marked_shared_ptr<Node> *prev_pointer;
            lu::shared_ptr<Node> prev;
            lu::shared_ptr<Node> cur;

        try_again:
            prev_pointer = head;

            cur = prev_pointer->load();
            while (true) {
                if (!cur) {
                    pos.cur = {};
                    pos.next = {};
                    pos.prev = {};
                    pos.prev_pointer = prev_pointer;
                    return false;
                }

                lu::marked_shared_ptr<Node> next = cur->next.load();
                if (prev_pointer->load() != cur) {
                    goto try_again;
                }

                if (next.get_bit()) {
                    next.clear_bit();
                    if (!prev_pointer->compare_exchange_weak(cur, next)) {
                        goto try_again;
                    }
                } else {
                    if (!comp(cur->value, value)) {
                        pos.cur = std::move(cur);
                        pos.next = std::move(next);
                        pos.prev = std::move(prev);
                        pos.prev_pointer = prev_pointer;
                        return !comp(value, cur->value);
                    }
                    prev_pointer = &(cur->next);
                    prev = std::move(cur);
                }
                cur = std::move(next);
            }
        }

    private:
        lu::atomic_marked_shared_ptr<Node> head_;
    };
}// namespace atomic_shared_ptr

namespace hazard_pointer {
    template<class ValueType, class BackOff>
    class TreiberStack {
        struct Node : public lu::hazard_pointer_obj_base<Node> {
            template<class... Args>
            Node(Args &&...args)
                : value(std::forward<Args>(args)...) {}

            ValueType value;
            Node *next{};
        };

    public:
        void push(ValueType value) {
            BackOff back_off;
            auto new_node = new Node(value);
            auto head = head_.load();
            new_node->next = head;
            while (true) {
                if (head_.compare_exchange_weak(new_node->next, new_node)) {
                    return;
                }
                back_off();
            }
        }

        std::optional<ValueType> pop() {
            BackOff back_off;
            auto head_guard = lu::make_hazard_pointer();
            while (true) {
                auto head = head_guard.protect(head_);
                if (!head) {
                    return std::nullopt;
                }
                if (head_.compare_exchange_strong(head, head->next)) {
                    head->retire();
                    return {std::move(head->value)};
                }
                back_off();
            }
        }

    private:
        std::atomic<Node *> head_{nullptr};
    };

    template<class ValueType, class BackOff>
    class MSQueue {
        struct Node : lu::hazard_pointer_obj_base<Node> {
            ValueType value{};
            std::atomic<Node *> next{};

            Node() = default;

            template<class... Args>
            Node(Args &&...args)
                : value(std::forward<Args>(args)...) {
            }
        };

    public:
        MSQueue() {
            auto dummy_node = new Node();
            head_.store(dummy_node);
            tail_.store(dummy_node);
        }

        template<class... Args>
        void push(Args &&...args) {
            BackOff back_off;

            auto new_node = new Node(std::forward<Args>(args)...);
            auto tail_guard = lu::make_hazard_pointer();

            while (true) {
                auto tail = tail_guard.protect(tail_);
                auto tail_next = tail->next.load();
                if (tail_next) {
                    tail_.compare_exchange_weak(tail, tail_next);
                } else {
                    if (tail->next.compare_exchange_weak(tail_next, new_node)) {
                        tail_.compare_exchange_weak(tail, new_node);
                        return;
                    }
                }
                back_off();
            }
        }

        std::optional<ValueType> pop() {
            BackOff back_off;
            auto head_guard = lu::make_hazard_pointer();
            auto head_next_guard = lu::make_hazard_pointer();
            while (true) {
                auto head = head_guard.protect(head_);
                auto head_next = head_next_guard.protect(head->next);
                if (!head_next) {
                    return std::nullopt;
                }
                if (head_.compare_exchange_weak(head, head_next)) {
                    head->retire();
                    return {std::move(head_next->value)};
                }
                back_off();
            }
        }

    private:
        std::atomic<Node *> head_;
        std::atomic<Node *> tail_;
    };

    template<class ValueType, class KeyCompare, class KeySelect, class BackOff>
    class OrderedList : private lu::detail::EmptyBaseHolder<KeyCompare>,
                        private lu::detail::EmptyBaseHolder<KeySelect> {

        using KeyCompareHolder = lu::detail::EmptyBaseHolder<KeyCompare>;
        using KeySelectHolder = lu::detail::EmptyBaseHolder<KeySelect>;

        struct Node : public lu::hazard_pointer_obj_base<Node> {
        public:
            template<class... Args>
            explicit Node(Args &&...args)
                : value(std::forward<Args>(args)...) {
            }

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

    public:
        using value_type = ValueType;
        using key_type = KeySelect::type;

        using compare = KeyCompare;
        using key_select = KeySelect;

        using guarded_ptr = std::conditional_t<!std::is_same_v<value_type, key_type>, lu::guarded_ptr<ValueType>, lu::guarded_ptr<const ValueType>>;

    private:
        static void delete_node(node_pointer node) {
            node->retire();
        }

        static bool unlink(position &pos) {
            node_marked_pointer next(pos.next);
            if (pos.cur->next.compare_exchange_weak(next, node_marked_pointer(next.get(), 1))) {
                node_marked_pointer cur(pos.cur);
                if (pos.prev_pointer->compare_exchange_weak(cur, node_marked_pointer(pos.next))) {
                    delete_node(cur);
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
        static bool find(std::atomic<node_marked_pointer> *head, const value_type &value, position &pos, Compare &&comp) {
            std::atomic<node_marked_pointer> *prev_pointer;
            node_marked_pointer cur{};

            BackOff back_off;

        try_again:
            prev_pointer = head;

            cur = pos.cur_guard.protect(*head, [](node_marked_pointer ptr) {
                return ptr.get();
            });

            while (true) {
                if (!cur) {
                    pos.prev_pointer = prev_pointer;
                    pos.cur = {};
                    pos.next = {};
                    return false;
                }

                node_marked_pointer next = pos.next_guard.protect(cur->next, [](node_marked_pointer ptr) {
                    return ptr.get();
                });

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
            : KeyCompareHolder(compare),
              KeySelectHolder(key_select) {}

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

    public:
        bool insert(const ValueType &value) {
            return emplace(value);
        }

        template<class... Args>
        bool emplace(Args &&...args) {
            BackOff back_off;
            node_pointer new_node = new Node(std::forward<Args>(args)...);

            position pos;
            while (true) {
                if (find(new_node->value, pos)) {
                    return false;
                }
                if (link(pos, new_node)) {
                    return true;
                }
                back_off();
            }
        }

        bool erase(const ValueType &value) {
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

        guarded_ptr extract(const ValueType &value) {
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
                auto head = head_guard.protect(head_, [](node_marked_pointer ptr) {
                    return ptr.get();
                });
                if (!head) {
                    break;
                }
                if (find(head->value, pos) && pos.cur == head.get()) {
                    unlink(pos);
                }
            }
        }

        guarded_ptr find(const ValueType &value) {
            position pos;
            if (find(value, pos)) {
                return guarded_ptr(std::move(pos.cur_guard), &pos.cur->value);
            } else {
                return guarded_ptr();
            }
        }

        bool contains(const ValueType &value) {
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
    using ordered_list = OrderedList<ValueType, KeyCompare, SetKeySelect<ValueType>, BackOff>;

    template<class KeyType, class ValueType, class KeyCompare = std::less<ValueType>, class BackOff = YieldBackOff>
    using ordered_key_value_list = OrderedList<std::pair<const KeyType, ValueType>, KeyCompare, MapKeySelect<KeyType, ValueType>, BackOff>;
}// namespace hazard_pointer

template<typename TContainer>
void stressTest(int actions, int threads) {
    std::vector<std::thread> workers;
    workers.reserve(threads);
    std::vector<std::vector<int>> generated(threads);
    std::vector<std::vector<int>> extracted(threads);
    TContainer container;
    for (int i = 0; i < threads; i++) {
        workers.emplace_back([i, actions, &container, &generated, &extracted, threads]() {
            for (int j = 0; j < actions / threads; j++) {
                if (rand() % 2) {
                    int a = rand();
                    container.push(a);
                    generated[i].push_back(a);
                } else {
                    auto a = container.pop();
                    if (a) {
                        extracted[i].push_back(*a);
                    }
                }
            }
        });
    }

    for (auto &thread: workers) {
        thread.join();
    }

    std::vector<int> all_generated;
    std::vector<int> all_extracted;
    for (int i = 0; i < generated.size(); i++) {
        for (int j = 0; j < generated[i].size(); j++) {
            all_generated.push_back(generated[i][j]);
        }
    }

    for (int i = 0; i < extracted.size(); i++) {
        for (int j = 0; j < extracted[i].size(); j++) {
            all_extracted.push_back(extracted[i][j]);
        }
    }

    while (true) {
        auto a = container.pop();
        if (a) {
            all_extracted.push_back(*a);
        } else {
            break;
        }
    }

    assert(all_generated.size() == all_extracted.size());
    std::sort(all_generated.begin(), all_generated.end());
    std::sort(all_extracted.begin(), all_extracted.end());
    for (int i = 0; i < all_extracted.size(); i++) {
        assert(all_generated[i] == all_extracted[i]);
    }
}

template<class Func>
void abstractStressTest(Func &&func) {
    for (int i = 1; i <= std::thread::hardware_concurrency(); i++) {
        std::cout << "\t" << i;
    }
    std::cout << std::endl;
    for (int i = 500000; i <= 6000000; i += 500000) {
        std::cout << i << "\t";
        for (int j = 1; j <= std::thread::hardware_concurrency(); j++) {
            std::chrono::steady_clock::time_point begin = std::chrono::steady_clock::now();
            func(i, j);
            std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();
            std::cout << std::chrono::duration_cast<std::chrono::milliseconds>(end - begin).count() << "\t";
        }
        std::cout << std::endl;
    }
}

template<class Func>
void abstractStressTest(Func &&func, std::ostream &out) {
    int actions = 4000000;
    for (int j = 1; j <= std::thread::hardware_concurrency() * 2; j++) {
        std::chrono::steady_clock::time_point begin = std::chrono::steady_clock::now();
        func(actions, j);
        std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();
        out << std::chrono::duration_cast<std::chrono::milliseconds>(end - begin).count() << ",";
    }
    out << std::endl;
}

int main() {
    // hazard_pointer::ordered_list<int> list;
    // list.emplace(10);
    // std::cout << list.contains(10) << std::endl;
    // list.erase(10);
    // std::cout << list.contains(10) << std::endl;

    // for (int i = 0; i < 10; ++i) {
    //     list.insert(i);
    // }
    // list.clear();
    // std::cout << list.empty();
    for (int i = 0; i < 1; ++i) {
        abstractStressTest(stressTest<hazard_pointer::TreiberStack<int, YieldBackOff>>);
    }
}