/*
 * Copyright (c) 2026 Bayrem Gharsellaoui
 * SPDX-License-Identifier: Apache-2.0
 */
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(test);

#include "pn532.h"

int main(void) {
    const struct device *dev = DEVICE_DT_GET_ONE(nxp_pn532);
    struct pn532_fw_version version = {0};

    if (!device_is_ready(dev)) {
        LOG_ERR("PN532 device not ready");
        return 0;
    }

    if (pn532_get_firmware_version(dev, &version) == 0) {
        LOG_INF("Found PN5%02X", version.ic);
        LOG_INF("Firmware version: %d.%d", version.ver, version.rev);
    } else {
        LOG_ERR("Failed to get PN532 firmware version");
    }

    while (true) {
        k_msleep(500);
    }

    return 0;
}

void k_sys_fatal_error_handler(unsigned int reason, const struct arch_esf *esf)
{
	ARG_UNUSED(esf);
	struct k_thread *faulting_thread = NULL;

	switch (reason) {
	case K_ERR_CPU_EXCEPTION: {
		LOG_ERR("Generic CPU exception, not covered by other codes");
		break;
	}
	case K_ERR_SPURIOUS_IRQ: {
		LOG_ERR("Unhandled hardware interrupt");
		break;
	}
	case K_ERR_STACK_CHK_FAIL: {
		LOG_ERR("Faulting context overflowed its stack buffer");
		/* Get the current thread that caused the fault */
		faulting_thread = k_current_get();
		if (faulting_thread) {
			LOG_ERR("Fault occurred in thread: %s", k_thread_name_get(faulting_thread));
			LOG_ERR("Thread ID: %p", (void *)faulting_thread);
			LOG_ERR("Stack start: %p, size: %zu",
				(void *)faulting_thread->stack_info.start,
				faulting_thread->stack_info.size);
		} else {
			LOG_ERR("Could not determine faulting thread");
		}
		break;
	}
	case K_ERR_KERNEL_OOPS: {
		LOG_ERR("Moderate severity software error");
		break;
	}
	case K_ERR_KERNEL_PANIC: {
		LOG_ERR("High severity software error");
		break;
	}
	case K_ERR_ARCH_START: {
		LOG_ERR("Arch specific fatal errors");
		break;
	}
	default: {
		LOG_ERR("Unknow reason for fatal error (%d)", reason);
		break;
	}
	}

	/* Disable interrupts and halt the system */
	arch_irq_lock();
	for (;;) { /* Spin endlessly */ }
}
