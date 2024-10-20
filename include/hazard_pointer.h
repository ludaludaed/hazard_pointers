#ifndef __HAZARD_POINTERS_H__
#define __HAZARD_POINTERS_H__

#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <cstddef>
#include <memory>
#include <type_traits>
#include <utility>

#include "intrusive/forward_list.h"
#include "intrusive/options.h"
#include "intrusive/unordered_set.h"


namespace lu {
    class HazardPointerTag {};

    using HazardPointerHook = lu::unordered_set_base_hook<lu::tag<HazardPointerTag>, lu::store_hash<false>>;

    class HazardObject : public HazardPointerHook {
        friend class HazardPointerDomain;

        template<class, class>
        friend class HazardPointerObjBase;

        using ReclaimFunc = void(HazardObject *value);
        using ReclaimFuncPtr = void (*)(HazardObject *value);

    private:
        void reclaim() {
            reclaim_func_(this);
        }

        void set_reclaimer(ReclaimFuncPtr reclaim_func) noexcept {
            reclaim_func_ = reclaim_func;
        }

        bool is_protected() const noexcept {
            return protected_;
        }

        void make_protected() noexcept {
            protected_ = true;
        }

        void make_unprotected() noexcept {
            protected_ = false;
        }

    private:
        ReclaimFuncPtr reclaim_func_{};
        bool protected_{false};
    };

    template<class ValueType>
    struct RawPointerKeyOfValue {
        using type = const ValueType *;

        const ValueType *operator()(const ValueType &value) const noexcept {
            return &value;
        }
    };

    class RetiredSet {
        static constexpr std::size_t num_of_buckets = 64;

        using SetOfRetired = lu::unordered_set<HazardObject, lu::base_hook<HazardPointerHook>, lu::key_of_value<RawPointerKeyOfValue<HazardObject>>>;

        using BucketsData = std::array<typename SetOfRetired::bucket_type, num_of_buckets>;
        using BucketTraits = typename SetOfRetired::bucket_traits;

    public:
        using value_type = HazardObject;
        using key_type = const HazardObject *;

        using pointer = value_type *;
        using const_pointer = const value_type *;
        using reference = value_type &;
        using const_reference = const value_type &;

        using iterator = typename SetOfRetired::iterator;
        using const_iterator = typename SetOfRetired::const_iterator;

    public:
        RetiredSet() noexcept
            : retired_set_(BucketTraits(buckets_.data(), buckets_.size())) {}

        RetiredSet(const RetiredSet &) = delete;

        RetiredSet(RetiredSet &&other) = delete;

        RetiredSet &operator=(const RetiredSet &) = delete;

        RetiredSet &operator=(RetiredSet &&other) = delete;

    public:
        void insert(reference value) noexcept {
            retired_set_.insert(value);
        }

        void erase(reference element) noexcept {
            retired_set_.erase(retired_set_.iterator_to(element));
        }

        bool contains(key_type key) const noexcept {
            return retired_set_.contains(key);
        }

        iterator find(key_type key) noexcept {
            return retired_set_.find(key);
        }

        const_iterator find(key_type key) const noexcept {
            return retired_set_.find(key);
        }

        void merge(RetiredSet &other) noexcept {
            retired_set_.merge(other.retired_set_);
        }

        bool empty() const noexcept {
            return retired_set_.empty();
        }

        iterator begin() noexcept {
            return retired_set_.begin();
        }

        iterator end() noexcept {
            return retired_set_.end();
        }

        const_iterator begin() const noexcept {
            return retired_set_.begin();
        }

        const_iterator end() const noexcept {
            return retired_set_.end();
        }

    private:
        BucketsData buckets_{};
        SetOfRetired retired_set_;
    };

    class HazardRecord : public lu::forward_list_hook<> {
    public:
        using pointer = HazardObject *;
        using const_pointer = const HazardObject *;

    public:
        HazardRecord() = default;

        HazardRecord(const HazardRecord &other) = delete;

        HazardRecord(HazardRecord &&other) = delete;

    public:
        inline void reset(const_pointer new_ptr = {}) {
            protected_.store(new_ptr);
        }

        inline const_pointer get() const {
            return protected_.load();
        }

        inline bool empty() const {
            return !protected_.load();
        }

    private:
        std::atomic<const_pointer> protected_{};
    };

    class ProtectionsList {
        static constexpr std::size_t num_of_protections = 4;

        using FreeList = lu::forward_list<HazardRecord>;
        using Array = std::array<HazardRecord, num_of_protections>;

    public:
        using value_type = HazardRecord;

        using reference = typename Array::reference;
        using const_reference = typename Array::const_reference;
        using pointer = typename Array::pointer;
        using const_pointer = typename Array::const_pointer;

        using iterator = typename Array::iterator;
        using const_iterator = typename Array::const_iterator;

    public:
        ProtectionsList() noexcept
            : data_(), free_list_(data_.begin(), data_.end()) {}

        ProtectionsList(const ProtectionsList &) = delete;

        ProtectionsList(ProtectionsList &&other) = delete;

        ProtectionsList &operator=(const ProtectionsList &) = delete;

        ProtectionsList &operator=(ProtectionsList &&other) = delete;

    public:
        pointer acquire() noexcept {
            if (free_list_.empty()) [[unlikely]] {
                return {};
            } else {
                pointer record = &free_list_.front();
                free_list_.pop_front();
                return record;
            }
        }

        void release(pointer record) noexcept {
            assert((array_.data() <= &protection) && (array_.data() + array_.size() > &protection) && "Can't release protection from other thread");
            record->reset();
            free_list_.push_front(*record);
        }

        bool full() const noexcept {
            return free_list_.empty();
        }

        iterator begin() noexcept {
            return data_.begin();
        }

        iterator end() noexcept {
            return data_.end();
        }

        const_iterator begin() const noexcept {
            return data_.begin();
        }

        const_iterator end() const noexcept {
            return data_.end();
        }

    private:
        Array data_;
        FreeList free_list_;
    };

    class HazardPointerDomain {
        friend class HazardPointer;

    private:
        class HazardThreadData {
            friend class HazardPointerDomain;
            friend class HazardThreadDataOwner;

        public:
            explicit HazardThreadData(HazardPointerDomain &domain) noexcept
                : domain_(domain) {}

            ~HazardThreadData() {
                auto current = retired_set_.begin();
                while (current != retired_set_.end()) {
                    auto next = std::next(current);
                    destroy_retired(*current);
                    current = next;
                }
            }

        private:
            bool acquired() noexcept {
                return in_use_.load();
            }

            bool try_acquire() noexcept {
                return !in_use_.exchange(true);
            }

            void release() noexcept {
                in_use_.store(false);
            }

            void destroy_retired(HazardObject &to_erase) noexcept {
                retired_set_.erase(to_erase);
                to_erase.reclaim();
                --num_of_retires_;
            }

            void merge(HazardThreadData &other) noexcept {
                num_of_retires_ += other.num_of_retires_;
                other.num_of_retires_ = 0;
                retired_set_.merge(other.retired_set_);
            }

        public:
            HazardRecord *acquire_protection() noexcept {
                return protections_list_.acquire();
            }

            void release_protection(HazardRecord *protection) noexcept {
                protections_list_.release(protection);
            }

            void retire(HazardObject *retired_ptr) noexcept {
                retired_set_.insert(*retired_ptr);
                if (++num_of_retires_ >= scan_threshold) [[unlikely]] {
                    scan();
                }
            }

            void scan() noexcept {
                auto current_thread_data = domain_.get_head();
                while (current_thread_data) {
                    if (current_thread_data->acquired()) {
                        auto &protections = current_thread_data->protections_list_;
                        for (auto it = protections.begin(); it != protections.end(); ++it) {
                            auto protection = it->get();
                            auto found = retired_set_.find(protection);
                            if (found != retired_set_.end()) {
                                found->make_protected();
                            }
                        }
                    }
                    current_thread_data = current_thread_data->next_;
                }

                auto current = retired_set_.begin();
                while (current != retired_set_.end()) {
                    auto next = std::next(current);
                    if (current->is_protected()) {
                        current->make_unprotected();
                    } else {
                        destroy_retired(*current);
                    }
                    current = next;
                }
            }

            void help_scan() noexcept {
                auto current_thread_data = domain_.get_head();
                while (current_thread_data) {
                    if (current_thread_data->try_acquire()) {
                        current_thread_data->scan();
                        merge(*current_thread_data);
                        current_thread_data->release();
                    }
                    current_thread_data = current_thread_data->next_;
                }
                scan();
            }

        private:
            constexpr static std::size_t scan_threshold = 64;

            HazardPointerDomain &domain_;

            std::size_t num_of_retires_{};

            ProtectionsList protections_list_{};
            RetiredSet retired_set_{};

            std::atomic<bool> in_use_{true};
            HazardThreadData *next_{};
        };

        struct HazardThreadDataOwner {
            ~HazardThreadDataOwner() {
                if (thread_data) {
                    thread_data->help_scan();
                    thread_data->release();
                }
            }

            HazardThreadData *thread_data{};
        };

    public:
        HazardPointerDomain() = default;

        HazardPointerDomain(const HazardPointerDomain &other) = delete;

        HazardPointerDomain(HazardPointerDomain &&other) = delete;

        HazardPointerDomain &operator=(const HazardPointerDomain &other) = delete;

        HazardPointerDomain &operator=(HazardPointerDomain &&other) = delete;

        ~HazardPointerDomain() {
            HazardThreadData *current = head_.load();
            while (current) {
                HazardThreadData *next = current->next_;
                delete current;
                current = next;
            }
        }

    public:
        HazardThreadData *get_thread_data() noexcept {
            static thread_local HazardThreadDataOwner owner;
            if (!owner.thread_data) {
                HazardThreadData *current = get_head();
                while (current) {
                    if (current->try_acquire()) {
                        owner.thread_data = current;
                        return owner.thread_data;
                    }
                    current = current->next_;
                }
                HazardThreadData *new_thread_data = new HazardThreadData(*this);
                new_thread_data->next_ = get_head();
                while (true) {
                    if (head_.compare_exchange_weak(new_thread_data->next_, new_thread_data)) {
                        break;
                    }
                }
                owner.thread_data = new_thread_data;
            }
            return owner.thread_data;
        }

        HazardThreadData *get_head() noexcept {
            return head_.load();
        }

    private:
        std::atomic<HazardThreadData *> head_{};
    };

    inline HazardPointerDomain &get_default_domain() {
        static HazardPointerDomain domain;
        return domain;
    }

    class HazardPointer {
    public:
        HazardPointer() noexcept
            : domain_(),
              protection_() {}

        explicit HazardPointer(HazardPointerDomain *domain) noexcept
            : domain_(domain),
              protection_(domain_->get_thread_data()->acquire_protection()) {}

        HazardPointer(const HazardPointer &) = delete;

        HazardPointer(HazardPointer &&other) noexcept
            : domain_(other.domain_), protection_(other.protection_) {
            other.protection_ = {};
        }

        HazardPointer &operator=(const HazardPointer &) = delete;

        HazardPointer &operator=(HazardPointer &&other) noexcept {
            HazardPointer dummy(std::move(other));
            swap(dummy);
            return *this;
        }

        ~HazardPointer() {
            if (protection_) {
                auto thread_data = domain_->get_thread_data();
                thread_data->release_protection(protection_);
            }
        }

    public:
        bool empty() const noexcept {
            return !protection_ || protection_->empty();
        }

        explicit operator bool() const noexcept {
            return empty();
        }

        template<class Ptr, class = std::enable_if_t<std::is_base_of_v<HazardObject, typename std::pointer_traits<Ptr>::element_type>>>
        Ptr protect(const std::atomic<Ptr> &src) noexcept {
            return protect(src, [](auto &&p) { return std::forward<decltype(p)>(p); });
        }

        template<class Ptr, class Func, class = std::enable_if_t<std::is_base_of_v<HazardObject, typename std::pointer_traits<Ptr>::element_type>>>
        Ptr protect(const std::atomic<Ptr> &src, Func &&func) noexcept {
            auto ptr = src.load();
            while (!try_protect(ptr, src, std::forward<Func>(func))) {}
            return ptr;
        }

        template<class Ptr, class = std::enable_if_t<std::is_base_of_v<HazardObject, typename std::pointer_traits<Ptr>::element_type>>>
        bool try_protect(Ptr &ptr, const std::atomic<Ptr> &src) noexcept {
            return try_protect(ptr, src, [](auto &&p) { return std::forward<decltype(p)>(p); });
        }

        template<class Ptr, class Func, class = std::enable_if_t<std::is_base_of_v<HazardObject, typename std::pointer_traits<Ptr>::element_type>>>
        bool try_protect(Ptr &ptr, const std::atomic<Ptr> &src, Func &&func) noexcept {
            assert(protection_ && "hazard_ptr must be initialized");
            auto old = ptr;
            reset_protection(func(old));
            ptr = src.load();
            if (old != ptr) {
                reset_protection();
                return false;
            }
            return true;
        }

        template<class Ptr, class = std::enable_if_t<std::is_base_of_v<HazardObject, typename std::pointer_traits<Ptr>::element_type>>>
        void reset_protection(const Ptr ptr) noexcept {
            assert(protection_ && "hazard_ptr must be initialized");
            protection_->reset(ptr);
        }

        void reset_protection(nullptr_t = nullptr) noexcept {
            assert(protection_ && "hazard_ptr must be initialized");
            protection_->reset();
        }

        void swap(HazardPointer &other) noexcept {
            std::swap(domain_, other.domain_);
            std::swap(protection_, other.protection_);
        }

    public:
        friend void swap(HazardPointer &left, HazardPointer &right) noexcept {
            left.swap(right);
        }

    private:
        HazardPointerDomain *domain_;
        HazardRecord *protection_{};
    };

    template<class ValueType>
    class GuardedPointer {
    public:
        using value_type = ValueType;

        using pointer = ValueType *;
        using reference = ValueType &;
        using const_reference = const ValueType &;

    public:
        GuardedPointer() = default;

        GuardedPointer(HazardPointer guard, pointer ptr)
            : guard_(std::move(guard)), ptr_(ptr) {}

        GuardedPointer(const GuardedPointer &) = delete;

        GuardedPointer &operator=(const GuardedPointer &) = delete;

        pointer operator->() const {
            return ptr_;
        }

        reference operator*() {
            return *ptr_;
        }

        const_reference operator*() const {
            return *ptr_;
        }

        explicit operator bool() const {
            return ptr_;
        }

    private:
        HazardPointer guard_{};
        pointer ptr_{};
    };

    template<class ValueType, class Deleter>
    class HazardPointerDeleter {
    protected:
        void set_deleter(Deleter deleter) noexcept(std::is_nothrow_move_assignable_v<Deleter>) {
            deleter_ = std::move(deleter);
        }

        void do_delete(ValueType *value) {
            deleter_(value);
        }

    private:
        Deleter deleter_;
    };

    template<class ValueType, class Deleter>
    class HazardPointerObjBase : public HazardObject, private HazardPointerDeleter<ValueType, Deleter> {
    protected:
        HazardPointerObjBase() noexcept = default;

        HazardPointerObjBase(const HazardPointerObjBase &) noexcept = default;

        HazardPointerObjBase(HazardPointerObjBase &&) noexcept = default;

        HazardPointerObjBase &operator=(const HazardPointerObjBase &) noexcept = default;

        HazardPointerObjBase &operator=(HazardPointerObjBase &&) noexcept = default;

    public:
        void retire(Deleter deleter = Deleter(), HazardPointerDomain &domain = get_default_domain()) noexcept {
            assert(!retired_.exchange(true, std::memory_order_relaxed) && "Double retire is not allowed");
            this->set_deleter(std::move(deleter));
            this->set_reclaimer(reclaim_func);
            auto thread_data = domain.get_thread_data();
            thread_data->retire(this);
        }

    private:
        static void reclaim_func(HazardObject *obj) {
            auto obj_base = static_cast<HazardPointerObjBase<ValueType, Deleter> *>(obj);
            auto value = static_cast<ValueType *>(obj);
            obj_base->do_delete(value);
        }
#ifndef NDEBUG
    private:
        std::atomic<bool> retired_{false};
#endif
    };

    inline HazardPointer make_hazard_pointer(HazardPointerDomain &domain = get_default_domain()) {
        return HazardPointer(&domain);
    }

    template<class ValueType, class Deleter = std::default_delete<ValueType>>
    using hazard_pointer_obj_base = HazardPointerObjBase<ValueType, Deleter>;

    using hazard_pointer_domain = HazardPointerDomain;

    using hazard_pointer = HazardPointer;

    template<class ValueType>
    using guarded_ptr = GuardedPointer<ValueType>;
}// namespace lu

#endif