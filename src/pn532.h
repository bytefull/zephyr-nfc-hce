#ifndef PN532_H
#define PN532_H

#include <zephyr/device.h>

/* Firmware version structure */
struct pn532_fw_version {
    uint8_t ic;  /* PN5xx IC type (e.g. 0x32 for PN532) */
    uint8_t ver; /* Firmware version */
    uint8_t rev; /* Firmware revision */
};

/**
 * @brief Initialize the PN532 device.
 */
void pn532_init(void);

/**
 * @brief Send the wakeup sequence to the PN532 to wake it up from power-down mode.
 */
void pn532_wakeup(void);

/**
 * @brief Send the SAMConfig command to the PN532 to configure the SAM (Secure Access Module).
 * @return true if the command was successful, false otherwise.
 */
bool pn532_sam_config(void);

/**
 * @brief Set the serial baud rate of the PN532.
 * @param baudrate should be a number corresponding to
 * one of the defined baud rate values (e.g. PN532_BAUDRATE_115200).
 * @return true if the command was successful, false otherwise.
 */
bool pn532_set_serial_baudrate(uint32_t baudrate);

/**
 * @brief Send the GetFirmwareVersion command to the PN532 and parse the response.
 * @param fw_version Pointer to a pn532_fw_version struct to store the parsed firmware version.
 * @return true if the command was successful and the firmware version was parsed, false otherwise.
 */
bool pn532_get_firmware_version(struct pn532_fw_version *fw_version);

/**
 * @brief Send the InListPassiveTarget command to the PN532 to list passive targets
 * and parse the response.
 * @return true if a target was successfully in-listed, false otherwise.
 */
bool pn532_in_list_passive_target(void);

/**
 * @brief Send the InDataExchange command to the PN532 to exchange data with the inlisted target
 * and parse the response.
 * @param send Pointer to the command buffer (payload) to send (without framing).
 * @param sendLength Length of the command buffer in bytes.
 * @param response Pointer to a buffer to store the response data (without framing).
 * @param responseLength Pointer to a variable that holds the length of the response buffer.
 * Updated with actual response length on success.
 * @return true if the command was successful and a valid response was received, false otherwise.
 */
bool pn532_in_data_exchange(uint8_t *send, uint8_t sendLength, uint8_t *response, uint8_t *responseLength);

#endif /* PN532_H */