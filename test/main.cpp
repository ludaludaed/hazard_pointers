#include <hazard_pointer.h>
#include <intrusive/forward_list.h>
#include <intrusive/hashtable.h>
#include <intrusive/unordered_set.h>
#include <marked_shared_ptr.h>
#include <shared_ptr.h>

#include "back_off.h"
#include "ordered_list.h"
#include "structures.h"

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <ostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>


struct A : lu::forward_list_base_hook<> {
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
        if (all_generated[i] != all_extracted[i]) {
            throw std::runtime_error("the values must be equal: " + std::to_string(all_generated[i]) + ", " + std::to_string(all_extracted[i]));
        }
    }
}

template<class Func>
void abstractStressTest(Func &&func) {
    std::size_t num_of_threads = std::thread::hardware_concurrency();
    for (int i = 1; i <= num_of_threads; i++) {
        std::cout << "\t" << i;
    }
    // num_of_threads = 1;
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
        lu::detach_thread();
    }
    if (lu::get_default_domain().num_of_reclaimed() != lu::get_default_domain().num_of_retired()) {
        throw std::runtime_error("the number of reclaimed and retired must be equal: " + std::to_string(lu::get_default_domain().num_of_reclaimed()) + ", " + std::to_string(lu::get_default_domain().num_of_retired()));
    }
}

int main() {
    for (int i = 0; i < 1000; ++i) {
        std::cout << "iteration: #" << i << std::endl;
        abstractStressTest(stressTest<lu::hp::MSQueue<int, lu::EmptyBackOff>>);
    }
}