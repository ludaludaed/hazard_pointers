#include <intrusive/forward_list.h>
#include <intrusive/hashtable.h>
#include <intrusive/options.h>
#include <intrusive/unordered_set.h>
#include <reclamation/hazard_pointer.h>
#include <reclamation/marked_shared_ptr.h>
#include <reclamation/shared_ptr.h>
#include <utils/back_off.h>

#include "ordered_list.h"
#include "structures.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <ostream>
#include <random>
#include <span>
#include <stdexcept>
#include <string>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>


template<class Container>
void stressTest(int actions, int threads) {
    std::vector<std::thread> workers;
    workers.reserve(threads);
    std::vector<std::vector<int>> generated(threads);
    std::vector<std::vector<int>> extracted(threads);
    Container container;
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
        template<class KeyGen>
        Worker(operations_view operations, std::size_t actions, KeyGen &&key_gen)
            : operations_(operations)
            , num_of_actions_(actions)
            , key_gen_(std::forward<KeyGen>(key_gen)) {}

        void operator()(set_type &set) {
            std::size_t op_index = std::rand() % operations_.size();
            for (std::size_t i = 0; i < num_of_actions_; ++i) {
                key_type key = key_gen_();
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
        std::function<key_type()> key_gen_;

    public:
        std::vector<key_type> inserted{};
        std::vector<key_type> erased{};
        std::size_t num_of_found{};
        std::size_t num_of_not_found{};

    private:
        char padding_[512];
    };

public:
    struct Config {
        std::size_t insert_percentage = 50;
        std::size_t erase_percentage = 50;
        std::size_t num_of_keys = 100;
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
            std::random_device rd;
            auto key_gen = [num_of_keys = config_.num_of_keys, gen = std::mt19937(rd())]() mutable {
                return (int) gen() % num_of_keys;
            };
            workers.emplace_back(operations_, actions_per_thread, key_gen);
        }

        std::vector<std::thread> threads;
        threads.reserve(num_of_threads);
        for (auto &&worker: workers) {
            threads.emplace_back([&worker, &set]() { worker(set); });
        }

        for (auto &&thread: threads) {
            thread.join();
        }

        std::vector<key_type> inserted;
        std::vector<key_type> erased;

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
        /*
          We perform a find operation because we need to remove marked elements,
          if present (a marked element is one we have flagged for deletion).
          In a regular iteration, this might not be crucial, but for this particular test,
          we must ensure that all elements marked for deletion are indeed removed from the list.
        */
        set.find(config_.num_of_keys + 1);

        for (auto it = set.begin(); it != set.end(); ++it) {
            erased.emplace_back(*it);
        }

        std::sort(inserted.begin(), inserted.end());
        std::sort(erased.begin(), erased.end());
        if (inserted.size() != erased.size()) {
            throw std::runtime_error("Error non equals sizes " + std::to_string(inserted.size()) + " "
                                     + std::to_string(erased.size()));
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

#include <intrusive/compressed_tuple.h>

struct T : lu::unordered_set_base_hook<lu::is_auto_unlink<false>> {
    friend std::size_t hash_value(const T &t) {
        return t.i;
    }

    friend bool operator==(const T &l, const T &r) {
        return l.i == r.i;
    }

    std::size_t i{};
};

struct U : lu::forward_list_base_hook<lu::is_auto_unlink<false>> {
    std::size_t i;
};

template<std::size_t I>
struct Empty {};

int main() {
    std::vector<T> values(10);
    std::vector<typename lu::unordered_set<T>::bucket_type> b(10);
    lu::unordered_set<T> set(typename lu::unordered_set<T>::bucket_traits(b.data(), b.size()));

    std::cout << set.size() << std::endl;
    for (int i = 0; i < values.size(); ++i) {
        values[i].i = i;
        set.insert(values[i]);
        std::cout << i << " " << set.size() << std::endl;
    }
    std::cout << std::endl;

    std::vector<U> list_values(10);
    lu::forward_list<U> list;
    std::cout << list.size() << std::endl;
    for (int i = 0; i < list_values.size(); ++i) {
        list_values[i].i = i;
        list.push_front(list_values[i]);
        std::cout << i << " " << list.size() << std::endl;
    }
    std::cout << std::endl;

    lu::compressed_tuple<Empty<0>, Empty<1>, char, int, Empty<2>, long long, Empty<3>, Empty<4>, Empty<5>, Empty<6>,
                         Empty<7>, Empty<8>, Empty<9>, Empty<10>, Empty<11>, std::array<long long, 2>>
            ct;
    std::tuple<Empty<0>, Empty<1>, char, int, Empty<2>, long long, Empty<3>, Empty<4>, Empty<5>, Empty<6>, Empty<7>,
               Empty<8>, Empty<9>, Empty<10>, Empty<11>, std::array<long long, 2>>
            t;

    struct res {
        std::array<long long, 2> a;
        long long b;
        int c;
        char d;
    };

    std::cout << sizeof(ct) << std::endl << sizeof(t) << std::endl << sizeof(res) << std::endl;

    auto &&e0 = lu::get<0>(ct);
    auto &&e1 = lu::get<1>(std::move(ct));
    auto &&e2 = lu::get<2>((const decltype(ct) &) ct);
    auto &&e3 = lu::get<3>(std::move((const decltype(ct) &) ct));

    auto &&e_int = lu::get<int>(ct);
    auto &&e_int_rvalue = lu::get<int>(std::move(ct));
    auto &&e_int_const = lu::get<int>((const decltype(ct) &) ct);
    auto &&e_int_rvalue_const = lu::get<int>(std::move((const decltype(ct) &) ct));

    lu::compressed_tuple<int, char, double> tp(0, 'a', 0.001);

    std::cout << lu::get<int>(tp) << " " << lu::get<0>(tp) << std::endl;
    std::cout << lu::get<char>(tp) << " " << lu::get<1>(tp) << std::endl;
    std::cout << lu::get<double>(tp) << " " << lu::get<2>(tp) << std::endl;

    for (int i = 0; i < 1000; ++i) {
        std::cout << "iteration: #" << i << std::endl;
        abstractStressTest(SetFixture<lu::ordered_list_set<int, lu::backoff<lu::none_backoff>>>({}));
    }
}
