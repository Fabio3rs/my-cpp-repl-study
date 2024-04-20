#include "backtraced_exceptions.hpp"

#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <dlfcn.h>
#include <exception>
#include <execinfo.h>

static void *(*cxa_allocate_exception_o)(size_t val) = nullptr;

static constexpr uintptr_t BACKTRACE_MAGIC = 0xFADED0DEBAC57ACELL;

extern "C" void *__cxa_allocate_exception(size_t size) noexcept {
    std::array<void *, 1024> exceptionsbt{};
    int bt_size = backtrace(exceptionsbt.data(), exceptionsbt.size());

    assert(bt_size > 0);

    size_t bt_sizesz = static_cast<size_t>(bt_size < 0 ? 0 : bt_size);

    // align to 16 bytes
    size_t nsize = (size + 0xFULL) & (~0xFULL);

    size_t magic_pos = nsize;

    // Add space for the magic number
    nsize += sizeof(uintptr_t);

    // Add space for pointer to the backtrace
    nsize += sizeof(uintptr_t);

    nsize += sizeof(size_t);

    nsize += sizeof(void *) * bt_sizesz;

    void *ptr = cxa_allocate_exception_o(nsize);

    if (ptr) {
        auto uptr = reinterpret_cast<uintptr_t>(ptr);

        // Set the magic number
        // NOLINTNEXTLINE
        *reinterpret_cast<uintptr_t *>(uptr + magic_pos) = BACKTRACE_MAGIC;

        // Set the pointer to the backtrace
        // NOLINTNEXTLINE
        auto ptrcalc = uptr + magic_pos + sizeof(BACKTRACE_MAGIC);

        // Set the size of the backtrace
        // NOLINTNEXTLINE
        *reinterpret_cast<size_t *>(ptrcalc) = bt_sizesz;

        // Set the backtrace
        // NOLINTNEXTLINE
        auto *btrace = reinterpret_cast<void **>(ptrcalc + sizeof(size_t));

        for (size_t i = 0; i < bt_sizesz; i++) {
            btrace[i] = exceptionsbt[i];
        }
    }

    return ptr;
}

auto backtraced_exceptions::get_backtrace_for(const std::exception &e)
    -> std::pair<void *const *, int> {
    auto *ptr = reinterpret_cast<const uint8_t *>(&e);

    // align the pointer to 16 bytes
    auto alignp = (reinterpret_cast<uintptr_t>(ptr) + 0xF);
    ptr = reinterpret_cast<const uint8_t *>(alignp & (~0xFULL));

    // Get the magic number
    // NOLINTNEXTLINE
    auto *magic = reinterpret_cast<const uintptr_t *>(ptr + sizeof(uintptr_t));

    // find the magic number
    for (size_t i = 0; i < 1024 / sizeof(uintptr_t); i++) {
        if (magic[i] == BACKTRACE_MAGIC) {
            ++i;

            // NOLINTNEXTLINE
            auto *size = reinterpret_cast<const size_t *>(magic + i);

            // NOLINTNEXTLINE
            auto *btrace =
                reinterpret_cast<void *const *>(const_cast<size_t *>(size + 1));

            return std::make_pair(btrace, static_cast<int>(*size));
        }
    }

    return std::make_pair(static_cast<void *const *>(nullptr), 0);
}

namespace {
struct global_initer {
    global_initer() noexcept {
        cxa_allocate_exception_o =
            reinterpret_cast<decltype(cxa_allocate_exception_o)>(
                dlsym(RTLD_NEXT, "__cxa_allocate_exception"));
    }
} except_initer;
} // namespace
