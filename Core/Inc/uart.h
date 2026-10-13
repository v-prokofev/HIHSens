/*
 * uart.h - USART1 (PA9 TX / PA10 RX) with DMA, register level
 * (HAL UART driver is not in the project, so no CubeMX changes needed)
 */
#ifndef INC_UART_H_
#define INC_UART_H_

#include "main.h"

void uart_init(uint32_t baud);
void uart_set_baud(uint32_t baud);   /* waits for TX to finish */
void uart_send(const char *s);       /* waits for previous DMA transfer */
int  uart_getc(void);                /* -1 if no data */

#endif /* INC_UART_H_ */
