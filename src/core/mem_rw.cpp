#include <android/log.h>
#include <string.h>
#include <errno.h>
#include "../../include/mi_core.h"
#include <sys/uio.h>

#define LOG_TAG "Angels/RW"
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace mi {

bool read_mem(pid_t pid, uintptr_t addr, void* dst, size_t len) {
    if (!dst || len == 0 || pid <= 0) return false;
    struct iovec local  = { dst, len };
    struct iovec remote = { reinterpret_cast<void*>(addr), len };
    ssize_t n = process_vm_readv(pid, &local, 1, &remote, 1, 0);
    if (n < 0) {
        LOGE("process_vm_readv failed: %s (pid=%d, addr=%p)", strerror(errno), pid, (void*)addr);
    }
    return (n >= 0 && static_cast<size_t>(n) == len);
}

bool write_mem(pid_t pid, uintptr_t addr, const void* src, size_t len) {
    if (!src || len == 0 || pid <= 0) return false;
    struct iovec local  = { const_cast<void*>(src), len };
    struct iovec remote = { reinterpret_cast<void*>(addr), len };
    ssize_t n = process_vm_writev(pid, &local, 1, &remote, 1, 0);
    if (n < 0) {
        LOGE("process_vm_writev failed: %s (pid=%d, addr=%p)", strerror(errno), pid, (void*)addr);
    }
    return (n >= 0 && static_cast<size_t>(n) == len);
}

} // namespace mi
