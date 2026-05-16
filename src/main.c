/*
 * Copyright (c) 2026 Bayrem Gharsellaoui
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(main, LOG_LEVEL_DBG);

#include <psa/crypto.h>
#include <psa/crypto_values.h>

#include "pn532.h"

#define NONCE_SIZE             (32U)
#define APDU_RESPONSE_MAX_SIZE (255U)

static int verify_android_signature(const uint8_t *nonce, const uint8_t *signature, size_t sig_len);

/*
 * SELECT APDU (AID selection)
 * AID = F123456789ABCDE1 (8 bytes)
 */
static const uint8_t SELECT_APDU_CMD[] = {
	0x00,                                           /* CLA (Standard) */
	0xA4,                                           /* INS (SELECT) */
	0x04,                                           /* P1 (select by AID) */
	0x00,                                           /* P2 */
	0x08,                                           /* Lc: length = 8 bytes */
	0xF1, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xE1, /* AID (F123456789ABCDE1) */
	0x00                                            /* Le */
};

/*
 * CHALLENGE APDU (to request signature of the nonce from the Android device)
 */
static const uint8_t CHALLENGE_APDU_HEADER[] = {
	0x80, /* CLA (Proprietary) */
	0x10, /* INS (Challenge) */
	0x00, /* P1 */
	0x00, /* P2 */
	0x20  /* Lc (32 bytes) */
};

/*
 * Authorized public key (uncompressed format, 65 bytes: 0x04 || X || Y)
 * Corresponds to the private key used by the Android device to sign the nonce.
 */
static const uint8_t AUTHORIZED_PUBLIC_KEY[] = {
	0x04, 0xd0, 0xa3, 0xf6, 0x3b, 0x08, 0x54, 0x2d, 0x77, 0xcf, 0xfd, 0x7b, 0xbe,
	0xc7, 0xf3, 0x11, 0x6f, 0x48, 0xf4, 0x47, 0x6a, 0x45, 0x56, 0x8e, 0x1b, 0x2f,
	0x92, 0x5c, 0xc8, 0xbc, 0xfa, 0xf0, 0x12, 0x22, 0x94, 0x72, 0x04, 0x9e, 0x9f,
	0x49, 0x6a, 0x44, 0xa8, 0x37, 0x72, 0xae, 0x79, 0x2f, 0x2b, 0x9d, 0x56, 0x62,
	0xfe, 0x95, 0x1e, 0x46, 0x0a, 0x19, 0xa2, 0x9f, 0x28, 0x5c, 0x47, 0x04, 0x13};

int main(void)
{
	const struct device *pn532_dev = DEVICE_DT_GET_ONE(nxp_pn532);
	uint8_t nonce[NONCE_SIZE] = {0};
	uint8_t challenge_apdu[sizeof(CHALLENGE_APDU_HEADER) + sizeof(nonce)] = {0};
	uint8_t response[APDU_RESPONSE_MAX_SIZE] = {0};
	uint8_t resp_len = 0;

	if (psa_crypto_init() != PSA_SUCCESS) {
		LOG_ERR("PSA Crypto init failed");
		return 0;
	}

	if (!device_is_ready(pn532_dev)) {
		LOG_ERR("PN532 device not ready");
		return 0;
	}

	LOG_INF("System ready. Waiting for NFC target...");

	while (1) {
		/* 1. DISCOVERY */
		if (pn532_in_list_passive_target(pn532_dev) < 0) {
			k_msleep(200);
			continue;
		}
		LOG_INF("Target detected");

		/* 2. SELECT AID */
		resp_len = sizeof(response);
		if (pn532_in_data_exchange(pn532_dev, (uint8_t *)SELECT_APDU_CMD,
					   sizeof(SELECT_APDU_CMD), response, &resp_len) < 0) {
			LOG_WRN("AID selection failed");
			continue;
		}

		if ((resp_len < 2) || (response[resp_len - 2] != 0x90) ||
		    (response[resp_len - 1] != 0x00)) {
			LOG_WRN("AID rejected");
			continue;
		}

		LOG_INF("Received SELECT AID response");
		LOG_HEXDUMP_DBG(response, resp_len, "Response:");

		LOG_INF("AID selected");

		/* 3. GENERATE NONCE */
		if (psa_generate_random(nonce, sizeof(nonce)) != PSA_SUCCESS) {
			LOG_ERR("Failed to generate nonce");
			continue;
		}
		LOG_HEXDUMP_DBG(nonce, sizeof(nonce),
				"Generated " STRINGIFY(NONCE_SIZE) "-byte nonce:");

		/* Build challenge APDU */
		memcpy(challenge_apdu, CHALLENGE_APDU_HEADER, sizeof(CHALLENGE_APDU_HEADER));
		memcpy(challenge_apdu + sizeof(CHALLENGE_APDU_HEADER), nonce, sizeof(nonce));

		/* Send challenge APDU and receive response */
		resp_len = sizeof(response);
		if (pn532_in_data_exchange(pn532_dev, challenge_apdu, sizeof(challenge_apdu),
					   response, &resp_len) < 0) {
			LOG_ERR("Challenge failed");
			continue;
		}

		LOG_INF("Received signature response");
		LOG_HEXDUMP_DBG(response, resp_len, "Response:");

		/* 4. VERIFY RESPONSE */
		if (resp_len >= 2) {
			if ((response[resp_len - 2] != 0x90) || (response[resp_len - 1] != 0x00)) {
				LOG_WRN("Invalid response format");
				continue;
			}
		} else {
			LOG_WRN("Signature response too short");
			continue;
		}

		const uint8_t *signature = response;
		/* -2 to exclude the status words (SW1, SW2) at the end of the response */
		size_t sig_len = resp_len - 2;

		LOG_INF("Verifying signature...");

		if (verify_android_signature(nonce, signature, sig_len) < 0) {
			LOG_ERR("AUTH FAILED (signature invalid)");
			continue;
		}

		LOG_INF("AUTH SUCCESS (verified signature)");

		k_msleep(2000);
	}

	return 0;
}

static int verify_android_signature(const uint8_t *nonce, const uint8_t *signature, size_t sig_len)
{
	psa_status_t status;
	mbedtls_svc_key_id_t key_id;
	psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;

	/* 1. Setup Key Attributes */
	psa_set_key_type(&attributes, PSA_KEY_TYPE_ECC_PUBLIC_KEY(PSA_ECC_FAMILY_SECP_R1));
	psa_set_key_algorithm(&attributes, PSA_ALG_ECDSA(PSA_ALG_SHA_256));
	psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_VERIFY_MESSAGE);

	/* 2. Import Public Key */
	status = psa_import_key(&attributes, AUTHORIZED_PUBLIC_KEY, sizeof(AUTHORIZED_PUBLIC_KEY),
				&key_id);
	if (status != PSA_SUCCESS) {
		LOG_ERR("Key import failed (%d)", status);
		return -1;
	}

	/* 3. Verify Signature */
	status = psa_verify_message(key_id, PSA_ALG_ECDSA(PSA_ALG_SHA_256), nonce, NONCE_SIZE,
				    signature, sig_len);
	if (status != PSA_SUCCESS) {
		LOG_ERR("Signature verification failed (%d)", status);
		/* Destroy the key before returning */
	}

	psa_destroy_key(key_id);

	return (status == PSA_SUCCESS) ? 0 : -1;
}
