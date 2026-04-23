#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(test, LOG_LEVEL_INF);

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

static const struct device *uart_dev = DEVICE_DT_GET(DT_ALIAS(pn532_uart));

static uint8_t rx_buf[256] = {0};
static volatile size_t rx_len = 0;

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

    if (uart_irq_rx_ready(dev)) {
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

    /* Wait for ACK (we’re willing to wait 200 ms for ack) */
    if (!wait_for_rx(sizeof(ack), timeout_ms)) {
        LOG_ERR("Timeout waiting for ACK");
        return false;
    }

    if (memcmp(rx_buf, ack, sizeof(ack)) != 0) {
        LOG_ERR("Invalid ACK");
        return false;
    }

    LOG_DBG("ACK received");

    /* Did the response already start arriving right after ACK? */
    if (rx_len > sizeof(ack)) {
        return true;
    }

    /* Otherwise wait for a little bit more */
    int64_t end = k_uptime_get() + timeout_ms;
    while (k_uptime_get() < end) {
        if (rx_len > sizeof(ack)) {
            LOG_DBG("Response received");
            return true;
        }
        k_sleep(K_USEC(100));
    }

    LOG_ERR("Timeout waiting for response");
    return false;
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
    LOG_INF("Sending wakeup command");
    uart_send(wakeup_cmd, sizeof(wakeup_cmd));
#if !defined(CONFIG_BOARD_NATIVE_SIM)
    k_msleep(2);
#endif

    /* ---- SAMConfig ---- */
    LOG_INF("Sending SAMConfig command");
    pn532_packetbuffer[0] = PN532_COMMAND_SAMCONFIGURATION;
    pn532_packetbuffer[1] = 0x01; // normal mode;
    pn532_packetbuffer[2] = 0x14; // timeout 50ms * 20 = 1 second
    pn532_packetbuffer[3] = 0x01; // use IRQ pin!
    if (!pn532_send_command(pn532_packetbuffer, 4, 100)) {
        LOG_ERR("SAMConfig failed");
        return 0;
    }
    /* Check SAMConfig response manually */
    /* Wait for a little bit until we receive the 15 bytes of the response */
    int64_t end = k_uptime_get() + 100;
    while (k_uptime_get() < end) {
        LOG_DBG("rx_len=%d, waiting for SAMConfig response...", rx_len);
        if (rx_len >= 15) {
            LOG_DBG("Response received");
            break;
        }
        k_sleep(K_USEC(100));
    }

    if (rx_buf[12] != 0x15) {
        LOG_ERR("Invalid SAMConfig response: 0x%02X", rx_buf[12]);
        return 0;
    }

    LOG_INF("SAMConfig OK");

    /* ---- GetFirmwareVersion ---- */
    LOG_INF("Sending GetFirmwareVersion");
    pn532_packetbuffer[0] = PN532_COMMAND_GETFIRMWAREVERSION;
    if (!pn532_send_command(pn532_packetbuffer, 1, 100)) {
        LOG_ERR("GetFirmwareVersion failed");
        return 0;
    }
    /* Wait for a little bit until we receive the 19 bytes of the response */
    end = k_uptime_get() + 100;
    while (k_uptime_get() < end) {
        LOG_DBG("rx_len=%d, waiting for GetFirmwareVersion response...", rx_len);
        if (rx_len >= 19) {
            LOG_DBG("Response received");
            break;
        }
        k_sleep(K_USEC(100));
    }
    /* Parse firmware response manually */
    if (rx_buf[12] != 0x03) {
        LOG_ERR("Unexpected response code: 0x%02X", rx_buf[12]);
        return 0;
    }
    LOG_INF("GetFirmwareVersion OK");
    const uint8_t ic  = rx_buf[13];
    const uint8_t ver = rx_buf[14];
    const uint8_t rev = rx_buf[15];
    LOG_INF("Found PN5%02X", ic);
    LOG_INF("Firmware version: %d.%d", ver, rev);

    /* ---- InListPassiveTarget ---- */
    LOG_INF("Sending InListPassiveTarget command");
    pn532_packetbuffer[0] = PN532_COMMAND_INLISTPASSIVETARGET;
    pn532_packetbuffer[1] = 0x01;
    pn532_packetbuffer[2] = 0x00;
    while (1) {
        if (!pn532_send_command(pn532_packetbuffer, 3, 1000)) {
            LOG_DBG("Nothing detected, retrying...");
            k_msleep(100);
            continue;
        } else {
            LOG_INF("InListPassiveTarget OK");
            break;
        }
    }

    /* ---- InDataExchange ---- */
    LOG_INF("Sending InDataExchange command");
    pn532_packetbuffer[0] = PN532_COMMAND_INDATAEXCHANGE;
    pn532_packetbuffer[1] = 1; // Target number (only one target supported in this example)
    for (int i = 0; i < sizeof(selectApduCmd); ++i) {
        pn532_packetbuffer[i + 2] = selectApduCmd[i];
    }
    if (!pn532_send_command(pn532_packetbuffer, sizeof(selectApduCmd) + 2, 1000)) {
        LOG_ERR("SELECT APDU failed");
        return 0;
    }
    LOG_INF("InDataExchange OK");
    LOG_INF("SELECT APDU successful...");

    while (1)
    {
        k_msleep(100);
    }

    return 0;
}