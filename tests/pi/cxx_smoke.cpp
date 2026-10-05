// SPDX-License-Identifier: MIT (Copyright (c) 2026 Anurodh Pokharel)
// Runs on the Pi against the LLVM 20 libc++ (scripts/build_libcxx.sh): what WebKit's WTF/JSC lean on.
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <exception>
#include <expected>
#include <filesystem>
#include <fstream>
#include <functional>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <ranges>
#include <shared_mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
static int fails;
#define CHECK(c) do { bool ok_ = (c); std::printf("%s %s\n", ok_ ? "PASS" : "FAIL", #c); std::fflush(stdout); if (!ok_) fails++; } while (0)
struct Base { virtual ~Base() = default; }; struct Derived : Base {};
static std::atomic<int> dtor_runs;
struct TlsDtor { ~TlsDtor() { dtor_runs++; } };
static thread_local TlsDtor tls_obj;
static std::expected<int, std::string> parse(int v) { if (v < 0) return std::unexpected("neg"); return v * 2; }
int main() {
    std::string s = "libc++"; s += " 20"; CHECK(s == "libc++ 20");
    std::vector<int> v{3, 1, 2}; std::ranges::sort(v); CHECK((v == std::vector<int>{1, 2, 3}));
    std::map<std::string, int> m{{"a", 1}}; CHECK(m.at("a") == 1);
    bool caught = false; try { throw std::runtime_error("boom"); } catch (const std::exception &e) { caught = std::string(e.what()) == "boom"; } CHECK(caught);
    caught = false; try { std::vector<int>().at(5); } catch (const std::out_of_range &) { caught = true; } CHECK(caught);
    Base *b = new Derived; CHECK(dynamic_cast<Derived *>(b) != nullptr); delete b;
    std::stringstream ss; ss << 42 << ' ' << 1.5; int i; double d; ss >> i >> d; CHECK(i == 42 && d == 1.5);
    CHECK(parse(4).value() == 8 && !parse(-1).has_value());
    std::mutex mu; std::condition_variable cv; bool ready = false; std::thread t([&] { std::lock_guard l(mu); ready = true; cv.notify_one(); });
    { std::unique_lock l(mu); CHECK(cv.wait_for(l, std::chrono::seconds(5), [&] { return ready; })); } t.join();
    auto f = std::async(std::launch::async, [] { return 7; }); CHECK(f.get() == 7);
    std::once_flag of; int n = 0; std::call_once(of, [&] { n++; }); std::call_once(of, [&] { n++; }); CHECK(n == 1);
    std::shared_mutex sm; { std::shared_lock a(sm); std::shared_lock c(sm); } CHECK(true);
    { std::thread w([] { (void)&tls_obj; }); w.join(); } CHECK(dtor_runs == 1);
    std::shared_ptr<int> up = std::make_shared<int>(5); std::function<int()> fn = [p = up] { return *p; }; CHECK(fn() == 5);
    CHECK(std::chrono::steady_clock::now().time_since_epoch().count() > 0);
    namespace fs = std::filesystem;
    fs::path dir = fs::temp_directory_path() / "flo_fs_smoke"; std::error_code ec; fs::remove_all(dir, ec);
    CHECK(fs::create_directories(dir / "a" / "b"));
    { std::ofstream(dir / "a" / "f.txt") << "hello"; }
    CHECK(fs::file_size(dir / "a" / "f.txt") == 5);
    fs::create_symlink("f.txt", dir / "a" / "link"); CHECK(fs::is_symlink(dir / "a" / "link") && fs::read_symlink(dir / "a" / "link") == "f.txt");
    CHECK(fs::canonical(dir / "a" / "b" / ".." / "link") == fs::canonical(dir / "a" / "f.txt"));
    fs::resize_file(dir / "a" / "f.txt", 2); CHECK(fs::file_size(dir / "a" / "f.txt") == 2);
    fs::permissions(dir / "a" / "f.txt", fs::perms::owner_read | fs::perms::owner_write); CHECK((fs::status(dir / "a" / "f.txt").permissions() & fs::perms::owner_read) != fs::perms::none);
    fs::last_write_time(dir / "a" / "f.txt", fs::file_time_type::clock::now() - std::chrono::hours(1)); CHECK(true);
    int n2 = 0; for (auto &e : fs::recursive_directory_iterator(dir)) { (void)e; n2++; } CHECK(n2 == 4);
    CHECK(fs::remove_all(dir) == 5);
    std::printf(fails ? "RESULT FAIL %d\n" : "RESULT OK\n", fails); return fails != 0;
}
