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
#include "thread_local_list.h"


namespace lu {
    class HazardPointerTag {};

    using HazardPointerHook = lu::unordered_set_base_hook<lu::tag<HazardPointerTag>, lu::store_hash<false>, lu::is_auto_unlink<false>>;

    class HazardObject : public HazardPointerHook {
        friend class HazardThreadData;
        friend class HazardPointerDomain;

        template<class, class>
        friend class HazardPointerObjBase;

        using ReclaimFunc = void(HazardObject *value);
        using ReclaimFuncPtr = void (*)(HazardObject *value);

        ~HazardObject() {
            assert(!this->is_linked());
        }

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

        using BucketType = typename SetOfRetired::bucket_type;
        using BucketTraits = typename SetOfRetired::bucket_traits;
        using Buckets = std::array<BucketType, num_of_buckets>;

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

        RetiredSet(RetiredSet &&) = delete;

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

        std::size_t size() const {
            return retired_set_.size();
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
        Buckets buckets_{};
        SetOfRetired retired_set_;
    };

    class HazardRecord : public lu::forward_list_hook<> {
    public:
        using pointer = HazardObject *;
        using const_pointer = const HazardObject *;

    public:
        HazardRecord() = default;

        HazardRecord(const HazardRecord &) = delete;

        HazardRecord(HazardRecord &&) = delete;

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

    class HazardRecords {
        static constexpr std::size_t num_of_records = 4;

        using Array = std::array<HazardRecord, num_of_records>;

    public:
        using value_type = HazardRecord;

        using reference = typename Array::reference;
        using const_reference = typename Array::const_reference;
        using pointer = typename Array::pointer;
        using const_pointer = typename Array::const_pointer;

        using iterator = typename Array::iterator;
        using const_iterator = typename Array::const_iterator;

    public:
        HazardRecords() noexcept
            : data_(), free_list_(data_.begin(), data_.end()) {}

        HazardRecords(const HazardRecords &) = delete;

        HazardRecords(HazardRecords &&) = delete;

    public:
        pointer acquire() noexcept {
            pointer record{};
            if (!free_list_.empty()) [[likely]] {
                record = &free_list_.front();
                free_list_.pop_front();
            }
            return record;
        }

        void release(pointer record) noexcept {
            assert((data_.data() <= record) && (data_.data() + data_.size() > record) &&
                   "Can't release hazard record from other thread");
            if (record) [[likely]] {
                record->reset();
                free_list_.push_front(*record);
            }
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
        lu::forward_list<HazardRecord> free_list_;
    };

    class HazardThreadData : public lu::thread_local_list_base_hook<> {
        friend class HazardPointerDomain;

    public:
        HazardThreadData() = default;

        HazardThreadData(const HazardThreadData &) = delete;

        HazardThreadData(HazardThreadData &&) = delete;

        ~HazardThreadData() {
            clear();
        }

        void clear() {
            auto current = retires_.begin();
            while (current != retires_.end()) {
                auto prev = current++;
                destroy_retired(*prev);
            }
        }

        void destroy_retired(HazardObject &retired) {
            retires_.erase(retired);
            retired.reclaim();
        }

        bool retire(HazardObject &retired) {
            retires_.insert(retired);
            return retires_.size() >= scan_threshold;
        }

        void merge(HazardThreadData &other) {
            retires_.merge(other.retires_);
        }

        HazardRecord *acquire_record() noexcept {
            return records_.acquire();
        }

        void release_record(HazardRecord *record) noexcept {
            records_.release(record);
        }

    private:
        constexpr static std::size_t scan_threshold = 64;
        HazardRecords records_{};
        RetiredSet retires_{};
    };

    class HazardPointerDomain {
        struct Detacher {
            explicit Detacher(HazardPointerDomain *domain)
                : domain_(domain) {}

            void operator()(HazardThreadData *) const {
                domain_->help_scan();
            }

        private:
            HazardPointerDomain *domain_;
        };

        struct Creator {
            HazardThreadData *operator()() const {
                return new HazardThreadData();
            }
        };

    public:
        HazardPointerDomain() : list_(Detacher(this), Creator()) {}

        HazardPointerDomain(const HazardPointerDomain &) = delete;

        HazardPointerDomain(HazardPointerDomain &&) = delete;

        void attach_thread() {
            list_.attach_thread();
        }

        void detach_thread() {
            list_.detach_thread();
        }

        void retire(HazardObject *retired) {
            auto thread_data = list_.get_thread_local();
            if (thread_data->retire(*retired)) [[unlikely]] {
                scan();
            }
        }

        HazardRecord *acquire_record() noexcept {
            auto thread_data = list_.get_thread_local();
            return thread_data->acquire_record();
        }

        void release_record(HazardRecord *record) noexcept {
            auto thread_data = list_.get_thread_local();
            thread_data->release_record(record);
        }

    private:
        void scan() {
            auto thread_data = list_.get_thread_local();
            auto& retires = thread_data->retires_;
            for (auto current = list_.begin(); current != list_.end(); ++current) {
                if (!current->is_acquired()) {
                    continue;
                }
                auto &records = current->records_;
                for (auto it = records.begin(); it != records.end(); ++it) {
                    auto record = it->get();
                    auto found = retires.find(record);
                    if (found != retires.end()) {
                        found->make_protected();
                    }
                }
            }

            auto current = retires.begin();
            while (current != retires.end()) {
                auto prev = current++;
                if (prev->is_protected()) {
                    prev->make_unprotected();
                } else {
                    thread_data->destroy_retired(*prev);
                }
            }
        }

        void help_scan() {
            auto thread_data = list_.get_thread_local();
            for (auto current = list_.begin(); current != list_.end(); ++current) {
                if (current->try_acquire()) {
                    thread_data->merge(*current);
                    current->release();
                }
            }
            scan();
        }

    private:
        lu::thread_local_list<HazardThreadData> list_;
    };

    inline HazardPointerDomain &get_default_domain() {
        static HazardPointerDomain domain;
        return domain;
    }

    class HazardPointer {
    public:
        HazardPointer() = default;

        explicit HazardPointer(HazardPointerDomain *domain) noexcept
            : domain_(domain),
              record_(domain_->acquire_record()) {}

        HazardPointer(const HazardPointer &) = delete;

        HazardPointer(HazardPointer &&other) noexcept
            : domain_(other.domain_), record_(other.record_) {
            other.record_ = {};
        }

        HazardPointer &operator=(const HazardPointer &) = delete;

        HazardPointer &operator=(HazardPointer &&other) noexcept {
            HazardPointer dummy(std::move(other));
            swap(dummy);
            return *this;
        }

        ~HazardPointer() {
            if (record_) {
                domain_->release_record(record_);
            }
        }

    public:
        bool empty() const noexcept {
            return !record_ || record_->empty();
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
            assert(record_ && "hazard_ptr must be initialized");
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
            assert(record_ && "hazard_ptr must be initialized");
            record_->reset(ptr);
        }

        void reset_protection(nullptr_t = nullptr) noexcept {
            assert(record_ && "hazard_ptr must be initialized");
            record_->reset();
        }

        void swap(HazardPointer &other) noexcept {
            std::swap(domain_, other.domain_);
            std::swap(record_, other.record_);
        }

    public:
        friend void swap(HazardPointer &left, HazardPointer &right) noexcept {
            left.swap(right);
        }

    private:
        HazardPointerDomain *domain_{};
        HazardRecord *record_{};
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

    public:
        void retire(Deleter deleter = Deleter(), HazardPointerDomain &domain = get_default_domain()) noexcept {
            assert(!retired_.exchange(true, std::memory_order_relaxed) && "Double retire is not allowed");
            this->set_deleter(std::move(deleter));
            this->set_reclaimer(reclaim_func);
            domain.retire(this);
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

    inline void attach_thread(HazardPointerDomain &domain = get_default_domain()) {
        domain.attach_thread();
    }

    inline void detach_thread(HazardPointerDomain &domain = get_default_domain()) {
        domain.detach_thread();
    }
}// namespace lu

#endif