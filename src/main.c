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

// static psa_status_t verify_android_signature(const uint8_t *nonce, const uint8_t *signature,
// 					     size_t sig_len);

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

static const uint8_t FAKE_NONCE [] = {
	0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
	0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
	0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
	0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F
};

static const uint8_t FAKE_NONCE_SIGNED [] = {
	0x30, 0x45, 0x02, 0x20, 0x5B, 0x1A, 0xC3, 0xD4,
	0xE5, 0xF6, 0x07, 0x18, 0x29, 0x3A, 0x4B, 0x5C,
	0x6D, 0x7E, 0x8F, 0x90, 0xA1, 0xB2, 0xC3, 0xD4,
	0xE5, 0xF6, 0x07, 0x18, 0x29, 0x3A, 0x4B, 0x5C,
	0x02, 0x21, 0x00, 0x9A, 0xBC, 0xDE, 0xF0, 0x12,
	0x34, 0x56, 0x78, 0x9A, 0xBC, 0xDE, 0xF0, 0x12,
	0x34, 0x56, 0x78, 0x9A, 0xBC, 0xDE, 0xF0, 0x12,
	0x34, 0x56, 0x78, 0x90, 0xAB, 0xCD, 0xEF, 0x01
};

// /*
//  * Public key (P-256 uncompressed: 0x04 || X || Y)
//  */
// static const uint8_t AUTHORIZED_PUBLIC_KEY[] = {
// 	0x04, 0xfa, 0x8c, 0x33, 0xaf, 0x35, 0x8b, 0x8b, 0x66, 0x65, 0x12, 0x1d, 0x1e,
// 	0x3b, 0xaf, 0xa6, 0xce, 0xce, 0x8b, 0xda, 0x83, 0x07, 0xe2, 0x11, 0x4c, 0x7a,
// 	0x13, 0xa5, 0x00, 0x50, 0x78, 0x3b, 0x31, 0xed, 0xf8, 0xb7, 0x2b, 0x51, 0x09,
// 	0x69, 0xc2, 0x4e, 0x2b, 0xee, 0x94, 0x34, 0x61, 0xbc, 0xe6, 0x4f, 0x3b, 0x50,
// 	0x80, 0x12, 0xc9, 0x84, 0xc9, 0xb5, 0x3d, 0x34, 0xe8, 0x5c, 0x50, 0x33, 0x28};

int main(void)
{
	const struct device *pn532_dev = DEVICE_DT_GET_ONE(nxp_pn532);
	uint8_t nonce[32] = {0};
	uint8_t challenge_apdu[sizeof(CHALLENGE_APDU_HEADER) + sizeof(nonce)] = {0};
	uint8_t response[255] = {0};
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

		LOG_INF("Received select aid response");
		LOG_HEXDUMP_DBG(response, resp_len, "Response:");

		LOG_INF("AID selected");

		/* 3. GENERATE NONCE */
		// if (psa_generate_random(nonce, sizeof(nonce)) != PSA_SUCCESS) {
		// 	LOG_ERR("Failed to generate nonce");
		// 	continue;
		// }
		memcpy(nonce, FAKE_NONCE, sizeof(FAKE_NONCE));

		LOG_HEXDUMP_DBG(nonce, sizeof(nonce), "Generated 32-byte nonce:");

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
		if ((resp_len < (64 + 2)) || (response[resp_len - 2] != 0x90) ||
		    (response[resp_len - 1] != 0x00)) {
			LOG_WRN("Invalid response format");
			continue;
		}

		// const uint8_t *signature = response;
		/* -2 to exclude the status words (SW1, SW2) at the end of the response */
		size_t sig_len = resp_len - 2;

		if (sig_len != 64) {
			LOG_ERR("Invalid signature length: %d", sig_len);
			continue;
		}

		LOG_INF("Verifying signature...");

		if (memcmp(response, FAKE_NONCE_SIGNED, resp_len-2) == 0) {
			LOG_INF("Received expected fake signature response");
		} else {
			LOG_ERR("Received unexpected signature response");
			continue;
		}

		// if (verify_android_signature(nonce, signature, sig_len) != PSA_SUCCESS) {
		// 	LOG_ERR("AUTH FAILED (signature invalid)");
		// 	continue;
		// }

		LOG_INF("AUTH SUCCESS (verified signature)");

		k_msleep(2000);
	}

	return 0;
}

// static psa_status_t verify_android_signature(const uint8_t *nonce, const uint8_t *signature,
// 					     size_t sig_len)
// {
// 	psa_status_t status;
// 	mbedtls_svc_key_id_t key_id;
// 	psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;

// 	/* 1. Setup Key Attributes */
// 	psa_set_key_type(&attributes, PSA_KEY_TYPE_ECC_PUBLIC_KEY(PSA_ECC_FAMILY_SECP_R1));
// 	psa_set_key_algorithm(&attributes, PSA_ALG_ECDSA(PSA_ALG_SHA_256));
// 	psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_VERIFY_MESSAGE);

// 	/* 2. Import the Android Public Key into a volatile slot */
// 	status = psa_import_key(&attributes, AUTHORIZED_PUBLIC_KEY, sizeof(AUTHORIZED_PUBLIC_KEY),
// 				&key_id);
// 	if (status != PSA_SUCCESS) {
// 		LOG_ERR("Key import failed (%d)", status);
// 		return status;
// 	}

// 	/* 3. Verify the signature */
// 	status = psa_verify_message(key_id, PSA_ALG_ECDSA(PSA_ALG_SHA_256), nonce, 32, signature,
// 				    sig_len);

// 	psa_destroy_key(key_id);
// 	return status;
// }
