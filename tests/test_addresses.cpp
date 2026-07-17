// Address build/parse tests, including the DCA single-digit/no-/mix quirk.
#include "test_util.h"
#include "x32_addresses.h"

using namespace x32;

static void TestBuild() {
  CHECK_EQ(AddressFor({StripType::CH, 1}, Param::Fader),
           std::string("/ch/01/mix/fader"));
  CHECK_EQ(AddressFor({StripType::CH, 32}, Param::Mute),
           std::string("/ch/32/mix/on"));
  CHECK_EQ(AddressFor({StripType::BUS, 16}, Param::Fader),
           std::string("/bus/16/mix/fader"));
  // DCA: single digit, no /mix segment.
  CHECK_EQ(AddressFor({StripType::DCA, 3}, Param::Fader),
           std::string("/dca/3/fader"));
  CHECK_EQ(AddressFor({StripType::DCA, 8}, Param::Mute),
           std::string("/dca/8/on"));
}

static void TestBuildOutOfRange() {
  CHECK(AddressFor({StripType::CH, 0}, Param::Fader).empty());
  CHECK(AddressFor({StripType::CH, 33}, Param::Fader).empty());
  CHECK(AddressFor({StripType::DCA, 9}, Param::Fader).empty());
  CHECK(AddressFor({StripType::BUS, 17}, Param::Mute).empty());
}

static void TestParse() {
  StripId id;
  Param p;

  CHECK(ParseAddress("/ch/07/mix/fader", &id, &p));
  CHECK(id.type == StripType::CH);
  CHECK_EQ(id.index, 7);
  CHECK(p == Param::Fader);

  CHECK(ParseAddress("/ch/32/mix/on", &id, &p));
  CHECK(id.type == StripType::CH);
  CHECK_EQ(id.index, 32);
  CHECK(p == Param::Mute);

  CHECK(ParseAddress("/dca/3/fader", &id, &p));
  CHECK(id.type == StripType::DCA);
  CHECK_EQ(id.index, 3);
  CHECK(p == Param::Fader);

  CHECK(ParseAddress("/dca/8/on", &id, &p));
  CHECK(id.type == StripType::DCA);
  CHECK(p == Param::Mute);

  CHECK(ParseAddress("/bus/16/mix/fader", &id, &p));
  CHECK(id.type == StripType::BUS);
  CHECK_EQ(id.index, 16);
}

static void TestParseRejects() {
  StripId id;
  Param p;
  // DCA must NOT have /mix.
  CHECK(!ParseAddress("/dca/3/mix/fader", &id, &p));
  // CH must have /mix.
  CHECK(!ParseAddress("/ch/07/fader", &id, &p));
  // Out of range index.
  CHECK(!ParseAddress("/ch/33/mix/fader", &id, &p));
  CHECK(!ParseAddress("/dca/9/fader", &id, &p));
  // Unrelated addresses.
  CHECK(!ParseAddress("/info", &id, &p));
  CHECK(!ParseAddress("/xinfo", &id, &p));
  CHECK(!ParseAddress("/ch/07/mix/pan", &id, &p));
  CHECK(!ParseAddress("", &id, &p));
}

static void TestRoundTrip() {
  StripType fams[] = {StripType::CH, StripType::BUS, StripType::DCA};
  for (StripType f : fams) {
    for (int i = 1; i <= StripCount(f); ++i) {
      for (Param pr : {Param::Fader, Param::Mute}) {
        std::string a = AddressFor({f, i}, pr);
        CHECK(!a.empty());
        StripId id;
        Param p;
        CHECK(ParseAddress(a, &id, &p));
        CHECK(id.type == f);
        CHECK_EQ(id.index, i);
        CHECK(p == pr);
      }
    }
  }
}

static void RunTests() {
  TestBuild();
  TestBuildOutOfRange();
  TestParse();
  TestParseRejects();
  TestRoundTrip();
}

TEST_MAIN(RunTests)
