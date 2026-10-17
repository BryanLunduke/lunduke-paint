// SPDX-License-Identifier: GPL-3.0-or-later
// Round 15, without a display.
// P-1: saving to a symlinked image replaces the real file in its own folder
//      and keeps the link; a dangling link is refused with a Save As hint.
// P-3: stale ".<name>.tmp.<pid>.<hex>" files from a crashed save are removed
//      at the next save of that name when the writer PID is gone and the file
//      is old; live, young, foreign-named and other-name temps are kept.

#include "io/image_io.hpp"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <fcntl.h>
#include <fstream>
#include <iterator>
#include <string>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

using namespace lundukepaint;

namespace {
int g_failures = 0;
void check(bool ok, const char* what) {
  std::printf("%s: %s\n", ok ? "PASS" : "FAIL", what);
  if (!ok) {
    ++g_failures;
  }
}
std::string slurp(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}
bool is_link(const std::string& path) {
  struct stat st;
  return lstat(path.c_str(), &st) == 0 && S_ISLNK(st.st_mode);
}
void touch_old(const std::string& path, int age_seconds) {
  std::ofstream(path) << "partial";
  struct timeval tv[2];
  gettimeofday(&tv[0], nullptr);
  tv[0].tv_sec -= age_seconds;
  tv[1] = tv[0];
  utimes(path.c_str(), tv);
}
bool exists(const std::string& path) {
  struct stat st;
  return lstat(path.c_str(), &st) == 0;
}
pid_t dead_pid() {
  const pid_t child = fork();
  if (child == 0) {
    _exit(0);
  }
  int status = 0;
  waitpid(child, &status, 0);
  return child;  // reaped: no longer running
}
int count_temps(const std::string& dir) {
  int n = 0;
  if (DIR* d = opendir(dir.c_str())) {
    while (dirent* e = readdir(d)) {
      if (std::strstr(e->d_name, ".tmp.") != nullptr) ++n;
    }
    closedir(d);
  }
  return n;
}
}  // namespace

int main() {
  char tmpl[] = "/tmp/lunduke-paint-r15-XXXXXX";
  if (mkdtemp(tmpl) == nullptr) {
    return 1;
  }
  const std::string dir = tmpl;
  const std::string real_dir = dir + "/real";
  const std::string link_dir = dir + "/links";
  mkdir(real_dir.c_str(), 0700);
  mkdir(link_dir.c_str(), 0700);

  // ---- P-1: symlinked image (the probe's case, relative link) ----
  {
    const std::string real = link_dir + "/real.png";
    std::ofstream(real) << "old";
    chmod(real.c_str(), 0640);
    const std::string link = link_dir + "/link.png";
    symlink("real.png", link.c_str());
    AtomicFile f;
    std::string err;
    const bool ok = atomic_create(link, f, err);
    std::printf("probe: save via symlink ok=%d err=%s\n", ok ? 1 : 0, err.c_str());
    check(ok, "atomic_create through a symlink succeeds");
    if (ok) {
      check(f.dest_path == real, "destination is the link's real file");
      const char data[] = "new-bytes";
      check(write(f.fd, data, sizeof(data) - 1) == static_cast<ssize_t>(sizeof(data) - 1), "write");
      check(atomic_commit(f, err), "commit");
    }
    check(is_link(link), "link is still a symlink after save");
    check(slurp(real) == "new-bytes", "real file has the new bytes");
    check(slurp(link) == "new-bytes", "reading through the link gives the new bytes");
    struct stat st;
    check(stat(real.c_str(), &st) == 0 && (st.st_mode & 0777) == 0640, "real file keeps mode 0640");
    check(count_temps(link_dir) == 0, "no temp left in the link's folder");
  }
  // Link in one folder, real file in another: temp goes next to the real file.
  {
    const std::string real = real_dir + "/target.png";
    std::ofstream(real) << "x";
    const std::string link = link_dir + "/cross.png";
    symlink(real.c_str(), link.c_str());
    AtomicFile f;
    std::string err;
    const bool ok = atomic_create(link, f, err);
    check(ok, "cross-folder link: create");
    if (ok) {
      check(f.tmp_path.compare(0, real_dir.size() + 1, real_dir + "/") == 0,
            "cross-folder link: temp is in the real file's folder");
      check(write(f.fd, "yz", 2) == 2, "cross-folder link: write");
      check(atomic_commit(f, err), "cross-folder link: commit");
    }
    check(is_link(link) && slurp(real) == "yz", "cross-folder link kept and target replaced");
  }
  // Save through the full image API (what Ctrl+S runs for a PNG).
  {
    const std::string real = real_dir + "/api.png";
    std::ofstream(real) << "placeholder";
    const std::string link = link_dir + "/api-link.png";
    symlink(real.c_str(), link.c_str());
    std::vector<std::uint8_t> px(4 * 4 * 4, 200);
    std::string err;
    const bool ok = save_flat_image(link, ImageFormat::Png, px.data(), 4, 4, 16, 90, err);
    std::printf("save_flat_image via link: ok=%d err=%s\n", ok ? 1 : 0, err.c_str());
    check(ok, "save_flat_image through a symlink succeeds");
    check(is_link(link), "save_flat_image keeps the link");
    const LoadedImage back = load_flat_image(real);
    check(back.ok() && back.width == 4 && back.height == 4, "real file is the saved 4x4 PNG");
  }
  // Dangling link: refused, with a Save As hint, nothing created.
  {
    const std::string link = link_dir + "/dangling.png";
    symlink("missing/nowhere.png", link.c_str());
    AtomicFile f;
    std::string err;
    const bool ok = atomic_create(link, f, err);
    std::printf("dangling: ok=%d err=%s\n", ok ? 1 : 0, err.c_str());
    check(!ok, "dangling link refused");
    check(err.find("Save As") != std::string::npos, "dangling link message suggests Save As");
    check(is_link(link), "dangling link left alone");
  }
  // A link to a directory is still not a regular file.
  {
    const std::string link = link_dir + "/dirlink.png";
    symlink(real_dir.c_str(), link.c_str());
    AtomicFile f;
    std::string err;
    check(!atomic_create(link, f, err) && err.find("not a regular file") != std::string::npos,
          "link to a folder refused");
  }

  // ---- P-3: stale temp cleanup ----
  {
    pid_t pid = 0;
    check(is_stale_save_temp_name(".pic.png.tmp.1234.0123456789abcdef", "pic.png", &pid) && pid == 1234,
          "temp name parsed");
    check(!is_stale_save_temp_name(".pic.png.tmp.1234.0123456789abcde", "pic.png", nullptr),
          "short random part rejected");
    check(!is_stale_save_temp_name(".pic.png.tmp.12x4.0123456789abcdef", "pic.png", nullptr),
          "non-numeric pid rejected");
    check(!is_stale_save_temp_name(".other.png.tmp.1234.0123456789abcdef", "pic.png", nullptr),
          "other base name rejected");
    check(!is_stale_save_temp_name("pic.png.tmp.1234.0123456789abcdef", "pic.png", nullptr),
          "unhidden name rejected");

    const std::string d = dir + "/stale";
    mkdir(d.c_str(), 0700);
    const pid_t gone = dead_pid();
    char name[128];
    std::snprintf(name, sizeof(name), "/.pic.png.tmp.%d.00112233445566aa", static_cast<int>(gone));
    const std::string stale = d + name;
    touch_old(stale, 3600);
    std::snprintf(name, sizeof(name), "/.pic.png.tmp.%d.00112233445566bb", static_cast<int>(gone));
    const std::string young = d + name;
    touch_old(young, 5);
    std::snprintf(name, sizeof(name), "/.pic.png.tmp.%d.00112233445566cc", static_cast<int>(getppid()));
    const std::string live = d + name;
    touch_old(live, 3600);
    std::snprintf(name, sizeof(name), "/.other.png.tmp.%d.00112233445566dd", static_cast<int>(gone));
    const std::string other = d + name;
    touch_old(other, 3600);
    const std::string user_file = d + "/.pic.png.tmp.notes";
    touch_old(user_file, 3600);
    // A stale-named symlink is never followed or removed.
    std::snprintf(name, sizeof(name), "/.pic.png.tmp.%d.00112233445566ee", static_cast<int>(gone));
    const std::string stale_link = d + name;
    symlink(user_file.c_str(), stale_link.c_str());

    const std::string pic = d + "/pic.png";
    std::vector<std::uint8_t> px(2 * 2 * 4, 9);
    std::string err;
    check(save_flat_image(pic, ImageFormat::Png, px.data(), 2, 2, 8, 90, err), "save pic.png");
    check(!exists(stale), "dead-PID old temp removed at save");
    check(exists(young), "young temp kept (may be a save in progress elsewhere)");
    check(exists(live), "temp of a running PID kept");
    check(exists(other), "another image's temp kept");
    check(exists(user_file), "user file with a similar name kept");
    check(is_link(stale_link), "temp-named symlink kept");
    check(remove_stale_save_temps(d, "pic.png", 0) == 1, "with min age 0 the young one goes too");
    check(!exists(young), "young temp removed when old enough");
  }

  const std::string cmd = "rm -rf '" + dir + "'";
  if (std::system(cmd.c_str()) != 0) {
    std::printf("cleanup failed for %s\n", dir.c_str());
  }
  std::printf("%d failure(s)\n", g_failures);
  return g_failures == 0 ? 0 : 1;
}
