// X32 → REAPER Mirror — embedded-FX companion (x32mirror_embed)
// A minimal VST2 effect: audio passthrough, zero parameters, advertising the
// Cockos embedded-UI capability so REAPER draws an in-strip button via the
// fx-embed protocol. All UI state comes from the extension's vtable, resolved
// lazily through audioMaster; the companion draws an inert placeholder when
// the extension is absent.
#include <cstring>
#include <new>

#include "embed_ui.h"
#include "extension_link.h"
#include "reaper_plugin_fx_embed.h"
#include "vst2_min.h"

namespace {

struct EmbedInstance {
  AEffect effect;
  audioMasterCallback am = nullptr;
  x32embed::ExtensionLink link;
};

EmbedInstance* Inst(AEffect* e) {
  return e ? static_cast<EmbedInstance*>(e->object) : nullptr;
}

void PassThrough(AEffect* effect, float** inputs, float** outputs,
                 int32_t frames) {
  int n = effect ? effect->numOutputs : 0;
  for (int ch = 0; ch < n; ++ch) {
    if (inputs && outputs && inputs[ch] && outputs[ch] && inputs[ch] != outputs[ch])
      std::memcpy(outputs[ch], inputs[ch], sizeof(float) * frames);
  }
}

float GetParam(AEffect*, int32_t) { return 0.0f; }
void SetParam(AEffect*, int32_t, float) {}

// Handle the fx-embed sub-protocol carried on effVendorSpecific/effEditDraw.
intptr_t HandleEmbed(EmbedInstance* inst, AEffect* effect, intptr_t parm2,
                     void* parm3, int msg) {
  switch (msg) {
    case REAPER_FXEMBED_WM_IS_SUPPORTED:
      return 1;
    case REAPER_FXEMBED_WM_CREATE:
    case REAPER_FXEMBED_WM_DESTROY:
      return 0;
    case REAPER_FXEMBED_WM_GETMINMAXINFO:
      return x32embed::FillSizeHints(
          reinterpret_cast<REAPER_FXEMBED_SizeHints*>(parm3));
    case REAPER_FXEMBED_WM_PAINT: {
      // Re-resolve lazily each paint so a late-loaded extension is picked up.
      X32Mirror_Interface* iface = inst->link.Interface(effect, inst->am);
      const char* guid = inst->link.HostGuid(effect, inst->am);
      x32embed::PaintButton(
          reinterpret_cast<REAPER_FXEMBED_IBitmap*>(parm2),
          reinterpret_cast<REAPER_FXEMBED_DrawInfo*>(parm3), iface, guid);
      return 1;
    }
    case REAPER_FXEMBED_WM_LBUTTONDOWN:
    case REAPER_FXEMBED_WM_LBUTTONUP:
    case REAPER_FXEMBED_WM_RBUTTONDOWN:
    case REAPER_FXEMBED_WM_RBUTTONUP:
    case REAPER_FXEMBED_WM_MOUSEMOVE: {
      X32Mirror_Interface* iface = inst->link.Interface(effect, inst->am);
      const char* guid = inst->link.HostGuid(effect, inst->am);
      return x32embed::OnMouse(
          msg, reinterpret_cast<REAPER_FXEMBED_DrawInfo*>(parm3), iface, guid);
    }
    default:
      return 0;
  }
}

intptr_t Dispatcher(AEffect* effect, int32_t opcode, int32_t index,
                    intptr_t value, void* ptr, float opt) {
  EmbedInstance* inst = Inst(effect);
  switch (opcode) {
    case effOpen:
    case effClose:
      return 0;
    case effGetVendorString:
      if (ptr) std::strcpy(static_cast<char*>(ptr), "X32Mirror");
      return 1;
    case effGetProductString:
      if (ptr) std::strcpy(static_cast<char*>(ptr), "X32 Mirror Embed");
      return 1;
    case effGetVendorVersion:
      return 1000;
    case effGetPlugCategory:
      return kPlugCategEffect;
    case effGetVstVersion:
      return 2400;
    case effCanDo:
      if (ptr && std::strcmp(static_cast<char*>(ptr), "hasCockosEmbeddedUI") == 0)
        return 0xbeef0000;  // opt into REAPER's embedded-UI protocol
      return 0;
    case effVendorSpecific:
      if (index == effEditDraw && inst) {
        // REAPER packs parm2 in `value`, parm3 in `ptr`, and the
        // REAPER_FXEMBED_WM_* message in `opt` (as a float).
        return HandleEmbed(inst, effect, value, ptr, static_cast<int>(opt));
      }
      return 0;
    default:
      return 0;
  }
}

}  // namespace

// --- VST2 entry point -------------------------------------------------------
// REAPER (and other hosts) call VSTPluginMain to instantiate the effect.
extern "C" {

#if defined(_WIN32)
#define X32_VST_EXPORT __declspec(dllexport)
#else
#define X32_VST_EXPORT __attribute__((visibility("default")))
#endif

X32_VST_EXPORT AEffect* VSTPluginMain(audioMasterCallback audioMaster) {
  EmbedInstance* inst = new (std::nothrow) EmbedInstance();
  if (!inst) return nullptr;

  AEffect& e = inst->effect;
  std::memset(&e, 0, sizeof(e));
  e.magic = kEffectMagic;
  e.dispatcher = &Dispatcher;
  e.process = &PassThrough;
  e.setParameter = &SetParam;
  e.getParameter = &GetParam;
  e.numPrograms = 0;
  e.numParams = 0;
  e.numInputs = 2;
  e.numOutputs = 2;
  e.flags = effFlagsCanReplacing;
  e.initialDelay = 0;
  e.object = inst;
  e.uniqueID = 0x58334d45;  // 'X3ME'
  e.version = 1000;
  e.processReplacing = &PassThrough;
  e.processDoubleReplacing = nullptr;

  inst->am = audioMaster;
  return &e;
}

// Some hosts look up the older/aliased entry-point name.
X32_VST_EXPORT AEffect* main_plugin(audioMasterCallback audioMaster) {
  return VSTPluginMain(audioMaster);
}

}  // extern "C"
