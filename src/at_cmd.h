#ifndef AT_CMD_H_
#define AT_CMD_H_

#include <zephyr/device.h>

typedef void (*at_cmd_handler_t)(const char *args);

/**
 * Register an AT command handler.
 *
 * @param cmd     Command prefix to match (e.g. "AT+ROLE"). Case-insensitive.
 *                Use "AT" alone for the bare AT test command.
 * @param handler Callback invoked with everything after the prefix as args.
 *                For "AT+ROLE=INITIATOR" with prefix "AT+ROLE", args is "=INITIATOR".
 */
void at_cmd_register(const char *cmd, at_cmd_handler_t handler);

/**
 * Send a response string over the AT UART, terminated with \r\n.
 */
void at_cmd_respond(const char *response);

/**
 * Initialize the AT command interface on the given UART.
 * Starts the processing thread internally.
 */
void at_cmd_init(const struct device *uart_dev);

/**
 * Reconfigure the AT UART to a new baudrate.
 * Returns 0 on success.
 */
int at_cmd_set_baudrate(uint32_t baud);

#endif /* AT_CMD_H_ */
