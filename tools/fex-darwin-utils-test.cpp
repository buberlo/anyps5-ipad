// Tests the actual Darwin implementations; this allocator adapter is test-only.
#include <FEXCore/Utils/FileUtils.h>
#include <FEXHeaderUtils/Syscalls.h>
#include <array>
#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <iostream>

namespace FEXCore::Allocator {
void* aligned_alloc(size_t alignment, size_t size) {
  void* ptr {};
  return ::posix_memalign(&ptr, std::max(alignment, sizeof(void*)), size) ? nullptr : ptr;
}
void aligned_free(void* ptr) { std::free(ptr); }
}
namespace fextl {
[[noreturn]] void ReportAllocationFailure(size_t, size_t) { std::abort(); }
}

int main() {
  namespace fs = std::filesystem;
  char temp[] = "/tmp/fex-darwin-utils-XXXXXX";
  const char* created = mkdtemp(temp);
  assert(created);
  const fs::path root(created), tree = root / "tree", outside = root / "outside";
  fs::create_directories(tree / "nested" / "deep");
  fs::create_directories(outside);
  std::ofstream(tree / "data") << "owned";
  std::ofstream(tree / "nested" / "deep" / "data") << "nested";
  std::ofstream(outside / "keep") << "must survive";
  fs::create_directory_symlink(outside, tree / "external-link");
  fs::create_symlink(root / "missing", tree / "dangling-link");
  std::map<std::string, bool> entries;
  FEXCore::FileUtils::WalkDirectory(tree.string(), [](std::string_view name, bool dir, const void* data) {
    auto& found = *static_cast<std::map<std::string, bool>*>(const_cast<void*>(data));
    assert(found.emplace(name, dir).second);
  }, &entries);
  assert(entries.size() == 4 && entries.at("nested") && !entries.at("data"));
  assert(!entries.at("external-link") && !entries.at("dangling-link"));
  fs::create_directory_symlink(tree, root / "root-link");
  assert(!FEXCore::FileUtils::RecursiveRemoveDirectory(fextl::string((root / "root-link").string().c_str())));
  assert(fs::exists(tree / "data"));
  assert(FEXCore::FileUtils::RecursiveRemoveDirectory(fextl::string(tree.string().c_str())));
  assert(!fs::exists(tree) && fs::exists(outside / "keep"));
  assert(FEXCore::FileUtils::RecursiveRemoveDirectory(fextl::string(tree.string().c_str())));
  std::map<std::string, bool> absent;
  FEXCore::FileUtils::WalkDirectory(tree.string(), [](std::string_view n, bool d, const void* data) {
    static_cast<std::map<std::string, bool>*>(const_cast<void*>(data))->emplace(n, d);
  }, &absent);
  assert(absent.empty());
  // Exercise real entropy twice, including the API's zero-length/error contract.
  std::array<unsigned char, 256> first {}, second {};
  assert(FHU::Syscalls::getrandom(first.data(), first.size(), 0) == first.size());
  assert(FHU::Syscalls::getrandom(second.data(), second.size(), 0) == second.size());
  assert(first != second);
  assert(FHU::Syscalls::getrandom(nullptr, 0, 0) == 0);
  errno = 0;
  assert(FHU::Syscalls::getrandom(first.data(), first.size(), 1) == -1 && errno == ENOTSUP);
  errno = 0;
  assert(FHU::Syscalls::getrandom(nullptr, static_cast<size_t>(SSIZE_MAX) + 1, 0) == -1 && errno == EINVAL);
  fs::remove_all(root);
  std::cout << "Darwin directory traversal/removal and entropy contracts passed\n";
}
