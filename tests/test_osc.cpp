// OSC codec round-trip and robustness tests.
#include "osc.h"
#include "test_util.h"

using namespace x32;

static void TestQueryEncoding() {
  auto pkt = OscEncodeQuery("/ch/01/mix/fader");
  // Address (16 incl NUL, padded to 16) + ",\0\0\0" (4) = 20 bytes.
  CHECK_EQ(pkt.size() % 4, size_t(0));
  OscMessage m;
  CHECK(OscDecode(pkt, &m));
  CHECK_EQ(m.address, std::string("/ch/01/mix/fader"));
  CHECK(m.args.empty());
}

static void TestDecodeFloat() {
  // Build a ,f message by hand: "/x" + ",f" + float 0.75 big-endian.
  std::vector<uint8_t> buf;
  const char* addr = "/ch/01/mix/fader";
  for (const char* p = addr; *p; ++p) buf.push_back((uint8_t)*p);
  buf.push_back(0);
  while (buf.size() % 4) buf.push_back(0);
  const char* tags = ",f";
  for (const char* p = tags; *p; ++p) buf.push_back((uint8_t)*p);
  buf.push_back(0);
  while (buf.size() % 4) buf.push_back(0);
  float f = 0.75f;
  uint32_t bits;
  __builtin_memcpy(&bits, &f, 4);
  buf.push_back((bits >> 24) & 0xff);
  buf.push_back((bits >> 16) & 0xff);
  buf.push_back((bits >> 8) & 0xff);
  buf.push_back(bits & 0xff);

  OscMessage m;
  CHECK(OscDecode(buf, &m));
  CHECK_EQ(m.address, std::string("/ch/01/mix/fader"));
  CHECK_EQ(m.args.size(), size_t(1));
  CHECK(m.args[0].type == OscArg::Type::Float);
  CHECK_NEAR(m.args[0].f, 0.75, 1e-6);
}

static void TestDecodeInt() {
  std::vector<uint8_t> buf;
  const char* addr = "/ch/01/mix/on";
  for (const char* p = addr; *p; ++p) buf.push_back((uint8_t)*p);
  buf.push_back(0);
  while (buf.size() % 4) buf.push_back(0);
  const char* tags = ",i";
  for (const char* p = tags; *p; ++p) buf.push_back((uint8_t)*p);
  buf.push_back(0);
  while (buf.size() % 4) buf.push_back(0);
  buf.push_back(0);
  buf.push_back(0);
  buf.push_back(0);
  buf.push_back(1);  // int 1

  OscMessage m;
  CHECK(OscDecode(buf, &m));
  CHECK_EQ(m.args.size(), size_t(1));
  CHECK(m.args[0].type == OscArg::Type::Int);
  CHECK_EQ(m.args[0].i, 1);
}

static void TestDecodeStrings() {
  // /xinfo reply ,ssss
  std::vector<uint8_t> buf;
  const char* addr = "/xinfo";
  for (const char* p = addr; *p; ++p) buf.push_back((uint8_t)*p);
  buf.push_back(0);
  while (buf.size() % 4) buf.push_back(0);
  const char* tags = ",ssss";
  for (const char* p = tags; *p; ++p) buf.push_back((uint8_t)*p);
  buf.push_back(0);
  while (buf.size() % 4) buf.push_back(0);
  const char* strs[4] = {"192.168.1.5", "x32", "X32", "4.06"};
  for (int k = 0; k < 4; ++k) {
    for (const char* p = strs[k]; *p; ++p) buf.push_back((uint8_t)*p);
    buf.push_back(0);
    while (buf.size() % 4) buf.push_back(0);
  }

  OscMessage m;
  CHECK(OscDecode(buf, &m));
  CHECK_EQ(m.args.size(), size_t(4));
  CHECK_EQ(m.args[0].s, std::string("192.168.1.5"));
  CHECK_EQ(m.args[3].s, std::string("4.06"));
}

static void TestRejectGarbage() {
  OscMessage m;
  // Too short.
  CHECK(!OscDecode((const uint8_t*)"ab", 2, &m));
  // Bundle marker.
  CHECK(!OscDecode((const uint8_t*)"#bun", 4, &m));
  // Does not start with '/'.
  CHECK(!OscDecode((const uint8_t*)"xxxx", 4, &m));
}

static void TestFuzzNoCrash() {
  // Feed a range of truncations of a valid packet; must never crash and must
  // return cleanly (true or false).
  auto full = OscEncodeQuery("/bus/16/mix/on");
  for (size_t n = 0; n <= full.size(); ++n) {
    OscMessage m;
    (void)OscDecode(full.data(), n, &m);
  }
  CHECK(true);
}

static void RunTests() {
  TestQueryEncoding();
  TestDecodeFloat();
  TestDecodeInt();
  TestDecodeStrings();
  TestRejectGarbage();
  TestFuzzNoCrash();
}

TEST_MAIN(RunTests)
