// Single translation unit that instantiates the REAPER API function pointers.
#define REAPERAPI_IMPLEMENT
#include "reaper_api.h"

namespace x32 {

bool LoadReaperApi(void* (*getFunc)(const char*)) {
  // REAPERAPI_LoadAPI returns 0 on success, or the number of functions that
  // failed to resolve. We treat any nonzero as failure.
  return REAPERAPI_LoadAPI(getFunc) == 0;
}

}  // namespace x32
