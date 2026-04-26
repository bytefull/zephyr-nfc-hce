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
        LOG_INF("PN532 device not ready");
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