#pragma once

extern "C" extern IMAGE_DOS_HEADER __ImageBase;



namespace SVM
{
    BOOLEAN CheckSupport();
    void Initialize(PVOID context);
    void Disable();
    void Teardown();
}
