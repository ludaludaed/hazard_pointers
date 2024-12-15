#ifndef __HAZARD_POINTERS_H__
#define __HAZARD_POINTERS_H__

#include "intrusive/forward_list.h"
#include "intrusive/options.h"
#include "intrusive/unordered_set.h"
#include "thread_local_list.h"
#include "utils.h"

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <type_traits>
#include <utility>


namespace lu {
    class HazardPointerTag {};

    using HazardPointerHook = lu::unordered_set_base_hook<
            lu::tag<HazardPointerTag>,
            lu::store_hash<false>,
            lu::is_auto_unlink<false>>;

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

    using HazardRetiresSet = lu::unordered_set<
            HazardObject,
            lu::base_hook<HazardPointerHook>,
            lu::is_power_2_buckets<true>,
            lu::key_of_value<RawPointerKeyOfValue<HazardObject>>,
            lu::hash<detail::PointerHash>>;

    class HazardRetires : public HazardRetiresSet {
        using Base = HazardRetiresSet;

        using BucketType = typename Base::bucket_type;
        using BucketTraits = typename Base::bucket_traits;

    public:
        using resource = std::span<BucketType>;

    public:
        HazardRetires(resource buckets) noexcept
            : Base(BucketTraits(buckets.data(), buckets.size())) {
            for (std::size_t i = 0; i < buckets.size(); ++i) {
                ::new (buckets.data() + i) BucketType();
            }
        }
    };

    class HazardRecord : public lu::forward_list_base_hook<> {
    public:
        using pointer = HazardObject *;
        using const_pointer = const HazardObject *;

    public:
        HazardRecord() = default;

        HazardRecord(const HazardRecord &) = delete;

        HazardRecord(HazardRecord &&) = delete;

        inline void reset(const_pointer new_ptr = {}) {
            protected_.store(new_ptr, std::memory_order_release);
        }

        inline const_pointer get() const noexcept {
            return protected_.load(std::memory_order_acquire);
        }

        inline bool empty() const noexcept {
            return !protected_.load(std::memory_order_acquire);
        }

    private:
        std::atomic<const_pointer> protected_{};
    };

    class HazardRecords {
    public:
        using resource = std::span<HazardRecord>;

        using value_type = HazardRecord;

        using reference = typename resource::reference;
        using const_reference = typename resource::const_reference;
        using pointer = typename resource::pointer;
        using const_pointer = typename resource::const_pointer;

        using iterator = pointer;
        using const_iterator = const_pointer;

    public:
        HazardRecords(resource data) noexcept
            : data_(data) {
            for (std::size_t i = 0; i < data_.size(); ++i) {
                ::new (data_.data() + i) value_type();
                free_list_.push_front(data_[i]);
            }
        }

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
            assert((data_.data() <= record) && (data_.data() + data_.size() > record)
                   && "Can't release hazard record from other thread");
            if (record) [[likely]] {
                record->reset();
                free_list_.push_front(*record);
            }
        }

        bool full() const noexcept {
            return free_list_.empty();
        }

        iterator begin() noexcept {
            return data_.data();
        }

        iterator end() noexcept {
            return data_.data() + data_.size();
        }

        const_iterator begin() const noexcept {
            return data_.data();
        }

        const_iterator end() const noexcept {
            return data_.data() + data_.size();
        }

    private:
        resource data_;
        lu::forward_list<HazardRecord> free_list_{};
    };

    class HazardThreadData : public lu::thread_local_list_base_hook {
        friend class HazardPointerDomain;

    public:
        using records_resource = typename HazardRecords::resource;
        using retires_resource = typename HazardRetires::resource;

    public:
        HazardThreadData(std::size_t scan_threshold, records_resource records_resource, retires_resource retires_resource)
            : scan_threshold_(scan_threshold)
            , records_(records_resource)
            , retires_(retires_resource) {}

        HazardThreadData(const HazardThreadData &) = delete;

        HazardThreadData(HazardThreadData &&) = delete;

        ~HazardThreadData() {
            clear();
        }

        void clear() {
            auto current = retires_.begin();
            while (current != retires_.end()) {
                auto prev = current++;
                reclaim(*prev);
            }
        }

        void reclaim(HazardObject &retired) {
            retires_.erase(retires_.iterator_to(retired));
            retired.reclaim();
            num_of_reclaimed.fetch_add(1, std::memory_order_relaxed);
        }

        bool retire(HazardObject &retired) {
            retires_.insert(retired);
            num_of_retired.fetch_add(1, std::memory_order_relaxed);
            return retires_.size() >= scan_threshold_;
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
        std::size_t scan_threshold_;
        HazardRecords records_;
        HazardRetires retires_;

        std::atomic<std::size_t> num_of_retired;
        std::atomic<std::size_t> num_of_reclaimed;
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
            Creator(std::size_t num_of_records, std::size_t num_of_retires, std::size_t scan_threshold)
                : num_of_records_(num_of_records)
                , num_of_retires_(num_of_retires)
                , scan_threshold_(scan_threshold) {}

            HazardThreadData *operator()() const {
                using records_resource = typename HazardThreadData::records_resource;
                using records_element_type = typename records_resource::element_type;

                using retires_resource = typename HazardThreadData::retires_resource;
                using retires_element_type = typename retires_resource::element_type;

                std::size_t header_size = sizeof(HazardThreadData);
                std::size_t records_resource_size = sizeof(records_element_type) * num_of_records_;
                std::size_t retires_resource_size = sizeof(retires_element_type) * num_of_retires_;

                std::size_t size = header_size + records_resource_size + retires_resource_size;

                auto blob = new std::uint8_t[size];
                auto records = reinterpret_cast<records_element_type *>(blob + header_size);
                auto retires = reinterpret_cast<retires_element_type *>(blob + header_size + records_resource_size);

                records_resource _records_resource(records, num_of_records_);
                retires_resource _retires_resource(retires, num_of_retires_);

                ::new (blob) HazardThreadData(scan_threshold_, _records_resource, _retires_resource);
                auto thread_data = reinterpret_cast<HazardThreadData *>(blob);

                return thread_data;
            }

        private:
            std::size_t num_of_records_;
            std::size_t num_of_retires_;
            std::size_t scan_threshold_;
        };

        struct Deleter {
            void operator()(HazardThreadData *thread_data) const {
                thread_data->~HazardThreadData();
                delete[] reinterpret_cast<std::uint8_t *>(thread_data);
            }
        };

    public:
        HazardPointerDomain(std::size_t num_of_records, std::size_t num_of_retires, std::size_t scan_threshold)
            : list_(Detacher(this),
                    Creator(num_of_records, num_of_retires, scan_threshold),
                    Deleter()) {}

        HazardPointerDomain(const HazardPointerDomain &) = delete;

        HazardPointerDomain(HazardPointerDomain &&) = delete;

        void attach_thread() {
            list_.attach_thread();
        }

        void detach_thread() {
            list_.detach_thread();
        }

        void retire(HazardObject *retired) {
            auto &thread_data = list_.get_thread_local();
            if (thread_data.retire(*retired)) [[unlikely]] {
                scan();
            }
        }

        HazardRecord *acquire_record() noexcept {
            auto &thread_data = list_.get_thread_local();
            return thread_data.acquire_record();
        }

        void release_record(HazardRecord *record) noexcept {
            auto &thread_data = list_.get_thread_local();
            thread_data.release_record(record);
        }

        std::size_t num_of_retired() {
            std::size_t result{};
            for (auto it = list_.begin(); it != list_.end(); ++it) {
                result += it->num_of_retired.load(std::memory_order_relaxed);
            }
            return result;
        }

        std::size_t num_of_reclaimed() {
            std::size_t result{};
            for (auto it = list_.begin(); it != list_.end(); ++it) {
                result += it->num_of_reclaimed.load(std::memory_order_relaxed);
            }
            return result;
        }

    private:
        void scan() {
            auto &thread_data = list_.get_thread_local();
            auto &retires = thread_data.retires_;
            std::atomic_thread_fence(std::memory_order_seq_cst);
            for (auto current = list_.begin(); current != list_.end(); ++current) {
                if (!current->is_acquired()) {
                    continue;
                }
                auto &records = current->records_;
                for (auto record = records.begin(); record != records.end(); ++record) {
                    auto found = retires.find(record->get());
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
                    thread_data.reclaim(*prev);
                }
            }
        }

        void help_scan() {
            auto &thread_data = list_.get_thread_local();
            for (auto current = list_.begin(); current != list_.end(); ++current) {
                if (current->try_acquire()) {
                    thread_data.merge(*current);
                    current->release();
                }
            }
            scan();
        }

    private:
        lu::thread_local_list<HazardThreadData> list_;
    };

    inline HazardPointerDomain &get_default_domain();

    class HazardPointer {
    public:
        HazardPointer() = default;

        explicit HazardPointer(HazardPointerDomain *domain) noexcept
            : domain_(domain)
            , record_(domain_->acquire_record()) {}

        HazardPointer(const HazardPointer &) = delete;

        HazardPointer(HazardPointer &&other) noexcept
            : domain_(other.domain_)
            , record_(other.record_) {
            other.record_ = {};
        }

        HazardPointer &operator=(const HazardPointer &) = delete;

        HazardPointer &operator=(HazardPointer &&other) noexcept {
            HazardPointer temp(std::move(other));
            swap(temp);
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
            auto ptr = src.load(std::memory_order_relaxed);
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
            ptr = src.load(std::memory_order_acquire);
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
            std::atomic_thread_fence(std::memory_order_seq_cst);
        }

        void reset_protection(nullptr_t = nullptr) noexcept {
            assert(record_ && "hazard_ptr must be initialized");
            record_->reset();
            std::atomic_thread_fence(std::memory_order_seq_cst);
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
            : guard_(std::move(guard))
            , ptr_(ptr) {}

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

    static constexpr std::size_t DEFAULT_NUM_OF_RECORDS = 4;
    static constexpr std::size_t DEFAULT_NUM_OF_RETIRES = 64;
    static constexpr std::size_t DEFAULT_SCAN_THRESHOLD = 64;

    inline HazardPointerDomain &get_default_domain() {
        static HazardPointerDomain domain(DEFAULT_NUM_OF_RECORDS, DEFAULT_NUM_OF_RETIRES, DEFAULT_SCAN_THRESHOLD);
        return domain;
    }

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