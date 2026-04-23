#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(main, LOG_LEVEL_DBG);

static const struct device *const uart_dev = DEVICE_DT_GET(DT_ALIAS(pn532_uart));

void pn532_uart_send(const uint8_t *data, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        uart_poll_out(uart_dev, data[i]);
    }

    LOG_HEXDUMP_INF(data, len, "TX");
}

int pn532_uart_read(uint8_t *buf, size_t max_len, int timeout_ms)
{
    int64_t end = k_uptime_get() + timeout_ms;
    int64_t now = 0;
    size_t idx = 0;

    while (idx < max_len) {
        now = k_uptime_get();
        if (now >= end) {
            break;
        }

        uint8_t c;
        if (uart_poll_in(uart_dev, &c) == 0) {
            buf[idx++] = c;
        } else {
            k_yield();
        }
    }

    if (idx > 0) {
        LOG_HEXDUMP_INF(buf, idx, "RX");
    } else {
        LOG_WRN("RX timeout (%d ms)", timeout_ms);
    }

    return idx;
}

void pn532_uart_flush(void)
{
    uint8_t c;
    while (uart_poll_in(uart_dev, &c) == 0) {
        /* discard */
    }
}

bool pn532_is_ack(uint8_t *buf)
{
    const uint8_t ack[] = {0x00, 0x00, 0xFF, 0x00, 0xFF, 0x00};
    return memcmp(buf, ack, 6) == 0;
}

/* Send a bare ACK frame (host -> PN532) */
void pn532_send_ack(void)
{
    const uint8_t ack[] = {0x00, 0x00, 0xFF, 0x00, 0xFF, 0x00};
    pn532_uart_send(ack, sizeof(ack));
}

#ifdef CONFIG_PN532_BAUD_921600
/* Change host-side baud rate at runtime */
static int set_host_baudrate(uint32_t baud)
{
    struct uart_config cfg = {0};
    int ret = uart_config_get(uart_dev, &cfg);
    if (ret) {
        LOG_ERR("uart_config_get failed: %d", ret);
        return ret;
    }
    cfg.baudrate = baud;
    ret = uart_configure(uart_dev, &cfg);
    if (ret) {
        LOG_ERR("uart_configure(%u) failed: %d", baud, ret);
    }
    return ret;
}
#endif /* CONFIG_PN532_BAUD_921600 */

static bool send_command_get_ack(const uint8_t *cmd, size_t cmd_len, int timeout_ms)
{
    uint8_t buf[6];

    pn532_uart_send(cmd, cmd_len);

    int len = pn532_uart_read(buf, 6, timeout_ms);
    if (len != 6 || !pn532_is_ack(buf)) {
        LOG_ERR("No ACK");
        return false;
    }
    return true;
}

int main(void)
{
    uint8_t buf[64] = {0};
    int len = 0;

    if (!device_is_ready(uart_dev)) {
        LOG_ERR("UART not ready");
        return -1;
    }
    LOG_INF("UART ready (115200 baud)");

    /* ---- Wakeup ---- */
    uint8_t wakeup[] = {0x55, 0x55, 0x00, 0x00, 0x00};
    pn532_uart_send(wakeup, sizeof(wakeup));
    k_msleep(2);
    pn532_uart_flush();

    /* ---- SAMConfig ---- */
    uint8_t samconfig_cmd[] = {
        0x00, 0xFF, 0x05, 0xFB,
        0xD4, 0x14,
        0x01, 0x14, 0x01,
        0x02,
        0x00
    };

    if (!send_command_get_ack(samconfig_cmd, sizeof(samconfig_cmd), 200)) {
        LOG_ERR("SAMConfig: No ACK");
        return 0;
    }
    LOG_INF("SAMConfig ACK OK");

    memset(buf, 0, sizeof(buf));
    len = pn532_uart_read(buf, sizeof(buf), 300);
    if (len <= 0 || buf[6] != 0x15) {
        LOG_ERR("SAMConfig response failed");
        return 0;
    }
    LOG_INF("SAMConfig response OK");

#ifdef CONFIG_PN532_BAUD_921600
    LOG_INF("Configuring PN532 to 921600 baud...");
    /* ---- SetSerialBaudRate to 921600 (BR=0x07) ----
     *
     * Frame: 00 FF 03 FD D4 10 07 15 00
     *   LEN=03, LCS=FD, TFI=D4, CMD=10, BR=07, DCS=15, Postamble=00
     */
    uint8_t set_baud_cmd[] = {
        0x00, 0xFF,
        0x03, 0xFD,
        0xD4, 0x10, 0x07,
        0x15,
        0x00
    };

    if (!send_command_get_ack(set_baud_cmd, sizeof(set_baud_cmd), 200)) {
        LOG_ERR("SetSerialBaudRate: No ACK");
        return 0;
    }
    LOG_INF("SetSerialBaudRate ACK OK");

    /* Read the D5 11 response — still at 115200 */
    memset(buf, 0, sizeof(buf));
    len = pn532_uart_read(buf, sizeof(buf), 200);
    if (len <= 0) {
        LOG_ERR("SetSerialBaudRate: No response");
        return 0;
    }
    LOG_INF("SetSerialBaudRate response OK");

    /* Per datasheet: PN532 switches AFTER receiving our ACK.
     * Send ACK at 115200, then switch host side. */
    pn532_send_ack();

    /* Wait >= 200 µs as required by the datasheet before next command */
    k_usleep(500);

    /* Switch host UART to 921600 */
    if (set_host_baudrate(921600) != 0) {
        return 0;
    }
    LOG_INF("Host UART switched to 921600");

    /* Flush any garbage from the baud-rate transition */
    k_msleep(2);
    pn532_uart_flush();
#endif /* CONFIG_PN532_BAUD_921600 */

    /* ---- getFirmwareVersion ---- */
    uint8_t fw_cmd[] = {
        0x00, 0xFF, 0x02, 0xFE,
        0xD4, 0x02,
        0x2A,
        0x00
    };

    if (!send_command_get_ack(fw_cmd, sizeof(fw_cmd), 200)) {
        LOG_ERR("GetFirmwareVersion: No ACK");
        return 0;
    }
    LOG_INF("GetFirmwareVersion ACK OK");

    memset(buf, 0, sizeof(buf));
    len = pn532_uart_read(buf, sizeof(buf), 300);
    if (len <= 0) {
        LOG_ERR("GetFirmwareVersion: No response");
        return 0;
    }

    LOG_INF("Response received");
    LOG_INF("Found chip PN5%02X", buf[7]);
    LOG_INF("Firmware version: %d.%d", buf[8], buf[9]);

    while (1) {
        k_msleep(1000);
    }

    return 0;
}

void k_sys_fatal_error_handler(unsigned int reason, const struct arch_esf *esf)
{
	ARG_UNUSED(esf);
	struct k_thread *faulting_thread = NULL;

	switch (reason) {
	case K_ERR_CPU_EXCEPTION: {
		LOG_ERR("Generic CPU exception, not covered by other codes");
		break;
	}
	case K_ERR_SPURIOUS_IRQ: {
		LOG_ERR("Unhandled hardware interrupt");
		break;
	}
	case K_ERR_STACK_CHK_FAIL: {
		LOG_ERR("Faulting context overflowed its stack buffer");
		/* Get the current thread that caused the fault */
		faulting_thread = k_current_get();
		if (faulting_thread) {
			LOG_ERR("Fault occurred in thread: %s", k_thread_name_get(faulting_thread));
			LOG_ERR("Thread ID: %p", (void *)faulting_thread);
			LOG_ERR("Stack start: %p, size: %zu",
				(void *)faulting_thread->stack_info.start,
				faulting_thread->stack_info.size);
		} else {
			LOG_ERR("Could not determine faulting thread");
		}
		break;
	}
	case K_ERR_KERNEL_OOPS: {
		LOG_ERR("Moderate severity software error");
		break;
	}
	case K_ERR_KERNEL_PANIC: {
		LOG_ERR("High severity software error");
		break;
	}
	case K_ERR_ARCH_START: {
		LOG_ERR("Arch specific fatal errors");
		break;
	}
	default: {
		LOG_ERR("Unknow reason for fatal error (%d)", reason);
		break;
	}
	}

	/* Disable interrupts and halt the system */
	arch_irq_lock();
	for (;;) { /* Spin endlessly */ }
}
