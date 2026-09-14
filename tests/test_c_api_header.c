/* Compile-only proof that the installed public header remains a C ABI. */
#include "rowl/c_api.h"

_Static_assert(ROWL_RESULT_OK == 0, "ResultCode ABI changed");
_Static_assert(ROWL_RESULT_BUFFER_TOO_SMALL == 12, "ResultCode ABI changed");
_Static_assert(ROWL_ENGINE_CAPABILITY_GRAPH_VNEXT == 8, "Capability ABI changed");
_Static_assert(ROWL_ENGINE_CAPABILITY_PLAYER_LOOP == 16, "Capability ABI changed");
_Static_assert(ROWL_ENGINE_CAPABILITY_SAVE_METADATA == 32, "Capability ABI changed");
_Static_assert(ROWL_ENGINE_C_API_VERSION_MAJOR == 1u, "C API major changed");
_Static_assert(sizeof(RowlEngine_ApiVersion) == sizeof(uint32_t) * 3,
               "ApiVersion layout changed");

void rowl_c_api_header_compile_contract(void) {
    RowlEngine_ApiVersion version = {0, 0, 0};
    uint64_t capabilities = 0;
    (void)RowlEngine_GetApiVersion(&version);
    (void)RowlEngine_GetCapabilities(&capabilities);
}
