#include "prx/libkernel/File/include/DescriptorFlags.hpp"
#include "prx/libkernel/File/include/FileFlags.hpp"
#include "prx/libkernel/KernelErrors.hpp"
#include "prx/libkernel/Socket/include/SocketRuntime.hpp"
#include "prx/libc/include/general/VabiMacros.hpp"

#include <cerrno>
#include <cstdarg>
#include <cstdint>
#include <mutex>
#include <stdexcept>
#include <string>
#include <unordered_map>

#ifdef _WIN32
#include <io.h>
#include <stdlib.h>
#else
#include <fcntl.h>
#endif

extern "C" int* APS5_VABI __error_nid_postfix();

namespace {

constexpr int GuestGetDescriptorFlags = 1;
constexpr int GuestSetDescriptorFlags = 2;
constexpr int GuestGetStatusFlags = 3;
constexpr int GuestSetStatusFlags = 4;
constexpr int GuestCloseOnExec = 1;
constexpr int GuestAsync = 0x40;
constexpr int SettableFlags = SCE_KERNEL_O_APPEND | SCE_KERNEL_O_NONBLOCK | GuestAsync | SCE_KERNEL_O_FSYNC | SCE_KERNEL_O_DIRECT;
constexpr int StatusFlags = SCE_KERNEL_O_ACCMODE | SettableFlags;

struct DescriptorState {
    int status;
    int descriptor;
};

std::mutex descriptorsMutex;
std::unordered_map<int, DescriptorState> descriptors;

#ifdef _WIN32
extern "C" _invalid_parameter_handler _set_thread_local_invalid_parameter_handler(_invalid_parameter_handler);
void IgnoreInvalidParameter(const wchar_t*, const wchar_t*, const wchar_t*, unsigned int, std::uintptr_t) {}
bool NativeDescriptorOpen(int fd) {
    const auto previous = _set_thread_local_invalid_parameter_handler(IgnoreInvalidParameter);
    const auto handle = ::_get_osfhandle(fd);
    _set_thread_local_invalid_parameter_handler(previous);
    return handle != -1;
}
void NativeSetAppend(int fd, bool) {
    throw std::runtime_error("sceKernelFcntl: changing O_APPEND of an open descriptor is not supported on Windows, fd=" + std::to_string(fd));
}
#else
bool NativeDescriptorOpen(int fd) {
    return ::fcntl(fd, F_GETFD) != -1;
}
void NativeSetAppend(int fd, bool enabled) {
    const int flags = ::fcntl(fd, F_GETFL);
    if (flags == -1 || ::fcntl(fd, F_SETFL, enabled ? flags | O_APPEND : flags & ~O_APPEND) == -1) {
        throw std::runtime_error("sceKernelFcntl: native fcntl failed, fd=" + std::to_string(fd) + ", errno=" + std::to_string(errno));
    }
}
#endif

}

namespace File {

void RecordDescriptorFlags(int fd, int openFlags) {
    std::lock_guard lock(descriptorsMutex);
    descriptors[fd] = DescriptorState{openFlags & StatusFlags, 0};
}

void ForgetDescriptorFlags(int fd) {
    std::lock_guard lock(descriptorsMutex);
    descriptors.erase(fd);
}

int Fcntl(int fd, int command, int argument) {
    if (command < GuestGetDescriptorFlags || command > GuestSetStatusFlags) {
        throw std::runtime_error("sceKernelFcntl: unsupported command " + std::to_string(command));
    }
    if (fd < 0) return SCE_KERNEL_ERROR_EBADF;
    std::lock_guard lock(descriptorsMutex);
    const auto found = descriptors.find(fd);
    if (found == descriptors.end()) {
        if (NativeDescriptorOpen(fd)) {
            throw std::runtime_error("sceKernelFcntl: descriptor was not opened by sceKernelOpen, fd=" + std::to_string(fd));
        }
        return SCE_KERNEL_ERROR_EBADF;
    }
    auto& state = found->second;
    switch (command) {
        case GuestGetDescriptorFlags: return state.descriptor;
        case GuestSetDescriptorFlags: state.descriptor = argument & GuestCloseOnExec; return 0;
        case GuestGetStatusFlags: return state.status;
        default: break;
    }
    const int requested = argument & SettableFlags;
    const int changed = (state.status ^ requested) & SettableFlags;
    if (changed & (GuestAsync | SCE_KERNEL_O_FSYNC | SCE_KERNEL_O_DIRECT)) {
        throw std::runtime_error("sceKernelFcntl: unsupported status flag change " +std::to_string(changed) + ", fd=" + std::to_string(fd));
    }
    if (changed & SCE_KERNEL_O_APPEND) NativeSetAppend(fd, (requested & SCE_KERNEL_O_APPEND) != 0);
    state.status = (state.status & ~SettableFlags) | requested;
    return 0;
}

}

namespace {

bool TakesArgument(int command) {
    return command == GuestSetDescriptorFlags || command == GuestSetStatusFlags;
}

int KernelFcntl(int fd, int command, int argument) {
    if (fd >= GuestSockets::FirstDescriptor) {
        const int result = GuestSockets::Fcntl(fd, command, argument);
        return result == -1 ? SceKernelError(*__error_nid_postfix()) : result;
    }
    return File::Fcntl(fd, command, argument);
}

}

extern "C" {

int APS5_VABI sceKernelFcntl(int fd, int command, ...) {
    int argument = 0;
    if (TakesArgument(command)) {
#ifdef _WIN32
        __builtin_sysv_va_list arguments;
        __builtin_sysv_va_start(arguments, command);
        argument = __builtin_va_arg(arguments, int);
        __builtin_sysv_va_end(arguments);
#else
        std::va_list arguments;
        va_start(arguments, command);
        argument = va_arg(arguments, int);
        va_end(arguments);
#endif
    }
    return KernelFcntl(fd, command, argument);
}

int APS5_VABI fcntl_nid_postfix(int fd, int command, ...) {
    int argument = 0;
    if (TakesArgument(command)) {
#ifdef _WIN32
        __builtin_sysv_va_list arguments;
        __builtin_sysv_va_start(arguments, command);
        argument = __builtin_va_arg(arguments, int);
        __builtin_sysv_va_end(arguments);
#else
        std::va_list arguments;
        va_start(arguments, command);
        argument = va_arg(arguments, int);
        va_end(arguments);
#endif
    }
    const int result = KernelFcntl(fd, command, argument);
    if (result >= 0) return result;
    *__error_nid_postfix() = result & 0xffff;
    return -1;
}

}
