// Maillot fader curve fixtures and dB->D_VOL conversion tests.
#include "test_util.h"
#include "x32_addresses.h"

using namespace x32;

static void TestUnityAndExtremes() {
  // f = 0.75 -> 0 dB unity -> D_VOL 1.0
  CHECK_NEAR(FaderFloatToDb(0.75f), 0.0, 1e-6);
  CHECK_NEAR(FaderFloatToVol(0.75f), 1.0, 1e-6);

  // f = 1.0 -> +10 dB (40*1 - 30)
  CHECK_NEAR(FaderFloatToDb(1.0f), 10.0, 1e-6);

  // f = 0 -> -inf -> D_VOL exactly 0.0
  CHECK_NEAR(FaderFloatToVol(0.0f), 0.0, 0.0);
  CHECK(FaderFloatToDb(0.0f) <= kMinusInfDb);
}

static void TestPiecewiseBoundaries() {
  // Segment breakpoints (values from the plan's verified curve):
  //   f>=0.5   : 40f-30   -> f=0.5  -> -10 dB
  //   f>=0.25  : 80f-50   -> f=0.25 -> -30 dB ; f just below 0.5 -> 40*.5-... check continuity
  //   f>=0.0625: 160f-70  -> f=0.0625 -> -60 dB
  //   else     : 480f-90
  CHECK_NEAR(FaderFloatToDb(0.5f), -10.0, 1e-6);   // 40*0.5 - 30
  CHECK_NEAR(FaderFloatToDb(0.25f), -30.0, 1e-6);  // 80*0.25 - 50
  CHECK_NEAR(FaderFloatToDb(0.0625f), -60.0, 1e-6);// 160*0.0625 - 70

  // Continuity: just below a breakpoint uses the next segment and should be
  // close to the breakpoint value. Compute the expected value from the same
  // float input so this is not a float-vs-double rounding comparison.
  CHECK_NEAR(FaderFloatToDb(0.4999f), 80.0 * (double)0.4999f - 50.0, 1e-6);
  CHECK_NEAR(FaderFloatToDb(0.2499f), 160.0 * (double)0.2499f - 70.0, 1e-6);
}

static void TestDbToVol() {
  CHECK_NEAR(DbToVol(0.0), 1.0, 1e-9);
  CHECK_NEAR(DbToVol(-6.0206), 0.5, 1e-4);   // -6.02 dB ~ half amplitude
  CHECK_NEAR(DbToVol(20.0), 10.0, 1e-6);
  CHECK_NEAR(DbToVol(kMinusInfDb), 0.0, 0.0);
}

static void TestMonotonic() {
  double prev = -1e9;
  for (int k = 0; k <= 1000; ++k) {
    float f = k / 1000.0f;
    double v = FaderFloatToVol(f);
    CHECK(v >= prev - 1e-9);  // non-decreasing
    prev = v;
  }
}

static void RunTests() {
  TestUnityAndExtremes();
  TestPiecewiseBoundaries();
  TestDbToVol();
  TestMonotonic();
}

TEST_MAIN(RunTests)
