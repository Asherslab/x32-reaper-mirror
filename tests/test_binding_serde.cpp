// Binding value serialization round-trip and version/robustness tests.
#include "binding_serde.h"
#include "test_util.h"

using namespace x32;

static void TestRoundTrip() {
  BindingData b;
  b.strip = {StripType::CH, 7};
  b.enabled = true;
  b.mirror_mute = true;
  b.mirror_fader = false;
  std::string v = SerializeBinding(b);
  CHECK_EQ(v, std::string("v1|CH|7|1|1|0"));

  BindingData out;
  CHECK(ParseBinding("{GUID-1}", v, &out));
  CHECK_EQ(out.guid, std::string("{GUID-1}"));
  CHECK(out.strip.type == StripType::CH);
  CHECK_EQ(out.strip.index, 7);
  CHECK(out.enabled);
  CHECK(out.mirror_mute);
  CHECK(!out.mirror_fader);
}

static void TestDcaAndBus() {
  BindingData b;
  b.strip = {StripType::DCA, 3};
  b.enabled = false;
  b.mirror_mute = false;
  b.mirror_fader = true;
  std::string v = SerializeBinding(b);
  CHECK_EQ(v, std::string("v1|DCA|3|0|0|1"));
  BindingData out;
  CHECK(ParseBinding("{g}", v, &out));
  CHECK(out.strip.type == StripType::DCA);
  CHECK_EQ(out.strip.index, 3);
  CHECK(!out.enabled);
  CHECK(out.mirror_fader);
}

static void TestRejects() {
  BindingData out;
  CHECK(!ParseBinding("{g}", "", &out));
  CHECK(!ParseBinding("{g}", "v2|CH|1|1|1|1", &out));       // unknown version
  CHECK(!ParseBinding("{g}", "v1|CH|1|1|1", &out));         // too few fields
  CHECK(!ParseBinding("{g}", "v1|ZZ|1|1|1|1", &out));       // unknown family
  CHECK(!ParseBinding("{g}", "v1|CH|99|1|1|1", &out));      // out of range
  CHECK(!ParseBinding("{g}", "v1|DCA|9|1|1|1", &out));      // DCA out of range
}

static void RunTests() {
  TestRoundTrip();
  TestDcaAndBus();
  TestRejects();
}

TEST_MAIN(RunTests)
