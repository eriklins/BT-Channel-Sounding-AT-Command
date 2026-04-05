#include "at_cmd.h"

#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(at_cmd, LOG_LEVEL_INF);

#define AT_CMD_MAX_LEN     256
#define AT_CMD_QUEUE_DEPTH 4
#define AT_CMD_MAX_CMDS    16

struct at_cmd_entry {
	const char *cmd;
	at_cmd_handler_t handler;
};

static const struct device *at_uart;
static struct at_cmd_entry cmd_table[AT_CMD_MAX_CMDS];
static int cmd_count;

/* Line buffer filled in ISR context */
static char rx_buf[AT_CMD_MAX_LEN];
static int rx_pos;

/* Message queue to pass complete lines to the processing thread */
K_MSGQ_DEFINE(at_cmd_msgq, AT_CMD_MAX_LEN, AT_CMD_QUEUE_DEPTH, 1);

#define AT_CMD_THREAD_STACK_SIZE 2048
#define AT_CMD_THREAD_PRIORITY   7
static K_THREAD_STACK_DEFINE(at_cmd_stack, AT_CMD_THREAD_STACK_SIZE);
static struct k_thread at_cmd_thread_data;

void at_cmd_register(const char *cmd, at_cmd_handler_t handler)
{
	if (cmd_count >= AT_CMD_MAX_CMDS) {
		LOG_ERR("AT command table full");
		return;
	}
	cmd_table[cmd_count].cmd = cmd;
	cmd_table[cmd_count].handler = handler;
	cmd_count++;
}

void at_cmd_respond(const char *response)
{
	for (const char *p = response; *p; p++) {
		uart_poll_out(at_uart, *p);
	}
	uart_poll_out(at_uart, '\r');
	uart_poll_out(at_uart, '\n');
}

static void dispatch(char *line)
{
	/* Strip leading/trailing whitespace */
	while (*line == ' ' || *line == '\t') {
		line++;
	}
	size_t len = strlen(line);
	while (len > 0 && (line[len - 1] == ' ' || line[len - 1] == '\t')) {
		line[--len] = '\0';
	}

	if (len == 0) {
		return;
	}

	LOG_DBG("AT cmd: \"%s\"", line);

	/* Try registered commands, longest prefix match first.
	 * Commands are checked in registration order; register more specific ones first
	 * or we rely on prefix length comparison.
	 */
	int best_idx = -1;
	size_t best_len = 0;

	for (int i = 0; i < cmd_count; i++) {
		size_t cmd_len = strlen(cmd_table[i].cmd);

		if (cmd_len > len) {
			continue;
		}
		if (strncasecmp(line, cmd_table[i].cmd, cmd_len) != 0) {
			continue;
		}
		/* For exact "AT" match, ensure no alphanumeric follows (avoid matching "AT+..." with "AT") */
		if (cmd_len == 2 && len > 2 && (isalpha((unsigned char)line[2]) || line[2] == '+')) {
			continue;
		}
		if (cmd_len > best_len) {
			best_len = cmd_len;
			best_idx = i;
		}
	}

	if (best_idx >= 0) {
		const char *args = line + best_len;
		cmd_table[best_idx].handler(args);
	} else {
		at_cmd_respond("ERROR");
	}
}

static void at_cmd_thread(void *p1, void *p2, void *p3)
{
	char line[AT_CMD_MAX_LEN];

	while (true) {
		k_msgq_get(&at_cmd_msgq, line, K_FOREVER);
		dispatch(line);
	}
}

static void uart_isr(const struct device *dev, void *user_data)
{
	ARG_UNUSED(user_data);

	while (uart_irq_update(dev) && uart_irq_is_pending(dev)) {
		if (!uart_irq_rx_ready(dev)) {
			continue;
		}

		uint8_t c;
		int ret = uart_fifo_read(dev, &c, 1);

		if (ret != 1) {
			continue;
		}

		if (c == '\r' || c == '\n') {
			if (rx_pos > 0) {
				rx_buf[rx_pos] = '\0';
				k_msgq_put(&at_cmd_msgq, rx_buf, K_NO_WAIT);
				rx_pos = 0;
			}
		} else if (rx_pos < AT_CMD_MAX_LEN - 1) {
			rx_buf[rx_pos++] = c;
		}
	}
}

void at_cmd_init(const struct device *uart_dev)
{
	at_uart = uart_dev;

	uart_irq_callback_set(uart_dev, uart_isr);
	uart_irq_rx_enable(uart_dev);

	k_thread_create(&at_cmd_thread_data, at_cmd_stack,
			K_THREAD_STACK_SIZEOF(at_cmd_stack),
			at_cmd_thread, NULL, NULL, NULL,
			AT_CMD_THREAD_PRIORITY, 0, K_NO_WAIT);
	k_thread_name_set(&at_cmd_thread_data, "at_cmd");

	LOG_INF("AT command interface ready");
}

int at_cmd_set_baudrate(uint32_t baud)
{
	struct uart_config cfg;
	int err = uart_config_get(at_uart, &cfg);

	if (err) {
		return err;
	}

	cfg.baudrate = baud;

	uart_irq_rx_disable(at_uart);
	err = uart_configure(at_uart, &cfg);
	uart_irq_rx_enable(at_uart);

	if (err) {
		LOG_ERR("uart_configure failed (err %d)", err);
	}

	return err;
}
