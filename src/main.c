/*
 * Copyright (c) 2026 Bayrem Gharsellaoui
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/entropy.h>
#include <psa/crypto.h>
#include <psa/crypto_values.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(main, LOG_LEVEL_DBG);

#include "pn532.h"

static psa_status_t verify_android_signature(const uint8_t *nonce, const uint8_t *signature);

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

/*
 * Dummy Public Key (P-256 Uncompressed: 0x04 + X + Y)
 * To be replaced this with the public key exported from the Android app.
 */
static const uint8_t AUTHORIZED_PUBLIC_KEY[] = {
	0x04, 0x71, 0x93, 0x4d, 0x3d, 0x90, 0x28, 0x3d, 0x76, 0x78, 0x51, 0x67, 0x3c,
	0x98, 0x77, 0x9e, 0x38, 0x9b, 0x81, 0x1a, 0x0c, 0x24, 0x8e, 0xe2, 0x9d, 0x83,
	0x25, 0xe1, 0x50, 0xc1, 0x61, 0x8b, 0x95, 0x40, 0x8b, 0x23, 0x39, 0x03, 0x9b,
	0x01, 0xc0, 0x6e, 0x9c, 0xda, 0x64, 0xa1, 0x84, 0x0a, 0x94, 0x5d, 0x98, 0xb6,
	0xb8, 0x21, 0x90, 0xaf, 0x34, 0x84, 0xef, 0x76, 0x33, 0x04, 0x29, 0x84, 0xf6};

int main(void)
{
	const struct device *pn532_dev = DEVICE_DT_GET_ONE(nxp_pn532);
	const struct device *rng_dev = DEVICE_DT_GET(DT_NODELABEL(rng));
	uint8_t nonce[32] = {0};
	uint8_t challenge_apdu[sizeof(CHALLENGE_APDU_HEADER) + sizeof(nonce)] = {0};

	if (psa_crypto_init() != PSA_SUCCESS) {
		LOG_ERR("PSA Crypto initialization failed");
		return 0;
	}

	if (!device_is_ready(pn532_dev) || !device_is_ready(rng_dev)) {
		LOG_ERR("Hardware peripherals not ready");
		return 0;
	}

	LOG_INF("System Initialized. Waiting for NFC Target...");

	while (1) {
		/* 1. DISCOVERY: Wait for a phone to be tapped */
		if (pn532_in_list_passive_target(pn532_dev) < 0) {
			k_msleep(200);
			continue;
		}
		LOG_INF("Target Detected!");

		/* 2. SELECTION: Tell Android to wake up the HCE Service */
		uint8_t response[255] = {0};
		uint8_t res_len = sizeof(response);
		if (pn532_in_data_exchange(pn532_dev, (uint8_t *)SELECT_APDU_CMD,
					   sizeof(SELECT_APDU_CMD), response, &res_len) < 0) {
			LOG_WRN("AID Selection failed. Retrying...");
			continue;
		}
		if ((response[res_len - 2] != 0x90) || (response[res_len - 1] != 0x00)) {
			LOG_WRN("Android App refused AID selection");
			continue;
		}
		LOG_INF("Android HCE Service Selected.");

		/* 3. CHALLENGE: Generate Nonce and send to phone */
		entropy_get_entropy(rng_dev, nonce, sizeof(nonce));
		LOG_HEXDUMP_DBG(nonce, sizeof(nonce), "Generated Nonce:");

		memcpy(challenge_apdu, CHALLENGE_APDU_HEADER, sizeof(CHALLENGE_APDU_HEADER));
		memcpy(challenge_apdu + sizeof(CHALLENGE_APDU_HEADER), nonce, 32);

		res_len = sizeof(response);
		LOG_INF("Sending Cryptographic Challenge...");
		if (pn532_in_data_exchange(pn532_dev, challenge_apdu, sizeof(challenge_apdu),
					   response, &res_len) < 0) {
			LOG_ERR("Challenge exchange failed");
			continue;
		}

		/* 4. VERIFICATION: Check the received signature from the phone against our public key */
		if ((res_len < 66) || (response[res_len - 2] != 0x90) ||
		    (response[res_len - 1] != 0x00)) {
			LOG_WRN("Invalid response format from phone");
			LOG_DBG("Response Length: %d, Status: 0x%02X 0x%02X", res_len,
				response[res_len - 2], response[res_len - 1]);
			continue;
		}
		LOG_INF("Signature received. Verifying...");
		/* Note: In a production system, we should SHA-256 the nonce before verification */
		if (verify_android_signature(nonce, response) < 0) {
			LOG_ERR("❌ AUTHENTICATION FAILED: Invalid Signature.");
			continue;
		}
		LOG_INF("✅ AUTHENTICATION SUCCESS: Access Granted.");

		LOG_INF("Resetting for next transaction...");
		k_msleep(2000);
	}

	return 0;
}

psa_status_t verify_android_signature(const uint8_t *nonce, const uint8_t *signature)
{
	psa_status_t status;
	mbedtls_svc_key_id_t key_id;
	psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;

	/* 1. Setup Key Attributes */
	psa_set_key_type(&attributes, PSA_KEY_TYPE_ECC_PUBLIC_KEY(PSA_ECC_FAMILY_SECP_R1));
	psa_set_key_algorithm(&attributes, PSA_ALG_ECDSA(PSA_ALG_SHA_256));
	psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_VERIFY_HASH);

	/* 2. Import the Android Public Key into a volatile slot */
	status = psa_import_key(&attributes, AUTHORIZED_PUBLIC_KEY, sizeof(AUTHORIZED_PUBLIC_KEY),
				&key_id);
	if (status != PSA_SUCCESS) {
		LOG_ERR("Failed to import public key (%d)", status);
		return status;
	}

	/* 3. Verify the Signature
	 * Note: This assumes the Android app signs the SHA-256 hash of the nonce.
	 * If Android signs the raw 32-byte nonce directly, we treat the nonce as the hash.
	 */
	status = psa_verify_hash(key_id, PSA_ALG_ECDSA(PSA_ALG_SHA_256), nonce, 32, signature, 64);

	/* 4. Clean up key slot */
	psa_destroy_key(key_id);
	return status;
}
