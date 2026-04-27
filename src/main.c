/*
 * Copyright (c) 2026 Bayrem Gharsellaoui
 * SPDX-License-Identifier: Apache-2.0
 */
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/entropy.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(main, LOG_LEVEL_DBG);

#include "pn532.h"

/*
 * SELECT APDU (AID selection)
 * AID = F123456789ABCDE1 (8 bytes)
 */
static const uint8_t SELECT_APDU_CMD[] = {
	0x00,                                           /* CLA */
	0xA4,                                           /* INS (SELECT) */
	0x04,                                           /* P1 (select by AID) */
	0x00,                                           /* P2 */
	0x08,                                           /* Lc: length = 8 bytes */
	0xF1, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xE1, /* AID (F123456789ABCDE1) */
	0x00                                            /* Le */
};

/*
 * CHALLENGE APDU (to request a challenge/nonce from the card/phone)
 */
static const uint8_t CHALLENGE_APDU_HEADER[] = {
	0x80, /* CLA (Proprietary) */
	0x10, /* INS (Challenge) */
	0x00, /* P1 */
	0x00, /* P2 */
	0x20  /* Lc (32 bytes) */
};

static const uint8_t PUBLIC_KEY[] = {
	/* TODO: Paste example public key here */
};

static uint8_t nonce[32] = {0};

int main(void)
{
	const struct device *pn532_dev = DEVICE_DT_GET_ONE(nxp_pn532);
	const struct device *rng_dev = DEVICE_DT_GET(DT_NODELABEL(rng));
	uint8_t full_challenge_cmd[sizeof(CHALLENGE_APDU_HEADER) + sizeof(nonce)] = {0};

	if (!device_is_ready(rng_dev)) {
		LOG_ERR("Entropy device not ready");
		return 0;
	}

	/* 1. Generate a 32-byte cryptographically secure nonce */
	if (entropy_get_entropy(rng_dev, nonce, sizeof(nonce))) {
		LOG_ERR("Failed to generate secure nonce");
		return 0;
	}
	LOG_HEXDUMP_INF(nonce, sizeof(nonce), "Challenge Nonce");

	/* 2. Prepare and Send Challenge via InDataExchange */
	memcpy(full_challenge_cmd, CHALLENGE_APDU_HEADER, sizeof(CHALLENGE_APDU_HEADER));
	memcpy(full_challenge_cmd + sizeof(CHALLENGE_APDU_HEADER), nonce, sizeof(nonce));

	uint8_t sig_response[66] = {0}; // 64 bytes signature + 2 bytes status (0x9000)
	uint8_t sig_len = sizeof(sig_response);

	LOG_INF("Sending Challenge to Phone...");
	if (pn532_in_data_exchange(pn532_dev, full_challenge_cmd, sizeof(full_challenge_cmd),
							   sig_response, &sig_len) < 0) {
		LOG_ERR("Challenge Exchange failed");
		return 0;
	}
	LOG_HEXDUMP_INF(sig_response, sig_len, "Received Signature Response");

	/* 3. Verify the Signature */
	if ((sig_response[sig_len-2] == 0x90) && (sig_response[sig_len-1] == 0x00)) {
		LOG_INF("Signature received! Proceeding to verification...");
		// TODO: Use mbedTLS to verify the signature against the nonce using the known public key
	}

	 /* We're expecting a 64-byte signature followed by 0x90 0x00 */
	if (!device_is_ready(pn532_dev)) {
		LOG_ERR("PN532 device not ready");
		return 0;
	}

	/* ---- InListPassiveTarget ---- */
	while (1) {
		if (pn532_in_list_passive_target(pn532_dev) < 0) {
			LOG_DBG("No card detected, retrying...");
			k_msleep(500);
			continue;
		}
		LOG_INF("Detected something...");
		break;
	}

	/* ---- InDataExchange ---- */
	uint8_t response[2] = {0};
	uint8_t response_length = sizeof(response);
	if (pn532_in_data_exchange(pn532_dev, (uint8_t *)SELECT_APDU_CMD, sizeof(SELECT_APDU_CMD),
				   response, &response_length) < 0) {
		LOG_ERR("inDataExchange failed");
		return 0;
	}
	LOG_DBG("Received response (%d bytes):", response_length);
	LOG_HEXDUMP_DBG(response, response_length, "Response");
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
