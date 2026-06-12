#include "Globals.h"
#include "SVM/Svm.h"

static ULONG_PTR SvmTeardownCallback(ULONG_PTR) {
    SVM::Disable();
    return 0;
}

NTSTATUS CustomDriverEntry(
    _In_ PDRIVER_OBJECT  kdmapperParam1,
    _In_ PUNICODE_STRING kdmapperParam2
)
{
    UNREFERENCED_PARAMETER(kdmapperParam1);
    UNREFERENCED_PARAMETER(kdmapperParam2);

    log_debug("[Hv2] Driver entry reached.");

    if (!SVM::CheckSupport())
    {
        log_error("[Hv2] SVM is not supported on this processor.");
        return STATUS_NOT_SUPPORTED;
    }

    log_debug("[Hv2] SVM supported. Spawning VMM initialization thread.");

    HANDLE thread_handle = nullptr;
    NTSTATUS status = PsCreateSystemThread(
        &thread_handle,
        THREAD_ALL_ACCESS,
        nullptr,
        nullptr,
        nullptr,
        SVM::Initialize,
        nullptr
    );

    if (!NT_SUCCESS(status))
    {
        log_error("[Hv2] Failed to create VMM thread: 0x%X", status);
        return status;
    }
    return STATUS_SUCCESS;
}
