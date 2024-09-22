#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <forward_list>
#include <fstream>
#include <functional>
#include <future>
#include <iostream>
#include <optional>
#include <ostream>
#include <set>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <hazard_pointer.h>
#include <intrusive/forward_list.h>
#include <intrusive/hashtable.h>
#include <intrusive/unordered_set.h>
#include <marked_ptr.h>
#include <shared_ptr.h>

namespace atomic_shared_ptr {
    template<class ValueType>
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
            auto new_node = lu::make_shared<Node>(value);
            auto head = head_.load();
            do {
                new_node->next = head;
            } while (!head_.compare_exchange_weak(head, new_node));
        }

        std::optional<ValueType> pop() {
            while (true) {
                lu::shared_ptr<Node> head = head_.load();
                if (!head) {
                    return std::nullopt;
                }
                if (head_.compare_exchange_weak(head, head->next)) {
                    return {head->value};
                }
            }
        }

    private:
        lu::atomic_shared_ptr<Node> head_{};
    };

    template<class ValueType>
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
            auto new_item = lu::make_shared<Node>(value);
            lu::shared_ptr<Node> tail;
            while (true) {
                tail = tail_.load();
                if (tail->next.load()) {
                    tail_.compare_exchange_weak(tail, tail->next.load());
                } else {
                    lu::shared_ptr<Node> null;
                    if (tail->next.compare_exchange_weak(null, new_item)) {
                        break;
                    }
                }
            }
            tail_.compare_exchange_weak(tail, new_item);
        }

        std::optional<ValueType> pop() {
            while (true) {
                auto head = head_.load();
                auto head_next = head->next.load();
                if (!head_next) {
                    return std::nullopt;
                }
                if (head_.compare_exchange_weak(head, head_next)) {
                    return {head_next->value};
                }
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
    template<class ValueType>
    class TreiberStack {
        struct Node : public lu::hazard_pointer_obj_base<Node> {
            ValueType value;
            Node *next{nullptr};
        };

    public:
        void push(ValueType value) {
            auto new_node = new Node;
            new_node->value = value;
            auto head = head_.load();
            do {
                new_node->next = head;
            } while (!head_.compare_exchange_strong(head, new_node));
        }

        std::optional<ValueType> pop() {
            auto haz_ptr = lu::make_hazard_pointer();
            while (true) {
                Node *head = haz_ptr.protect(head_);
                if (!head) {
                    return std::nullopt;
                }
                if (head_.compare_exchange_strong(head, head->next)) {
                    std::optional<ValueType> res{head->value};
                    head->retire();
                    return res;
                }
            }
        }

    private:
        std::atomic<Node *> head_{nullptr};
    };

    template<class ValueType, class Compare>
    class OrderedList {
        class Node;

        using pointer = Node *;
        using marked_pointer = lu::marked_ptr<Node>;

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

        struct position {
            pointer cur;
            pointer next;
            std::atomic<marked_pointer> *prev_pointer;

            lu::hazard_pointer cur_guard{lu::make_hazard_pointer()};
            lu::hazard_pointer next_guard{lu::make_hazard_pointer()};
            lu::hazard_pointer prev_guard{lu::make_hazard_pointer()};
        };

    public:
        using guarded_ptr = lu::guarded_ptr<ValueType>;

    private:
        static void delete_node(pointer node) {
            node->retire();
        }

        static bool unlink(position &pos) {
            marked_pointer next(pos.next);
            if (pos.cur->next.compare_exchange_weak(next, marked_pointer(next.get(), 1))) {
                marked_pointer cur(pos.cur);
                if (pos.prev_pointer->compare_exchange_weak(cur, marked_pointer(pos.next))) {
                    delete_node(cur);
                }
                return true;
            }
            return false;
        }

        static bool link(position &pos, pointer new_node) {
            marked_pointer cur(pos.cur);
            new_node->next.store(cur);
            if (pos.prev_pointer->compare_exchange_weak(cur, marked_pointer(new_node))) {
                return true;
            } else {
                new_node->next.store({});
                return false;
            }
        }

        template<class _ValueType, class _Compare>
        static bool find(std::atomic<marked_pointer> *head, const _ValueType &value, position &pos, _Compare &&comp) {
            std::atomic<marked_pointer> *prev_pointer;
            marked_pointer cur{};

        try_again:
            prev_pointer = head;

            cur = pos.cur_guard.protect(*head, [](marked_pointer ptr) {
                return ptr.get();
            });

            while (true) {
                if (!cur) {
                    pos.prev_pointer = prev_pointer;
                    pos.cur = {};
                    pos.next = {};
                    return false;
                }

                marked_pointer next = pos.next_guard.protect(cur->next, [](marked_pointer ptr) {
                    return ptr.get();
                });

                if (prev_pointer->load().all() != cur.get()) {
                    goto try_again;
                }

                if (next.get_bit()) {
                    marked_pointer not_marked_cur(cur.get(), 0);
                    if (prev_pointer->compare_exchange_weak(not_marked_cur, marked_pointer(next.get(), 0))) {
                        delete_node(cur);
                    } else {
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
        ~OrderedList() {
            clear();
        }

        bool insert(const ValueType &value) {
            return emplace(value);
        }

        template<class... Args>
        bool emplace(Args &&...args) {
            pointer new_node = new Node(std::forward<Args>(args)...);

            position pos;
            while (true) {
                if (find(&head_, new_node->value, pos, Compare{})) {
                    return false;
                }
                if (link(pos, new_node)) {
                    return true;
                }
            }
        }

        bool erase(const ValueType &value) {
            position pos;
            while (find(&head_, value, pos, Compare{})) {
                if (unlink(pos)) {
                    return true;
                }
            }
            return false;
        }

        guarded_ptr extract(const ValueType &value) {
            position pos;
            while (find(&head_, value, pos, Compare{})) {
                if (unlink(pos)) {
                    return guarded_ptr(std::move(pos.cur_guard), &pos.cur->value);
                }
            }
            return guarded_ptr();
        }

        void clear() {
            lu::hazard_pointer head_guard = lu::make_hazard_pointer();
            position pos;
            while (true) {
                auto head = head_guard.protect(head_, [](marked_pointer ptr) {
                    return ptr.get();
                });
                if (!head) {
                    break;
                }
                if (find(&head_, head->value, pos, Compare{}) && pos.cur == head.get()) {
                    unlink(pos);
                }
            }
        }

        guarded_ptr find(const ValueType &value) {
            position pos;
            if (find(&head_, value, pos, Compare{})) {
                return guarded_ptr(std::move(pos.cur_guard), &pos.cur->value);
            } else {
                return guarded_ptr();
            }
        }

        bool contains(const ValueType &value) {
            position pos;
            return find(&head_, value, pos, Compare{});
        }

        bool empty() const {
            return !head_.load();
        }

    private:
        std::atomic<marked_pointer> head_{};
    };
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
};

int main() {
    hazard_pointer::OrderedList<int, std::less<int>> list;
    list.emplace(10);
    std::cout << list.contains(10) << std::endl;
    list.erase(10);
    std::cout << list.contains(10) << std::endl;

    for (int i = 0; i < 10; ++i) {
        list.insert(i);
    }
    list.clear();
    std::cout << list.empty();

    for (int i = 0; i < 100; ++i) {
        abstractStressTest(stressTest<atomic_shared_ptr::TreiberStack<int>>);
    }
}