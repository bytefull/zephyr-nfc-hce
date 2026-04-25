#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/sys/ring_buffer.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(pn532, LOG_LEVEL_DBG);

#include "pn532.h"

/* PN532 frame fields */
#define PN532_PREAMBLE    (0x00) /* Command sequence start, byte 1/3 */
#define PN532_STARTCODE1  (0x00) /* Command sequence start, byte 2/3 */
#define PN532_STARTCODE2  (0xFF) /* Command sequence start, byte 3/3 */
#define PN532_POSTAMBLE   (0x00) /* EOD */
#define PN532_HOSTTOPN532 (0xD4) /* Host-to-PN532 */
#define PN532_PN532TOHOST (0xD5) /* PN532-to-host */

/* PN532 Commands */
#define PN532_COMMAND_GETFIRMWAREVERSION  (0x02) /* Get firmware version */
#define PN532_COMMAND_SAMCONFIGURATION    (0x14) /* SAM configuration */
#define PN532_COMMAND_INLISTPASSIVETARGET (0x4A) /* List passive target */
#define PN532_COMMAND_INDATAEXCHANGE      (0x40) /* Data exchange */
#define PN532_COMMAND_SETSERIALBAUDRATE   (0x10) /* Set serial baud rate */

/* PN532 Responses */
#define PN532_RESPONSE_INLISTPASSIVETARGET (0x4B) /* List passive target */
#define PN532_RESPONSE_INDATAEXCHANGE      (0x41) /* Data exchange */
#define PN532_RESPONSE_SETSERIALBAUDRATE   (0x11) /* Set serial baud rate */
#define PN532_RESPONSE_GETFIRMWAREVERSION  (0x03) /* Get firmware version */

/**
 * @brief Baud rate values for the PN532.
 * BR is a byte indicating the baud rate requested by the host controller:
 *     − 0x00: 9.6 kbaud,
 *     − 0x01: 19.2 kbaud,
 *     − 0x02: 38.4 kbaud,
 *     − 0x03: 57.6 kbaud,
 *     − 0x04: 115.2 kbaud,
 *     − 0x05: 230.4 kbaud,
 *     − 0x06: 460.8 kbaud,
 *     − 0X07: 921.6 kbaud,
 *     − 0x08: 1.288 Mbaud.
 */
#define PN532_BAUDRATE_9600   (0x00)  /* 9.6 kbaud */
#define PN532_BAUDRATE_19200  (0x01)  /* 19.2 kbaud */
#define PN532_BAUDRATE_38400  (0x02)  /* 38.4 kbaud */
#define PN532_BAUDRATE_57600  (0x03)  /* 57.6 kbaud */
#define PN532_BAUDRATE_115200 (0x04)  /* 115.2 kbaud */
#define PN532_BAUDRATE_230400 (0x05)  /* 230.4 kbaud */
#define PN532_BAUDRATE_460800 (0x06)  /* 460.8 kbaud */
#define PN532_BAUDRATE_921600 (0x07)  /* 921600 baud */
#define PN532_BAUDRATE_1288000 (0x08) /* 1.288 Mbaud */

/* Expected firmware version message from PN532 */
static const uint8_t PN532_EXPECTED_FIRMWARE_VERSION[] = {
    0x00, 0x00, 0xFF, 0x06, 0xFA, 0xD5
};

/* ACK frame */
static const uint8_t PN532_ACK[] = {
    0x00, /* Preamble */
    0x00, /* Start code 1 */
    0xFF, /* Start code 2 */
    0x00, /* LEN  = 0 (no payload) */
    0xFF, /* LCS  = 0x100 - LEN = 0x100 - 0x00 = 0xFF */
    0x00  /* Postamble */
};

/* Wake-up sequence */
static const uint8_t PN532_WAKEUP_SEQ[] = {
    0x55, /* Dummy byte (wakeup pattern, generates clock edges) */
    0x55, /* Dummy byte (ensures PN532 exits power-down) */
    0x00, /* Preamble (start of a "fake" frame) */
    0x00, /* Start code 1 */
    0x00  /* (Not a valid frame, just padding / noise) */
};

/* UART device instance */
static const struct device *uart_dev = DEVICE_DT_GET(DT_ALIAS(pn532_uart));

/* Buffer for building commands to send to PN532 */
static uint8_t pn532_tx_buffer[64] = {0};

/* RX ring buffer for storing incoming data from PN532 */
RING_BUF_DECLARE(pn532_rx_ring_buffer, 256);

/* Tg number of inlisted tag */
static int8_t in_listed_tag = -1;

static void pn532_uart_rx_cb(const struct device *dev, void *user_data)
{
    ARG_UNUSED(user_data);
    int len = 0;
    uint8_t chunk[32] = {0};

    if (dev == NULL) {
        LOG_ERR("Invalid UART device in callback");
        return;
    }

    if (dev != uart_dev) {
        LOG_ERR("Interrupt from unknown UART device");
        return;
    }

    if (!uart_irq_update(dev)) {
        return;
    }

    if (!uart_irq_rx_ready(dev)) {
        return;
    }

    len = uart_fifo_read(dev, chunk, sizeof(chunk));
    if (len <= 0) {
        LOG_WRN("No bytes found in UART FIFO");
        return;
    }
    LOG_HEXDUMP_DBG(chunk, len, "RX chunk");

    if (ring_buf_put(&pn532_rx_ring_buffer, chunk, len) != len) {
        LOG_ERR("Failed to put %d bytes into RX ring buffer", len);
        return;
    }
}

static void pn532_uart_send(const uint8_t *data, size_t len)
{
    if (data == NULL) {
        LOG_ERR("Invalid data buffer");
        return;
    }
    if (len == 0) {
        LOG_ERR("Invalid data length");
        return;
    }

    for (size_t i = 0; i < len; i++) {
        uart_poll_out(uart_dev, data[i]);
    }

    LOG_HEXDUMP_DBG(data, len, "TX");
}

static bool pn532_wait_for_rx(size_t expected_len, int timeout_ms)
{
    int64_t end = k_uptime_get() + timeout_ms;

    while (k_uptime_get() < end) {
        uint32_t rx_len = ring_buf_size_get(&pn532_rx_ring_buffer);
        if (rx_len >= expected_len) {
            return true;
        }
        k_sleep(K_USEC(100));
    }

    return false;
}

static bool pn532_write_command(uint8_t *cmd, uint8_t cmd_len) {
    /* Check if the command buffer is valid */
    if (cmd == NULL) {
        LOG_ERR("Invalid command buffer");
        return false;
    }

    /* Check if the command length is valid */
    if (cmd_len == 0) {
        LOG_ERR("Invalid command length");
        return false;
    }

    /* Allocate full frame: 8 bytes overhead + command length */
    uint8_t frame[8 + cmd_len];

    /* The length should be the command length plus 1 for TFI */
    uint8_t len = cmd_len + 1;

    /* Frame header */
    frame[0] = PN532_PREAMBLE;
    frame[1] = PN532_STARTCODE1;
    frame[2] = PN532_STARTCODE2;

    /* Frame length and length checksum */
    frame[3] = len;
    frame[4] = ~len + 1;

    /* Frame identifier */
    frame[5] = PN532_HOSTTOPN532;

    /*
     * Copy command payload into frame and compute checksum
     * Checksum is computed over TFI + DATA
     */
    uint8_t sum = 0;
    for (uint8_t i = 0; i < cmd_len; i++) {
      frame[6 + i] = cmd[i];
      sum += cmd[i];
    }
    frame[6 + cmd_len] = ~(PN532_HOSTTOPN532 + sum) + 1;

    /* Postamble */
    frame[7 + cmd_len] = PN532_POSTAMBLE;

    /* Send the complete frame */
    pn532_uart_send(frame, 8 + cmd_len);

    return true;
}

static bool pn532_send_command(const uint8_t *cmd, size_t cmd_len, int timeout_ms)
{
    uint8_t ack[sizeof(PN532_ACK)] = {0};
    uint32_t len = 0;

    if (cmd == NULL) {
        LOG_ERR("Invalid command buffer");
        return false;
    }

    if (cmd_len == 0) {
        LOG_ERR("Invalid command length");
        return false;
    }

    /* Flush the RX ring buffer each time we want to send a command */
    ring_buf_reset(&pn532_rx_ring_buffer);

    /* Send command */
    if (!pn532_write_command((uint8_t *)cmd, cmd_len)) {
        return false;
    }

    /* Wait for 6 bytes of ACK */
    if (!pn532_wait_for_rx(sizeof(ack), timeout_ms)) {
        LOG_ERR("Timeout waiting for ACK");
        return false;
    }

    /* Read the ACK bytes */
    len = ring_buf_get(&pn532_rx_ring_buffer, ack, sizeof(ack));
    if (len != sizeof(ack)) {
        LOG_ERR("Failed to read ACK, expected %d bytes but got %d", sizeof(ack), len);
        return false;
    }

    /* Check if the received bytes match the expected ACK */
    if (memcmp(ack, PN532_ACK, sizeof(PN532_ACK)) != 0) {
        LOG_ERR("Invalid ACK");
        return false;
    }
    LOG_DBG("ACK received");

    /* Did the response already start arriving right after reading the ACK? */
    if (ring_buf_size_get(&pn532_rx_ring_buffer)) {
        LOG_DBG("Response already started arriving right after ACK");
        return true;
    }

    /* Otherwise wait for a little bit more for the response to arrive */
    if (!pn532_wait_for_rx(1, timeout_ms)) {
        LOG_ERR("Timeout waiting for response");
        return false;
    }

    LOG_DBG("Response received");
    return true;
}

void pn532_init(void) {
    if (!device_is_ready(uart_dev)) {
        LOG_ERR("UART device not ready");
        return;
    }

    /* Set up UART RX interrupt */
    uart_irq_callback_set(uart_dev, pn532_uart_rx_cb);
    uart_irq_rx_enable(uart_dev);

    LOG_DBG("PN532 UART initialized");
}

void pn532_wakeup(void) {
    LOG_DBG("Sending wakeup command");
    pn532_uart_send(PN532_WAKEUP_SEQ, sizeof(PN532_WAKEUP_SEQ));
#if !defined(CONFIG_BOARD_NATIVE_SIM)
    k_msleep(2);
#endif
}

bool pn532_sam_config(void) {
    LOG_DBG("Sending SAMConfig command");
    pn532_tx_buffer[0] = PN532_COMMAND_SAMCONFIGURATION;
    pn532_tx_buffer[1] = 0x01; // normal mode;
    pn532_tx_buffer[2] = 0x14; // timeout 50ms * 20 = 1 second
    pn532_tx_buffer[3] = 0x01; // use IRQ pin!
    if (!pn532_send_command(pn532_tx_buffer, 4, 100)) {
        return false;
    }
    /* Wait for a little bit until we receive the 9 bytes: size of the SAMConfig response */
    if (!pn532_wait_for_rx(9, 100)) {
        LOG_ERR("Timeout waiting for SAMConfig response");
        return false;
    }
    /* Read the SAMConfig response */
    uint8_t response_buf[9] = {0};
    if (ring_buf_get(&pn532_rx_ring_buffer, response_buf, 9) != 9) {
        LOG_ERR("Failed to read SAMConfig response");
        return false;
    }
    /* Verify response buffer */
    if ((response_buf[0] != 0) || (response_buf[1] != 0) || (response_buf[2] != 0xff)) {
        LOG_ERR("Preamble missing");
        return false;
    }
    /* I don't know what is byte 4 used for */
    if (response_buf[5] != PN532_PN532TOHOST) {
        LOG_ERR("Invalid SAMConfig response: 0x%02X", response_buf[5]);
        return false;
    }
    if (response_buf[6] != 0x15) {
        LOG_ERR("Invalid SAMConfig response: 0x%02X", response_buf[6]);
        return false;
    }

    LOG_DBG("SAMConfig OK");
    return true;
}

bool pn532_set_serial_baudrate(uint32_t baudrate) {
    uint8_t baudrate_code;

    switch (baudrate)
    {
    case 9600:
        baudrate_code = PN532_BAUDRATE_9600;
        break;
    case 19200:
        baudrate_code = PN532_BAUDRATE_19200;
        break;
    case 38400:
        baudrate_code = PN532_BAUDRATE_38400;
        break;
    case 57600:
        baudrate_code = PN532_BAUDRATE_57600;
        break;
    case 115200:
        baudrate_code = PN532_BAUDRATE_115200;
        break;
    case 230400:
        baudrate_code = PN532_BAUDRATE_230400;
        break;
    case 460800:
        baudrate_code = PN532_BAUDRATE_460800;
        break;
    case 921600:
        baudrate_code = PN532_BAUDRATE_921600;
        break;
    default:
        LOG_ERR("Unsupported baud rate: %d", baudrate);
        return false;
    }

    LOG_DBG("Sending SetSerialBaudRate command");
    pn532_tx_buffer[0] = PN532_COMMAND_SETSERIALBAUDRATE;
    pn532_tx_buffer[1] = baudrate_code;
    if (!pn532_send_command(pn532_tx_buffer, 2, 100)) {
        return false;
    }
    /* Wait for a little bit until we receive the 8 bytes: size of the SetSerialBaudRate response */
    if (!pn532_wait_for_rx(8, 100)) {
        LOG_ERR("Timeout waiting for SetSerialBaudRate response");
        return false;
    }
    // /* Read the SetSerialBaudRate response */
    uint8_t response_buf[8] = {0};
    if (ring_buf_get(&pn532_rx_ring_buffer, response_buf, 8) != 8) {
        LOG_ERR("Failed to read SetSerialBaudRate response");
        return false;
    }
    /* Verify response buffer */
    if ((response_buf[0] != 0) || (response_buf[1] != 0) || (response_buf[2] != 0xff)) {
        LOG_ERR("Preamble missing");
        return false;
    }

    if ((response_buf[5] != PN532_PN532TOHOST ) ||
        (response_buf[6] != PN532_RESPONSE_SETSERIALBAUDRATE)) {
        LOG_ERR("Invalid SetSerialBaudRate response: 0x%02X", response_buf[5]);
        return false;
    }

    /* Per datasheet: PN532 switches AFTER receiving our ACK.
     * Send ACK at 115200, then switch host side. */
    pn532_uart_send(PN532_ACK, sizeof(PN532_ACK));

    /* Wait >= 200 µs as required by the datasheet before next command */
    k_usleep(500);

    /* Switch host UART to the new baud rate */
    struct uart_config cfg = {0};
    int ret = uart_config_get(uart_dev, &cfg);
    if (ret) {
        LOG_ERR("uart_config_get failed: %d", ret);
        return false;
    }
    cfg.baudrate = baudrate;
    ret = uart_configure(uart_dev, &cfg);
    if (ret) {
        LOG_ERR("uart_configure(%d) failed: %d", baudrate, ret);
        return false;
    }
    LOG_INF("Host UART switched to %d", baudrate);

    /* Flush any garbage from the baud-rate transition */
    k_msleep(2);

    LOG_DBG("SetSerialBaudRate OK");
    return true;
}


bool pn532_get_firmware_version(struct pn532_fw_version *fw_version) {
    if (fw_version == NULL) {
        LOG_ERR("Invalid firmware version struct pointer");
        return false;
    }
    LOG_DBG("Sending GetFirmwareVersion command");
    pn532_tx_buffer[0] = PN532_COMMAND_GETFIRMWAREVERSION;
    if (!pn532_send_command(pn532_tx_buffer, 1, 1000)) {
        return false;
    }
    /* Wait for a little bit until we receive the 13 bytes of the GetFirmwareVersion response */
    if (!pn532_wait_for_rx(13, 100)) {
        LOG_ERR("Timeout waiting for GetFirmwareVersion response");
        return false;
    }
    /* Read the GetFirmwareVersion response */
    uint8_t response_buf[13] = {0};
    if (ring_buf_get(&pn532_rx_ring_buffer, response_buf, 13) != 13) {
        LOG_ERR("Failed to read GetFirmwareVersion response");
        return false;
    }
    /* Verify response buffer */
    if ((response_buf[5] != PN532_PN532TOHOST) ||
        (response_buf[6] != PN532_RESPONSE_GETFIRMWAREVERSION)) {
        LOG_ERR("Unexpected response to GetFirmwareVersion");
        return false;
    }
    if (memcmp(response_buf,
               PN532_EXPECTED_FIRMWARE_VERSION,
               sizeof(PN532_EXPECTED_FIRMWARE_VERSION)) != 0) {
        LOG_ERR("Unexpected firmware version response");
        return false;
    }

    fw_version->ic = response_buf[7];
    fw_version->ver = response_buf[8];
    fw_version->rev = response_buf[9];
    LOG_DBG("GetFirmwareVersion OK");
    return true;
}

bool pn532_in_list_passive_target() {
    LOG_DBG("Sending InListPassiveTarget command");
    pn532_tx_buffer[0] = PN532_COMMAND_INLISTPASSIVETARGET;
    pn532_tx_buffer[1] = 0x01;
    pn532_tx_buffer[2] = 0x00;
    if (!pn532_send_command(pn532_tx_buffer, 3, 1200)) {
        return false;
    }
    /* Wait for a little bit until we receive the 24 bytes of the InListPassiveTarget response */
    if (!pn532_wait_for_rx(24, 1200)) {
        LOG_ERR("Timeout waiting for InListPassiveTarget response");
        return false;
    }
    /* Read the inListPassiveTarget response */
    uint8_t response_buf[24] = {0};
    if (ring_buf_get(&pn532_rx_ring_buffer, response_buf, 24) != 24) {
        LOG_ERR("Failed to read inListPassiveTarget response");
        return false;
    }
    /* Verify response preamble */
    if ((response_buf[0] != 0) || (response_buf[1] != 0) || (response_buf[2] != 0xff)) {
        LOG_ERR("Preamble missing");
        return false;
    }
    /* Verify response length */
    uint8_t length = response_buf[3];
    if (response_buf[4] != (uint8_t)(~length + 1)) {
        LOG_ERR("Length check invalid");
        LOG_DBG("Expected: 0x%02X, Got: 0x%02X", (uint8_t)(~length + 1), response_buf[4]);
        return false;
    }
    /* Verify response code */
    if ((response_buf[5] != PN532_PN532TOHOST) ||
        (response_buf[6] != PN532_RESPONSE_INLISTPASSIVETARGET)) {
        LOG_ERR("Unexpected response to inlist passive host");
        return false;
    }
    /* Verify number of targets inlisted */
    if (response_buf[7] != 1) {
        LOG_ERR("Unhandled number of targets inlisted");
        LOG_DBG("Number of tags inlisted: 0x%02X", response_buf[7]);
        return false;
    }
    /* Save the listed tag */
    in_listed_tag = response_buf[8];
    LOG_DBG("Tag number: %d", in_listed_tag);
    LOG_DBG("InListPassiveTarget OK");
    return true;
}

bool pn532_in_data_exchange(uint8_t *send, uint8_t sendLength, uint8_t *response, uint8_t *responseLength) {
    if (send == NULL) {
        LOG_ERR("Invalid command buffer");
        return false;
    }

    if (sendLength == 0) {
        LOG_ERR("Invalid command length");
        return false;
    }

    if (response == NULL) {
        LOG_ERR("Invalid response buffer");
        return false;
    }

    if ((responseLength == NULL) || (*responseLength == 0)) {
        LOG_ERR("Invalid response length");
        return false;
    }

    LOG_DBG("Sending InDataExchange command");
    pn532_tx_buffer[0] = PN532_COMMAND_INDATAEXCHANGE;
    pn532_tx_buffer[1] = in_listed_tag;
    for (uint8_t i = 0; i < sendLength; ++i) {
        pn532_tx_buffer[i + 2] = send[i];
    }
    if (!pn532_send_command(pn532_tx_buffer, sendLength + 2, 1000)) {
        return false;
    }
    /* We can ignore the timeout here and only raise a warning
     * since the user doesn't necessarily know
     * how many bytes to expect in the response
     */
    if (!pn532_wait_for_rx(*responseLength+10, 1000)) {
        LOG_WRN("Timeout waiting for inDataExchange response, expected %d bytes but got %d bytes",
                *responseLength + 10,
                ring_buf_size_get(&pn532_rx_ring_buffer));
    }
    /* We can also ignore the length check here and only raise a warning
     * since the user doesn't necessarily know
     * how many bytes to expect in the response
     */
    uint8_t response_buf[128] = {0};
    uint32_t len = ring_buf_get(&pn532_rx_ring_buffer, response_buf, sizeof(response_buf));
    if (len != *responseLength + 10) {
        LOG_WRN("Failed to read inDataExchange response, Expected %d bytes but got %d",
                *responseLength + 10,
                len);
    }
    /* Verify response preamble */
    if ((response_buf[0] != 0) || (response_buf[1] != 0) || (response_buf[2] != 0xff)) {
        LOG_ERR("Invalid response preamble");
        return false;
    }
    /* Verify response length */
    uint8_t length = response_buf[3];
    if (response_buf[4] != (uint8_t)(~length + 1)) {
        LOG_ERR("Length check invalid");
        LOG_DBG("Expected: 0x%02X, Got: 0x%02X", (uint8_t)(~length + 1), response_buf[4]);
        return false;
    }
    /* Verify response code */
    if ((response_buf[5] != PN532_PN532TOHOST) ||
        (response_buf[6] != PN532_RESPONSE_INDATAEXCHANGE)) {
        LOG_ERR("Invalid response code");
        return false;
    }
    /* Verify status code */
    if ((response_buf[7] & 0x3f) != 0) {
        LOG_ERR("Status code indicates an error, expected 0x00 but got 0x%02X",
                response_buf[7] & 0x3f);
        return false;
    }
    /* Save response length */
    length -= 3;
    if (length > *responseLength) {
        length = *responseLength;
    }
    *responseLength = length;
    /* Save response data */
    for (uint8_t i = 0; i < length; ++i) {
        response[i] = response_buf[8 + i];
    }
    LOG_DBG("InDataExchange OK");
    return true;
}
