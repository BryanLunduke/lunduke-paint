// SPDX-License-Identifier: GPL-3.0-or-later
// The release binary must not carry a workspace path or a test-only hook.
// Test binaries may; this checks the lunduke-paint executable only.

#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

namespace {

int errors = 0;

void expect(bool cond, const char* msg) {
  if (!cond) {
    std::fprintf(stderr, "test_release_binary: %s\n", msg);
    ++errors;
  }
}

bool contains(const std::string& data, const char* needle) {
  return data.find(needle) != std::string::npos;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2 || argv[1] == nullptr || argv[1][0] == '\0') {
    std::fprintf(stderr, "test_release_binary: pass the lunduke-paint binary path\n");
    return 1;
  }
  std::ifstream in(argv[1], std::ios::binary);
  expect(static_cast<bool>(in), "could not open the production binary");
  if (!in) {
    return 1;
  }
  const std::string data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  expect(!data.empty(), "production binary is empty");

  const char* forbidden[] = {
      "/workspace",
      "LUNDUKEPAINT_REQUIRE_DISPLAY",
      "REVIEW_R3_SOURCE",
      "REVIEW_R6_SOURCE",
      "REVIEW_R7_SOURCE",
  };
  for (const char* needle : forbidden) {
    if (contains(data, needle)) {
      std::fprintf(stderr, "test_release_binary: production binary contains '%s'\n", needle);
      ++errors;
    }
  }

  if (errors != 0) {
    std::fprintf(stderr, "test_release_binary: %d failure(s)\n", errors);
    return 1;
  }
  std::printf("test_release_binary: ok\n");
  return 0;
}
