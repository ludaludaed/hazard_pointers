#include "back_off.h"
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
#include <mutex>
#include <optional>
#include <ostream>
#include <queue>
#include <set>
#include <span>
#include <stack>
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
#include <marked_shared_ptr.h>
#include <shared_ptr.h>

#include "ordered_list.h"
#include "thread_local_list.h"

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
    std::size_t num_of_threads = std::thread::hardware_concurrency();
    for (int i = 1; i <= num_of_threads; i++) {
        std::cout << "\t" << i;
    }
    std::cout << std::endl;
    for (int i = 500000; i <= 6000000; i += 500000) {
        std::cout << i << "\t";
        for (int j = 1; j <= num_of_threads; j++) {
            std::chrono::steady_clock::time_point begin = std::chrono::steady_clock::now();
            func(i, j);
            std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();
            std::cout << std::chrono::duration_cast<std::chrono::milliseconds>(end - begin).count() << "\t";
        }
        std::cout << std::endl;
    }
}

struct H : public lu::thread_local_list_base_hook<> {
    int y = 10;
};

struct Detacher {
    void operator()(H *ptr) const {
        std::cout << "detach " << ptr->y << std::endl;
    }
};

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

    // std::cout << sizeof(lu::unordered_set_base_hook<lu::store_hash<false>>) << std::endl;
    // std::cout << sizeof(lu::hazard_pointer_obj_base<int>) << std::endl;

    // for (int i = 0; i < 1; ++i) {
    //     abstractStressTest(stressTest<hazard_pointer::TreiberStack<int, lu::YieldBackOff>>);
    // }

    lu::thread_local_list<H, lu::detacher<Detacher>> list;

    auto it = list.get_thread_local();
    it->y = 1;

    std::thread thr{[&]() {
        auto it = list.get_thread_local();
        it->y = 2;
    }};

    thr.join();
    for (auto it = list.begin(); it != list.end(); ++it) {
        std::cout << list.is_acquired(it) << " " << it->y << std::endl;
    }

    list.detach_thread();
}