#ifndef TVP_FILE_OPERATIONS_H
#define TVP_FILE_OPERATIONS_H

#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace TVPFileIO {

// These access values are the TJS_BS_* access modes (without option bits).
inline int Open(const char *path, unsigned int access) {
    int flags;
    switch (access) {
    case 0: flags = O_RDONLY; break;
    case 1: flags = O_RDWR | O_CREAT | O_TRUNC; break;
    case 2: flags = O_RDWR | O_CREAT | O_APPEND; break;
    case 3: flags = O_RDWR; break;
    default: errno = EINVAL; return -1;
    }
    int fd;
    do { fd = open(path, flags | O_CLOEXEC, 0666); } while (fd < 0 && errno == EINTR);
    return fd;
}

inline ssize_t Read(int fd, void *buffer, size_t size) {
    ssize_t result;
    do { result = read(fd, buffer, size); } while (result < 0 && errno == EINTR);
    return result;
}

inline bool WriteAll(int fd, const void *buffer, size_t size) {
    const char *bytes = static_cast<const char *>(buffer);
    while (size) {
        const size_t chunk = size > static_cast<size_t>(SSIZE_MAX) ? SSIZE_MAX : size;
        const ssize_t written = write(fd, bytes, chunk);
        if (written < 0 && errno == EINTR) continue;
        if (written <= 0) return false;
        bytes += written;
        size -= written;
    }
    return true;
}

inline bool TruncateHere(int fd) {
    const off_t offset = lseek(fd, 0, SEEK_CUR);
    if (offset < 0) return false;
    int result;
    do { result = ftruncate(fd, offset); } while (result < 0 && errno == EINTR);
    return result == 0;
}

// Replace only after every byte and the close have succeeded. The temporary
// file is in the same directory, so rename cannot cross filesystem boundaries.
inline bool WriteAtomic(const std::string &path, const void *data, size_t size) {
    std::string pattern = path + ".tmp.XXXXXX";
    std::vector<char> temporary(pattern.begin(), pattern.end());
    temporary.push_back('\0');
    int fd = mkstemp(temporary.data());
    if (fd < 0) return false;
    bool ok = WriteAll(fd, data, size);
    if (ok) {
        int result;
        do { result = fsync(fd); } while (result < 0 && errno == EINTR);
        ok = result == 0;
    }
    if (close(fd) != 0) ok = false;
    if (ok) ok = rename(temporary.data(), path.c_str()) == 0;
    if (!ok) unlink(temporary.data());
    return ok;
}

} // namespace TVPFileIO
#endif
