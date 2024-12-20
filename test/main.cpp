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
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <ostream>
#include <random>
#include <span>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <vector>


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
            throw std::runtime_error("the values must be equal: " + std::to_string(all_generated[i]) + ", "
                                     + std::to_string(all_extracted[i]));
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
        throw std::runtime_error("the number of reclaimed and retired must be equal: "
                                 + std::to_string(lu::get_default_domain().num_of_reclaimed()) + ", "
                                 + std::to_string(lu::get_default_domain().num_of_retired()));
    }
}

template<class Set>
class SetFixture {
    enum class OperationType : std::uint8_t { insert, erase, find };

    using operations = std::array<OperationType, 100>;
    using operations_view = std::span<OperationType, 100>;

    using set_type = Set;
    using key_type = typename set_type::key_type;

    class Worker {
    public:
        Worker(operations_view operations, std::size_t actions, std::size_t num_of_keys)
            : operations_(operations)
            , num_of_actions_(actions)
            , num_of_keys_(num_of_keys) {}

        void operator()(set_type &set) {
            std::random_device rd;
            std::mt19937 gen(rd());
            std::size_t op_index = gen() % operations_.size();
            for (std::size_t i = 0; i < num_of_actions_; ++i) {
                std::size_t key = gen() % num_of_keys_;
                switch (operations_[op_index]) {
                    case OperationType::insert:
                        if (set.insert(key)) {
                            inserted.push_back(key);
                        }
                        break;
                    case OperationType::erase:
                        if (set.erase(key)) {
                            erased.push_back(key);
                        }
                        break;
                    case OperationType::find:
                        auto found = set.find(key);
                        if (found) {
                            num_of_found++;
                        } else {
                            num_of_not_found++;
                        }
                        break;
                }
                if (++op_index >= operations_.size()) [[unlikely]] {
                    op_index = 0;
                }
            }
        }

    private:
        operations_view operations_;
        std::size_t num_of_actions_;

        std::size_t num_of_keys_;

    public:
        std::vector<key_type> inserted{};
        std::vector<key_type> erased{};
        std::size_t num_of_found{};
        std::size_t num_of_not_found{};

    private:
        char padding_[128];
    };

public:
    struct Config {
        std::size_t insert_percentage = 25;
        std::size_t erase_percentage = 25;

        std::size_t num_of_keys = 10;
    };

public:
    explicit SetFixture(Config config)
        : config_(config) {
        generate_operations(operations_, config_.insert_percentage, config_.erase_percentage);
    }

    void operator()(std::size_t num_of_actions, std::size_t num_of_threads) {
        Set set;

        std::vector<Worker> workers;
        workers.reserve(num_of_threads);
        std::size_t actions_per_thread = num_of_actions / num_of_threads;
        for (std::size_t i = 0; i < num_of_threads; ++i) {
            workers.emplace_back(operations_, actions_per_thread, config_.num_of_keys);
        }

        std::vector<std::thread> threads;
        threads.reserve(num_of_threads);
        for (auto &&worker: workers) {
            threads.emplace_back([&worker, &set]() { worker(set); });
        }

        for (auto &&thread: threads) {
            thread.join();
        }

        std::vector<typename Set::value_type> inserted;
        std::vector<typename Set::value_type> erased;

        std::size_t num_of_found{};
        std::size_t num_of_not_found{};

        for (auto &&worker: workers) {
            for (auto &item: worker.inserted) {
                inserted.emplace_back(item);
            }
            for (auto &item: worker.erased) {
                erased.emplace_back(item);
            }
            num_of_found += worker.num_of_found;
            num_of_not_found += worker.num_of_not_found;
        }

        for (auto it = set.begin(); it != set.end(); ++it) {
            erased.emplace_back(*it);
        }

        std::sort(inserted.begin(), inserted.end());
        std::sort(erased.begin(), erased.end());
        if (inserted.size() != erased.size()) {
            throw std::runtime_error("Error");
        }
        for (std::size_t i = 0; i < inserted.size(); ++i) {
            if (inserted[i] != erased[i]) {
                throw std::runtime_error("Error");
            }
        }
    }

    static void generate_operations(operations &operations, std::size_t insert_percentage,
                                    std::size_t erase_percentage) {
        std::size_t current = 0;
        for (std::size_t i = 0; i < insert_percentage; ++i) {
            operations[current++] = OperationType::insert;
        }
        for (std::size_t i = 0; i < erase_percentage; ++i) {
            operations[current++] = OperationType::erase;
        }
        while (current < operations.size()) {
            operations[current++] = OperationType::find;
        }

        std::random_device rd;
        std::mt19937 gen(rd());
        std::shuffle(operations.begin(), operations.end(), gen);
    }

private:
    operations operations_{};
    Config config_{};
};

int main() {
    for (int i = 0; i < 1000; ++i) {
        std::cout << "iteration: #" << i << std::endl;
        abstractStressTest(SetFixture<lu::ordered_list_set<int>>({}));
    }

    // lu::ordered_list_map<int, int> set;
    // for (int i = 0; i < 10; ++i) {
    //     set.insert({i, i});
    // }

    // for (auto it = set.begin(); it != set.end(); ++it) {
    //     std::cout << it->first << it->second << " ";
    // }

    // std::cout << std::endl << set.contains(5) << std::endl;

    // for (int i = 0; i < 1000; ++i) {
    //     std::cout << "iteration: #" << i << std::endl;
    //     abstractStressTest(stressTest<lu::hp::TreiberStack<int, lu::EmptyBackOff>>);
    // }
}