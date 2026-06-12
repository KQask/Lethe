#pragma once

#define log_debug(fmt, ...) DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "[Hv2][DBG] " fmt "\n", ##__VA_ARGS__)
#define log_error(fmt, ...) DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "[Hv2][ERR] " fmt "\n", ##__VA_ARGS__)
