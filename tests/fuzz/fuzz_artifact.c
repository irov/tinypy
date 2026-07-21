#include "tinypy/artifact.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    tinypy_artifact_view_t view;
    tinypy_artifact_status_e status = tinypy_artifact_decode(data, size, 0U, &view);

    if (status == TINYPY_ARTIFACT_OK) {
        tinypy_artifact_expectation_t expectation;
        expectation.abi_version = TINYPY_ARTIFACT_ABI_VERSION;
        expectation.struct_size = (uint32_t)sizeof(expectation);
        expectation.check_flags = 0U;
        expectation.reserved = 0U;
        expectation.expected = view.metadata;
        (void)tinypy_artifact_check_profile(&view, &expectation);
    }
    return 0;
}
