#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(test, LOG_LEVEL_DBG);

#define PN532_PREAMBLE (0x00)   ///< Command sequence start, byte 1/3
#define PN532_STARTCODE1 (0x00) ///< Command sequence start, byte 2/3
#define PN532_STARTCODE2 (0xFF) ///< Command sequence start, byte 3/3
#define PN532_POSTAMBLE (0x00)  ///< EOD

#define PN532_HOSTTOPN532 (0xD4) ///< Host-to-PN532
#define PN532_PN532TOHOST (0xD5) ///< PN532-to-host

#define PN532_PACKBUFFSIZ 64                ///< Packet buffer size in bytes
uint8_t pn532_packetbuffer[PN532_PACKBUFFSIZ]; ///< Packet buffer used in various
                                            ///< transactions


// PN532 Commands
#define PN532_COMMAND_GETFIRMWAREVERSION (0x02)    ///< Get firmware version
#define PN532_COMMAND_SAMCONFIGURATION (0x14)      ///< SAM configuration
#define PN532_COMMAND_INLISTPASSIVETARGET (0x4A)   ///< List passive target
#define PN532_COMMAND_INDATAEXCHANGE (0x40)        ///< Data exchange
#define PN532_RESPONSE_INLISTPASSIVETARGET (0x4B) ///< List passive target
#define PN532_RESPONSE_INDATAEXCHANGE (0x41)      ///< Data exchange

struct pn532_fw_version
{
    uint8_t ic;  ///< PN5xx IC type (e.g. 0x32 for PN532)
    uint8_t ver; ///< Firmware version
    uint8_t rev; ///< Firmware revision
};

static const 
uint8_t pn532response_firmwarevers[] = {
    0x00, 0x00, 0xFF,
    0x06, 0xFA, 0xD5}; ///< Expected firmware version message from PN532

static const struct device *uart_dev = DEVICE_DT_GET(DT_ALIAS(pn532_uart));

static uint8_t rx_buf[256] = {0};
static volatile size_t rx_len = 0;
static int8_t _inListedTag; // Tg number of inlisted tag.

static const uint8_t ack[] = {
    0x00, /* Preamble */
    0x00, /* Start code 1 */
    0xFF, /* Start code 2 */
    0x00, /* LEN  = 0 (no payload) */
    0xFF, /* LCS  = 0x100 - LEN = 0x100 - 0x00 = 0xFF */
    0x00  /* Postamble */
};

static const uint8_t wakeup_cmd[] = {
    0x55, /* Dummy byte (wakeup pattern, generates clock edges) */
    0x55, /* Dummy byte (ensures PN532 exits power-down) */
    0x00, /* Preamble (start of a "fake" frame) */
    0x00, /* Start code 1 */
    0x00  /* (Not a valid frame, just padding / noise) */
};

  /*
  * SELECT APDU (AID selection)
  * AID = F123456789ABCDE1 (8 bytes)
  */
static const  uint8_t selectApduCmd[] = {
    0x00,  // CLA
    0xA4,  // INS (SELECT)
    0x04,  // P1 (select by AID)
    0x00,  // P2
    0x08,  // Lc: length = 8 bytes
    0xF1, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xE1, // AID (F123456789ABCDE1)
    0x00  // Le
};

/* UART interrupt callback */
static void uart_cb(const struct device *dev, void *user_data)
{
    ARG_UNUSED(user_data);

    if (!uart_irq_update(dev)) {
        return;
    }

    if (!uart_irq_rx_ready(dev)) {
        return;
    }

    if (rx_len >= sizeof(rx_buf)) {
        LOG_ERR("RX buffer overflow");
        rx_len = 0;
        return;
    }

    int len = uart_fifo_read(dev, rx_buf + rx_len, sizeof(rx_buf) - rx_len);
    if (len > 0) {
        rx_len += len;
        LOG_HEXDUMP_DBG(rx_buf, rx_len, "RX");
    }
}

static void uart_send(const uint8_t *data, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        uart_poll_out(uart_dev, data[i]);
    }

    LOG_HEXDUMP_DBG(data, len, "TX");
}

static bool wait_for_rx(size_t expected_len, int timeout_ms)
{
    int64_t end = k_uptime_get() + timeout_ms;

    while (k_uptime_get() < end) {
        if (rx_len >= expected_len) {
            return true;
        }
        k_sleep(K_USEC(100));
    }

    return false;
}

/**************************************************************************/
/*!
    @brief  Writes a command to the PN532, automatically inserting the
            preamble and required frame details (checksum, len, etc.)

    @param  cmd       Pointer to the command buffer
    @param  cmdlen    Command length in bytes
*/
/**************************************************************************/
void writecommand(uint8_t *cmd, uint8_t cmdlen) {
    uint8_t packet[8 + cmdlen];
    uint8_t LEN = cmdlen + 1;

    packet[0] = PN532_PREAMBLE;
    packet[1] = PN532_STARTCODE1;
    packet[2] = PN532_STARTCODE2;
    packet[3] = LEN;
    packet[4] = ~LEN + 1;
    packet[5] = PN532_HOSTTOPN532;
    uint8_t sum = 0;
    for (uint8_t i = 0; i < cmdlen; i++) {
      packet[6 + i] = cmd[i];
      sum += cmd[i];
    }
    packet[6 + cmdlen] = ~(PN532_HOSTTOPN532 + sum) + 1;
    packet[7 + cmdlen] = PN532_POSTAMBLE;

    uart_send(packet, 8 + cmdlen);
}

static bool pn532_send_command(const uint8_t *cmd, size_t cmd_len, int timeout_ms)
{
    /* Reset RX buffer */
    rx_len = 0;

    /* Send command */
    writecommand((uint8_t *)cmd, cmd_len);

    /* Wait for ACK */
    if (!wait_for_rx(sizeof(ack), timeout_ms)) {
        LOG_ERR("Timeout waiting for ACK");
        return false;
    }

    /* Check if the received data matches the expected ACK */
    if (memcmp(rx_buf, ack, sizeof(ack)) != 0) {
        LOG_ERR("Invalid ACK");
        return false;
    }
    LOG_DBG("ACK received");

    /* Did the response already start arriving right after ACK? */
    if (rx_len > sizeof(ack)) {
        return true;
    }

    /* Otherwise wait for a little bit more for the response to arrive after ACK */
    int64_t end = k_uptime_get() + timeout_ms;
    while (k_uptime_get() < end) {
        if (rx_len > sizeof(ack)) {
            LOG_DBG("Response received");
            return true;
        }
        k_sleep(K_USEC(100));
    }

    LOG_ERR("Timeout waiting for response after the ack");
    return false;
}

/**************************************************************************/
/*!
    @brief  Wakeup from LowVbat mode into Normal Mode.
*/
/**************************************************************************/
void wakeup(void) {
    LOG_INF("Sending wakeup command");
    uart_send(wakeup_cmd, sizeof(wakeup_cmd));
#if !defined(CONFIG_BOARD_NATIVE_SIM)
    k_msleep(2);
#endif
}

/**************************************************************************/
/*!
    @brief   Configures the SAM (Secure Access Module)
    @return  true on success, false otherwise.
*/
/**************************************************************************/
bool SAMConfig(void) {
    LOG_INF("Sending SAMConfig command");
    pn532_packetbuffer[0] = PN532_COMMAND_SAMCONFIGURATION;
    pn532_packetbuffer[1] = 0x01; // normal mode;
    pn532_packetbuffer[2] = 0x14; // timeout 50ms * 20 = 1 second
    pn532_packetbuffer[3] = 0x01; // use IRQ pin!
    if (!pn532_send_command(pn532_packetbuffer, 4, 100)) {
        LOG_ERR("SAMConfig failed");
        return false;
    }
    /* Wait for a little bit until we receive the 15 bytes of the SAMConfig response */
    if (!wait_for_rx(15, 100)) {
        LOG_ERR("Timeout waiting for SAMConfig response");
        return false;
    }
    int offset = sizeof(ack);
    if (rx_buf[offset] != 0 || rx_buf[offset + 1] != 0 || rx_buf[offset + 2] != 0xff) {
        LOG_ERR("Preamble missing");
        return false;
    }
    /* I don't know what is byte 4 and 5 used for */
    if (rx_buf[offset + 6] != 0x15) {
        LOG_ERR("Invalid SAMConfig response: 0x%02X", rx_buf[offset + 6]);
        return false;
    }
    LOG_INF("SAMConfig OK");
    return true;
}

/**************************************************************************/
/*!
    @brief  Checks the firmware version of the PN5xx chip

    @returns  The chip's firmware version and ID
*/
/**************************************************************************/
bool getFirmwareVersion(struct pn532_fw_version *fw_version) {
    LOG_INF("Sending GetFirmwareVersion command");
    pn532_packetbuffer[0] = PN532_COMMAND_GETFIRMWAREVERSION;
    if (!pn532_send_command(pn532_packetbuffer, 1, 100)) {
        LOG_ERR("GetFirmwareVersion failed");
        return false;
    }
    /* Wait for a little bit until we receive the 19 bytes of the GetFirmwareVersion response */
    if (!wait_for_rx(19, 100)) {
        LOG_ERR("Timeout waiting for GetFirmwareVersion response");
        return false;
    }
    if (rx_buf[sizeof(ack) + 5] != PN532_PN532TOHOST || rx_buf[sizeof(ack) + 6] != 0x03) {
        LOG_ERR("Unexpected response to GetFirmwareVersion");
        return false;
    }

    if (memcmp(rx_buf + sizeof(ack),
               pn532response_firmwarevers,
               sizeof(pn532response_firmwarevers)) != 0) {
        LOG_ERR("Unexpected firmware version");
        return false;
    }

    fw_version->ic = rx_buf[sizeof(ack) + 7];
    fw_version->ver = rx_buf[sizeof(ack) + 8];
    fw_version->rev = rx_buf[sizeof(ack) + 9];
    LOG_INF("GetFirmwareVersion OK");
    return true;
}

/**************************************************************************/
/*!
    @brief   'InLists' a passive target. PN532 acting as reader/initiator,
             peer acting as card/responder.
    @return  true on success, false otherwise.
*/
/**************************************************************************/
bool inListPassiveTarget() {
    LOG_INF("Sending InListPassiveTarget command");
    pn532_packetbuffer[0] = PN532_COMMAND_INLISTPASSIVETARGET;
    pn532_packetbuffer[1] = 0x01;
    pn532_packetbuffer[2] = 0x00;
    if (!pn532_send_command(pn532_packetbuffer, 3, 1000)) {
        LOG_DBG("Nothing detected, retrying...");
        return false;
    }
    LOG_INF("Detected something! Processing response...");
    uint8_t offset = sizeof(ack);
    if ((rx_buf[offset] == 0) && (rx_buf[offset + 1] == 0) && (rx_buf[offset + 2] == 0xff)) {
        uint8_t length = rx_buf[offset + 3];
        if (rx_buf[offset + 4] != (uint8_t)(~length + 1)) {
            LOG_ERR("Length check invalid");
            LOG_DBG("Expected: 0x%02X, Got: 0x%02X", (uint8_t)(~length + 1), rx_buf[offset + 4]);
            return false;
        }
        if (rx_buf[offset + 5] == PN532_PN532TOHOST &&
            rx_buf[offset + 6] == PN532_RESPONSE_INLISTPASSIVETARGET) {
            if (rx_buf[offset + 7] != 1) {
                LOG_ERR("Unhandled number of targets inlisted");
                LOG_DBG("Number of tags inlisted: 0x%02X", rx_buf[offset + 7]);
                return false;
            }
            _inListedTag = rx_buf[offset + 8];
            LOG_DBG("Tag number: %d", _inListedTag);
            LOG_INF("InListPassiveTarget OK");
            return true;
        }
        LOG_ERR("Unexpected response to inlist passive host");
        return false;
    } else {
        LOG_ERR("Preamble missing");
        return false;
    }
}

/**************************************************************************/
/*!
    @brief   Exchanges an APDU with the currently inlisted peer

    @param   send            Pointer to data to send
    @param   sendLength      Length of the data to send
    @param   response        Pointer to response data
    @param   responseLength  Pointer to the response data length
    @return  true on success, false otherwise.
*/
/**************************************************************************/
bool inDataExchange(uint8_t *send, uint8_t sendLength, uint8_t *response, uint8_t *responseLength) {
    LOG_INF("Sending InDataExchange command");
    pn532_packetbuffer[0] = PN532_COMMAND_INDATAEXCHANGE;
    pn532_packetbuffer[1] = 1; // Target number (only one target supported in this example)
    for (int i = 0; i < sizeof(selectApduCmd); ++i) {
        pn532_packetbuffer[i + 2] = selectApduCmd[i];
    }
    if (!pn532_send_command(pn532_packetbuffer, sizeof(selectApduCmd) + 2, 1000)) {
        LOG_ERR("inDataExchange command failed");
        return false;
    }
    int offset = sizeof(ack);
    if (rx_buf[offset] == 0 && rx_buf[offset + 1] == 0 && rx_buf[offset + 2] == 0xff) {
        uint8_t length = rx_buf[offset + 3];
        if (rx_buf[offset + 4] != (uint8_t)(~length + 1)) {
            LOG_ERR("Length check invalid");
            LOG_DBG("Expected: 0x%02X, Got: 0x%02X", (uint8_t)(~length + 1), rx_buf[offset + 4]);
            return false;
        }
        if (rx_buf[offset + 5] == PN532_PN532TOHOST &&
            rx_buf[offset + 6] == PN532_RESPONSE_INDATAEXCHANGE) {
            if ((rx_buf[offset + 7] & 0x3f) != 0) {
                LOG_ERR("Status code indicates an error");
                return false;
            }

            length -= 3;
            if (length > *responseLength) {
                length = *responseLength; // silent truncation...
            }

            for (int i = 0; i < length; ++i) {
                response[i] = rx_buf[offset + 8 + i];
            }
            *responseLength = length;
            return true;
        } else {
            LOG_ERR("Don't know how to handle this command");
            return false;
        }
    } else {
        LOG_ERR("Preamble missing");
        return false;
    }
}

int main(void)
{
    if (!device_is_ready(uart_dev)) {
        LOG_ERR("UART not ready");
        return 0;
    }

    LOG_INF("PN532 test started");

    uart_irq_callback_user_data_set(uart_dev, uart_cb, NULL);
    uart_irq_rx_enable(uart_dev);

    /* ---- Wakeup ---- */
    wakeup();

    /* ---- SAMConfig ---- */
    if (!SAMConfig()) {
        LOG_ERR("SAMConfig failed");
        return 0;
    }

    /* ---- GetFirmwareVersion ---- */
    struct pn532_fw_version fw_version = {0};
    if (!getFirmwareVersion(&fw_version)) {
        LOG_ERR("GetFirmwareVersion failed");
        return 0;
    }
    LOG_INF("Found PN5%02X", fw_version.ic);
    LOG_INF("Firmware version: %d.%d", fw_version.ver, fw_version.rev);

    /* ---- InListPassiveTarget ---- */
    while (1)
    {
        if (inListPassiveTarget()) {
            break;
        }
        LOG_DBG("No card detected, retrying...");
        k_msleep(500);
    }
    LOG_INF("InListPassiveTarget successful...");

    /* ---- InDataExchange ---- */
    uint8_t response[128] = {0};
    uint8_t responseLength = sizeof(response);
    if (!inDataExchange((uint8_t *)selectApduCmd, sizeof(selectApduCmd), response, &responseLength)) {
        LOG_ERR("inDataExchange failed");
        return 0;
    }
    LOG_INF("InDataExchange OK");
    LOG_DBG("Received response (%d bytes):", responseLength);
    LOG_HEXDUMP_DBG(response, responseLength, "Response");
    LOG_INF("SELECT APDU successful...");

    while (1)
    {
        k_msleep(100);
    }

    return 0;
}