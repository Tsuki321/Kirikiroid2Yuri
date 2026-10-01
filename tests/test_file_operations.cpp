#include <gtest/gtest.h>
#include "FileOperations.h"
#include "SafeArchivePath.h"
#include "StartOnceWorker.h"
#include <atomic>
#include <fstream>
#include <ftw.h>
#include <iterator>

class Files : public ::testing::Test {
protected:
    std::string root;
    void SetUp() override {
        char path[] = "/tmp/krkr-files-XXXXXX";
        char *result = mkdtemp(path);
        ASSERT_NE(nullptr, result);
        root = result;
    }
    static int Remove(const char *path, const struct stat *, int, struct FTW *) { return remove(path); }
    void TearDown() override { if (!root.empty()) nftw(root.c_str(), Remove, 16, FTW_DEPTH | FTW_PHYS); }
    std::string ReadFile(const std::string &name) {
        std::ifstream stream(name, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
    }
};

TEST_F(Files, AppendCreatesAndAlwaysAppendsAfterSeeking) {
    std::string path = root + "/save.dat";
    int fd = TVPFileIO::Open(path.c_str(), 2);
    ASSERT_GE(fd, 0);
    ASSERT_TRUE(TVPFileIO::WriteAll(fd, "first", 5));
    ASSERT_EQ(0, lseek(fd, 0, SEEK_SET));
    ASSERT_TRUE(TVPFileIO::WriteAll(fd, "second", 6));
    close(fd);
    EXPECT_EQ("firstsecond", ReadFile(path));
    fd = TVPFileIO::Open(path.c_str(), 2);
    ASSERT_GE(fd, 0);
    ASSERT_TRUE(TVPFileIO::WriteAll(fd, "!", 1));
    close(fd);
    EXPECT_EQ("firstsecond!", ReadFile(path));
}

TEST_F(Files, TruncateUsesCurrentPosition) {
    std::string path = root + "/save.dat";
    int fd = TVPFileIO::Open(path.c_str(), 1);
    ASSERT_GE(fd, 0);
    ASSERT_TRUE(TVPFileIO::WriteAll(fd, "abcdef", 6));
    ASSERT_EQ(2, lseek(fd, 2, SEEK_SET));
    ASSERT_TRUE(TVPFileIO::TruncateHere(fd));
    EXPECT_EQ(2, lseek(fd, 0, SEEK_CUR));
    struct stat info;
    ASSERT_EQ(0, fstat(fd, &info));
    EXPECT_EQ(2, info.st_size);
    ASSERT_TRUE(TVPFileIO::WriteAll(fd, "XYZ", 3));
    close(fd);
    EXPECT_EQ("abXYZ", ReadFile(path));
}

TEST_F(Files, UpdateMustNotCreateOrTruncate) {
    std::string path = root + "/save.dat";
    EXPECT_LT(TVPFileIO::Open(path.c_str(), 3), 0);
    ASSERT_TRUE(TVPFileIO::WriteAtomic(path, "saved", 5));
    int fd = TVPFileIO::Open(path.c_str(), 3);
    ASSERT_GE(fd, 0);
    char buffer[5];
    ASSERT_EQ(5, TVPFileIO::Read(fd, buffer, 5));
    close(fd);
    EXPECT_EQ("saved", std::string(buffer, sizeof(buffer)));
}

TEST_F(Files, ErrorsAreReportedAtWriteTime) {
    int fd = open("/dev/full", O_WRONLY);
    ASSERT_GE(fd, 0);
    EXPECT_FALSE(TVPFileIO::WriteAll(fd, "save", 4));
    close(fd);
    EXPECT_FALSE(TVPFileIO::WriteAll(-1, "save", 4));
    EXPECT_FALSE(TVPFileIO::TruncateHere(-1));
    EXPECT_FALSE(TVPFileIO::WriteAtomic(root + "/missing/save", "save", 4));
}

TEST_F(Files, AtomicReplacementPreservesOldFileUntilRename) {
    std::string path = root + "/save.dat";
    ASSERT_TRUE(TVPFileIO::WriteAtomic(path, "original", 8));
    ASSERT_TRUE(TVPFileIO::WriteAtomic(path, "new", 3));
    EXPECT_EQ("new", ReadFile(path));
    ASSERT_EQ(0, mkdir((root + "/directory").c_str(), 0700));
    EXPECT_FALSE(TVPFileIO::WriteAtomic(root + "/directory", "bad", 3));
    struct stat info;
    ASSERT_EQ(0, stat((root + "/directory").c_str(), &info));
    EXPECT_TRUE(S_ISDIR(info.st_mode));
}

TEST_F(Files, ArchiveExtractsNestedAndUnicodeNames) {
    std::string destination = root + "/game";
    int fd = TVPArchivePath::OpenFile(destination, "scenario/日本語.tjs");
    ASSERT_GE(fd, 0);
    ASSERT_TRUE(TVPFileIO::WriteAll(fd, "fixture", 7));
    close(fd);
    EXPECT_EQ("fixture", ReadFile(destination + "/scenario/日本語.tjs"));
    fd = TVPArchivePath::OpenFile(destination, "scenario\\windows.tjs");
    ASSERT_GE(fd, 0);
    close(fd);
}

TEST_F(Files, ArchiveRejectsTraversalAbsolutePathsAndNul) {
    const std::string bad[] = {"../save", "a/../../save", "/tmp/save", "C:\\save", "\\\\host\\save",
        "a\\..\\save", "a//save", "./save", "", std::string("safe\0../save", 13)};
    for (const auto &name : bad) {
        EXPECT_LT(TVPArchivePath::OpenFile(root + "/game", name), 0) << name;
    }
    EXPECT_NE(0, access((root + "/save").c_str(), F_OK));
}

TEST_F(Files, ArchiveRejectsSymlinkAndHardLinkDestinations) {
    std::string game = root + "/game", outside = root + "/outside";
    ASSERT_EQ(0, mkdir(game.c_str(), 0700));
    ASSERT_EQ(0, mkdir(outside.c_str(), 0700));
    ASSERT_TRUE(TVPFileIO::WriteAtomic(outside + "/save", "keep", 4));
    ASSERT_EQ(0, symlink(outside.c_str(), (game + "/directory").c_str()));
    ASSERT_EQ(0, symlink((outside + "/save").c_str(), (game + "/symlink").c_str()));
    ASSERT_EQ(0, link((outside + "/save").c_str(), (game + "/hardlink").c_str()));
    EXPECT_LT(TVPArchivePath::OpenFile(game, "directory/save"), 0);
    EXPECT_LT(TVPArchivePath::OpenFile(game, "symlink"), 0);
    EXPECT_LT(TVPArchivePath::OpenFile(game, "hardlink"), 0);
    EXPECT_EQ("keep", ReadFile(outside + "/save"));
}

TEST(ArchiveWorker, DestroyWithoutStartingDoesNotWait) {
    TVPStartOnceWorker worker([] { FAIL() << "Unstarted task was executed"; });
}

TEST(ArchiveWorker, ConcurrentStartRunsExactlyOnceAndJoins) {
    for (int iteration = 0; iteration < 200; ++iteration) {
        std::atomic<int> calls(0);
        {
            TVPStartOnceWorker worker([&] { ++calls; });
            std::thread first([&] { worker.Start(); });
            std::thread second([&] { worker.Start(); });
            first.join(); second.join();
        }
        ASSERT_EQ(1, calls.load()) << iteration;
    }
}
