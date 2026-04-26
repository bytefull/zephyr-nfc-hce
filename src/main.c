/*
 * Copyright (c) 2026 Bayrem Gharsellaoui
 * SPDX-License-Identifier: Apache-2.0
 */
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(main, LOG_LEVEL_DBG);

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

#if 0
    /* --- SetSerialBaudRate --- */
    if (pn532_set_serial_baudrate(dev, 230400) < 0) {
        LOG_ERR("Failed to set serial baud rate");
        return 0;
    }
#endif

    /* ---- GetFirmwareVersion ---- */
    struct pn532_fw_version version = {0};
    if (pn532_get_firmware_version(dev, &version) < 0) {
        LOG_ERR("GetFirmwareVersion failed");
        return 0;
    }
    LOG_INF("Found PN5%02X", version.ic);
    LOG_INF("Firmware version: %d.%d", version.ver, version.rev);

    /* ---- InListPassiveTarget ---- */
    while (1) {
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

    while (1) {
        k_msleep(500);
    }

    return 0;
}
