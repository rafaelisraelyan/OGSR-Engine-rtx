#pragma once

// Только для отдельной тестовой сборки; публичный API и production .lib
// не содержат переключателей отказа. Флаги относятся к вызывающему потоку.
#ifndef GR_JOBS_TESTING
#error JobTestHooks requires GR_JOBS_TESTING
#endif

#include <memory>
#include <new>
#include <utility>

namespace gr { namespace jobs { namespace testing {

inline thread_local bool failRegistration = false;
inline thread_local bool failSubmit = false;
inline thread_local unsigned registrationFailures = 0;

inline bool RejectSubmit() noexcept
{
    return std::exchange(failSubmit, false);
}

template<class T>
struct RegistrationAllocator : std::allocator<T>
{
    template<class U> struct rebind { using other = RegistrationAllocator<U>; };
    RegistrationAllocator() = default;
    template<class U> RegistrationAllocator(const RegistrationAllocator<U>&) noexcept {}

    T* allocate(std::size_t count)
    {
        // Реальный путь выделения памяти vector, а не исключение после Submit.
        if (std::exchange(failRegistration, false)) {
            ++registrationFailures;
            throw std::bad_alloc();
        }
        return std::allocator<T>::allocate(count);
    }
};

}}}
