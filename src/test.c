/*
 * Copyright (c) 2026 Bayrem Gharsellaoui
 * SPDX-License-Identifier: Apache-2.0
 */
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(test);

#include "pn532.h"

/*
 * SELECT APDU (AID selection)
 * AID = F123456789ABCDE1 (8 bytes)
 */
static const uint8_t SELECT_APDU_CMD[] = {
    0x00,  /* CLA */
    0xA4,  /* INS (SELECT) */
    0x04,  /* P1 (select by AID) */
    0x00,  /* P2 */
    0x08,  /* Lc: length = 8 bytes */
    0xF1, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xE1, /* AID (F123456789ABCDE1) */
    0x00   /* Le */
};

int main(void) {
    const struct device *dev = DEVICE_DT_GET_ONE(nxp_pn532);

    if (!device_is_ready(dev)) {
        LOG_ERR("PN532 device not ready");
        return 0;
    }

    /* ---- GetFirmwareVersion ---- */
    struct pn532_fw_version version = {0};
    if (pn532_get_firmware_version(dev, &version) < 0) {
        LOG_ERR("GetFirmwareVersion failed");
        return 0;
    }
    LOG_INF("Found PN5%02X", version.ic);
    LOG_INF("Firmware version: %d.%d", version.ver, version.rev);

    /* ---- InListPassiveTarget ---- */
	while (1)
	{
		if (pn532_in_list_passive_target(dev) < 0) {
			LOG_DBG("No card detected, retrying...");
			k_msleep(500);
			continue;
		}
		LOG_INF("Detected something...");
		break;
	}

    /* ---- InDataExchange ---- */
    uint8_t response[2] = {0};
    uint8_t responseLength = sizeof(response);
    if (pn532_in_data_exchange(dev,
							   (uint8_t *)SELECT_APDU_CMD,
							   sizeof(SELECT_APDU_CMD),
							   response,
							   &responseLength) < 0) {
        LOG_ERR("inDataExchange failed");
        return 0;
    }
    LOG_DBG("Received response (%d bytes):", responseLength);
    LOG_HEXDUMP_DBG(response, responseLength, "Response");
    /* We're expecting 0x90 0x00 from the Android phone HCE app */
    if ((response[0] != 0x90) || (response[1] != 0x00)) {
        LOG_ERR("SELECT APDU failed");
        return 0;
    }
    LOG_INF("SELECT APDU successful...");

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
