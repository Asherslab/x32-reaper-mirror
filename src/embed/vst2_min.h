// X32 → REAPER Mirror — embedded-FX companion
// Clean-room minimal VST2 ABI (vestige-style). Only the fields and opcodes the
// companion actually uses are declared. No Steinberg SDK headers are involved.
#ifndef X32MIRROR_VST2_MIN_H_
#define X32MIRROR_VST2_MIN_H_

#include <cstdint>

extern "C" {

struct AEffect;

typedef intptr_t (*audioMasterCallback)(AEffect* effect, int32_t opcode,
                                        int32_t index, intptr_t value,
                                        void* ptr, float opt);
typedef intptr_t (*AEffectDispatcherProc)(AEffect* effect, int32_t opcode,
                                          int32_t index, intptr_t value,
                                          void* ptr, float opt);
typedef void (*AEffectProcessProc)(AEffect* effect, float** inputs,
                                   float** outputs, int32_t sampleFrames);
typedef void (*AEffectProcessDoubleProc)(AEffect* effect, double** inputs,
                                         double** outputs, int32_t sampleFrames);
typedef void (*AEffectSetParameterProc)(AEffect* effect, int32_t index,
                                        float parameter);
typedef float (*AEffectGetParameterProc)(AEffect* effect, int32_t index);

// Classic VST2 AEffect layout (must match the ABI byte-for-byte).
struct AEffect {
  int32_t magic;  // kEffectMagic
  AEffectDispatcherProc dispatcher;
  AEffectProcessProc process;
  AEffectSetParameterProc setParameter;
  AEffectGetParameterProc getParameter;

  int32_t numPrograms;
  int32_t numParams;
  int32_t numInputs;
  int32_t numOutputs;
  int32_t flags;

  intptr_t resvd1;
  intptr_t resvd2;

  int32_t initialDelay;
  int32_t realQualities;
  int32_t offQualities;
  float ioRatio;

  void* object;
  void* user;
  int32_t uniqueID;
  int32_t version;

  AEffectProcessProc processReplacing;
  AEffectProcessDoubleProc processDoubleReplacing;

  char future[56];
};

// Magic 'VstP'.
static const int32_t kEffectMagic = 0x56737450;

// AEffect.flags
enum {
  effFlagsHasEditor = 1 << 0,
  effFlagsCanReplacing = 1 << 4,
};

// Dispatcher opcodes (subset).
enum {
  effOpen = 0,
  effClose = 1,
  effSetSampleRate = 10,
  effSetBlockSize = 11,
  effMainsChanged = 12,
  effEditGetRect = 13,
  effEditOpen = 14,
  effEditClose = 15,
  effEditDraw = 16,  // used as `index` inside effVendorSpecific for fx-embed
  effGetPlugCategory = 35,
  effGetVendorString = 47,
  effGetProductString = 48,
  effGetVendorVersion = 49,
  effVendorSpecific = 50,
  effCanDo = 51,
  effGetVstVersion = 58,
};

// VstPlugCategory
enum { kPlugCategEffect = 1 };

}  // extern "C"

#endif  // X32MIRROR_VST2_MIN_H_
