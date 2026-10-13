/*
 * uart.c - USART1 with DMA1 Ch4 (TX) and DMA1 Ch5 (RX, circular).
 * RX is polled from the circular buffer, no interrupts used.
 */
#include "uart.h"
#include <string.h>

#define RX_SIZE 128
#define TX_SIZE 256

static uint8_t rx_buf[RX_SIZE];
static uint8_t tx_buf[TX_SIZE];
static uint16_t rx_tail;

static int tx_busy(void)
{
	return (DMA1_Channel4->CCR & DMA_CCR_EN) && !(DMA1->ISR & DMA_ISR_TCIF4);
}

static void tx_wait(void)
{
	uint32_t to = 1000000;
	while (tx_busy() && --to) {}
}

void uart_set_baud(uint32_t baud)
{
	tx_wait();
	uint32_t to = 100000;
	while (!(USART1->SR & USART_SR_TC) && --to) {}
	USART1->CR1 &= ~USART_CR1_UE;
	USART1->BRR = (HAL_RCC_GetPCLK2Freq() + baud / 2) / baud;
	USART1->CR1 |= USART_CR1_UE;
}

void uart_init(uint32_t baud)
{
	RCC->APB2ENR |= RCC_APB2ENR_USART1EN | RCC_APB2ENR_IOPAEN | RCC_APB2ENR_AFIOEN;
	RCC->AHBENR  |= RCC_AHBENR_DMA1EN;
	(void)RCC->AHBENR;

	/* PA9: AF push-pull 50 MHz, PA10: input pull-up */
	GPIOA->CRH = (GPIOA->CRH & ~(0xFFu << 4)) | (0xBu << 4) | (0x8u << 8);
	GPIOA->ODR |= (1u << 10);

	USART1->CR1 = 0;
	USART1->BRR = (HAL_RCC_GetPCLK2Freq() + baud / 2) / baud;
	USART1->CR3 = USART_CR3_DMAR | USART_CR3_DMAT;

	/* RX: DMA1 Ch5, periph->mem, circular */
	DMA1_Channel5->CCR   = 0;
	DMA1_Channel5->CPAR  = (uint32_t)&USART1->DR;
	DMA1_Channel5->CMAR  = (uint32_t)rx_buf;
	DMA1_Channel5->CNDTR = RX_SIZE;
	DMA1_Channel5->CCR   = DMA_CCR_MINC | DMA_CCR_CIRC | DMA_CCR_EN;
	rx_tail = 0;

	/* TX: DMA1 Ch4, mem->periph */
	DMA1_Channel4->CCR  = 0;
	DMA1_Channel4->CPAR = (uint32_t)&USART1->DR;

	USART1->CR1 = USART_CR1_UE | USART_CR1_TE | USART_CR1_RE;
}

void uart_send(const char *s)
{
	while (*s) {
		uint32_t to = 100000;
		while (!(USART1->SR & USART_SR_TXE) && --to) {}
		USART1->DR = (uint8_t)(*s++);
	}
	uint32_t to = 100000;
	while (!(USART1->SR & USART_SR_TC) && --to) {}
}

int uart_getc(void)
{
	if (USART1->SR & (USART_SR_ORE | USART_SR_NE | USART_SR_FE | USART_SR_PE)) {
		volatile uint32_t sr = USART1->SR;
		volatile uint32_t dr = USART1->DR;
		(void)sr;
		(void)dr;
	}

	if (USART1->SR & USART_SR_RXNE) {
		return (uint8_t)(USART1->DR & 0xFF);
	}
	return -1;
}
