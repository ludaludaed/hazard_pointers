#ifndef __BACK_OF_H__
#define __BACK_OF_H__

#include <thread>

namespace lu {
    struct EmptyBackOff {
        void operator()() const {}
    };

    struct YieldBackOff {
        void operator()() const {
            std::this_thread::yield();
        }
    };
}// namespace lu

#endif