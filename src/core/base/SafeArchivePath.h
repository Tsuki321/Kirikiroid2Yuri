#pragma once
#include <cerrno>
#include <string>
#include <vector>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace TVPArchivePath {
inline bool Components(const std::string &name, std::vector<std::string> &parts) {
    parts.clear();
    if (name.empty() || name[0] == '/' || name[0] == '\\' || name.find('\0') != std::string::npos)
        return false;
    size_t start = 0;
    while (start < name.size()) {
        size_t end = name.find_first_of("/\\", start);
        if (end == std::string::npos) end = name.size();
        std::string part = name.substr(start, end - start);
        if (part.empty() || part == "." || part == ".." || part.find(':') != std::string::npos)
            return false;
        parts.push_back(part);
        start = end + 1;
    }
    return !parts.empty();
}

// Walk relative to open directory descriptors. O_NOFOLLOW on every component
// also blocks existing symlinks and races which replace a checked directory.
inline int OpenFile(const std::string &root, const std::string &entry) {
    std::vector<std::string> parts;
    if (!Components(entry, parts)) { errno = EINVAL; return -1; }
    std::string directory = root;
    while (directory.size() > 1 && directory.back() == '/') directory.pop_back();
    if (mkdir(directory.c_str(), 0777) < 0 && errno != EEXIST) return -1;
    int parent = open(directory.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (parent < 0) return -1;
    for (size_t i = 0; i + 1 < parts.size(); ++i) {
        if (mkdirat(parent, parts[i].c_str(), 0777) < 0 && errno != EEXIST) {
            close(parent); return -1;
        }
        int child = openat(parent, parts[i].c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        close(parent);
        if (child < 0) return -1;
        parent = child;
    }
    // Open without truncation first so a hard link cannot damage another file.
    int fd = openat(parent, parts.back().c_str(), O_WRONLY | O_CREAT | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK, 0666);
    close(parent);
    if (fd < 0) return -1;
    struct stat info;
    if (fstat(fd, &info) != 0 || !S_ISREG(info.st_mode) || info.st_nlink != 1 || ftruncate(fd, 0) != 0) {
        close(fd); errno = EINVAL; return -1;
    }
    return fd;
}
}
