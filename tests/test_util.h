// Minimal dependency-free test harness. Each test file defines RunTests() by
// using the CHECK/CHECK_EQ macros and returns the failure count from main().
#ifndef X32MIRROR_TEST_UTIL_H_
#define X32MIRROR_TEST_UTIL_H_

#include <cmath>
#include <cstdio>
#include <string>

namespace x32test {
inline int& failures() {
  static int f = 0;
  return f;
}
inline int& checks() {
  static int c = 0;
  return c;
}
}  // namespace x32test

#define CHECK(cond)                                                       \
  do {                                                                    \
    ++x32test::checks();                                                  \
    if (!(cond)) {                                                        \
      ++x32test::failures();                                              \
      std::printf("FAIL %s:%d  CHECK(%s)\n", __FILE__, __LINE__, #cond);  \
    }                                                                     \
  } while (0)

#define CHECK_EQ(a, b)                                                        \
  do {                                                                        \
    ++x32test::checks();                                                      \
    if (!((a) == (b))) {                                                      \
      ++x32test::failures();                                                  \
      std::printf("FAIL %s:%d  CHECK_EQ(%s, %s)\n", __FILE__, __LINE__, #a,   \
                  #b);                                                        \
    }                                                                         \
  } while (0)

#define CHECK_NEAR(a, b, eps)                                               \
  do {                                                                      \
    ++x32test::checks();                                                    \
    double _va = (a), _vb = (b);                                            \
    if (std::fabs(_va - _vb) > (eps)) {                                     \
      ++x32test::failures();                                                \
      std::printf("FAIL %s:%d  CHECK_NEAR(%s=%.6g, %s=%.6g, eps=%g)\n",     \
                  __FILE__, __LINE__, #a, _va, #b, _vb, (double)(eps));     \
    }                                                                       \
  } while (0)

#define TEST_MAIN(fn)                                                     \
  int main() {                                                            \
    fn();                                                                 \
    std::printf("%s: %d checks, %d failures\n", __FILE__,                 \
                x32test::checks(), x32test::failures());                  \
    return x32test::failures() == 0 ? 0 : 1;                              \
  }

#endif  // X32MIRROR_TEST_UTIL_H_
