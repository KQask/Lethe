#pragma once
#include <ntddk.h>
#include <wdm.h>
#include <intrin.h>
#include <cstdint>
#include <ntimage.h>

#include "util.h"

#include "SVM/Arch/cr.h"
#include "SVM/Arch/dr.h"
#include "SVM/Arch/rflags.h"
#include "SVM/Arch/msr.h"

#include "SVM/Arch/Hypercalls.h"
#include "SVM/Arch/Intrin.h"
#include "SVM/Arch/Guest.h"
#include "SVM/Arch/ControlArea.h"
#include "SVM/Arch/VmExit.h"

#include "SVM/Handlers/VmExitHandlers.h"

#include "SVM/Vmcb.h"
#include "SVM/Svm.h"
#include "SVM/Npt.h"