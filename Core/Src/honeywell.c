/*
 * honeywell.c - Honeywell HumidIcon (HIH6xxx / HIH8xxx) I2C driver
 * Protocol: Honeywell TN 009061-2 "I2C Communication with HumidIcon".
 */
#include "honeywell.h"

#define HW_I2C_TIMEOUT_MS 25u

void i2c_bus_unlock(I2C_HandleTypeDef *hi2c)
{
	GPIO_TypeDef *gpio = GPIOB;
	uint16_t scl = (hi2c->Instance == I2C1) ? GPIO_PIN_6 : GPIO_PIN_10;
	uint16_t sda = (hi2c->Instance == I2C1) ? GPIO_PIN_7 : GPIO_PIN_11;

	/* 1. Force peripheral SWRST */
	hi2c->Instance->CR1 |= I2C_CR1_SWRST;
	for (volatile int i = 0; i < 2000; i++) {}
	hi2c->Instance->CR1 &= ~I2C_CR1_SWRST;

	/* 2. Switch SCL/SDA to Output Open Drain to clock out any stuck slave */
	GPIO_InitTypeDef g = {0};
	g.Pin = scl | sda;
	g.Mode = GPIO_MODE_OUTPUT_OD;
	g.Speed = GPIO_SPEED_FREQ_HIGH;
	HAL_GPIO_Init(gpio, &g);

	HAL_GPIO_WritePin(gpio, sda, GPIO_PIN_SET);
	HAL_GPIO_WritePin(gpio, scl, GPIO_PIN_SET);
	for (volatile int i = 0; i < 500; i++) {}

	/* 9 clock cycles on SCL */
	for (int i = 0; i < 9; i++) {
		HAL_GPIO_WritePin(gpio, scl, GPIO_PIN_RESET);
		for (volatile int j = 0; j < 500; j++) {}
		HAL_GPIO_WritePin(gpio, scl, GPIO_PIN_SET);
		for (volatile int j = 0; j < 500; j++) {}
	}

	/* Generate STOP: SDA 0 -> SCL 1 -> SDA 1 */
	HAL_GPIO_WritePin(gpio, sda, GPIO_PIN_RESET);
	for (volatile int j = 0; j < 500; j++) {}
	HAL_GPIO_WritePin(gpio, scl, GPIO_PIN_SET);
	for (volatile int j = 0; j < 500; j++) {}
	HAL_GPIO_WritePin(gpio, sda, GPIO_PIN_SET);
	for (volatile int j = 0; j < 500; j++) {}

	/* 3. Switch back to Alternate Function Open Drain */
	g.Mode = GPIO_MODE_AF_OD;
	HAL_GPIO_Init(gpio, &g);

	/* 4. Re-init I2C peripheral */
	HAL_I2C_Init(hi2c);
}

static void bus_recover(I2C_HandleTypeDef *hi2c)
{
	i2c_bus_unlock(hi2c);
}

void hw_init(hw_dev_t *dev, I2C_HandleTypeDef *hi2c)
{
	dev->hi2c = hi2c;
	dev->pending = 0;
	dev->t_req = 0;
}

hw_status_t hw_request(hw_dev_t *dev)
{
	/* Measurement Request = START + addr(W) + STOP, no data bytes */
	if (HAL_I2C_IsDeviceReady(dev->hi2c, HW_I2C_ADDR, 1, HW_I2C_TIMEOUT_MS) != HAL_OK) {
		bus_recover(dev->hi2c);
		dev->pending = 0;
		return HW_ERR;
	}
	dev->t_req = HAL_GetTick();
	dev->pending = 1;
	return HW_OK;
}

hw_status_t hw_read(hw_dev_t *dev, hw_data_t *out)
{
	uint8_t b[4];

	if (!dev->pending) return HW_ERR;
	if (HAL_GetTick() - dev->t_req < HW_MEAS_TIME_MS) return HW_BUSY;
	dev->pending = 0;

	if (HAL_I2C_Master_Receive(dev->hi2c, HW_I2C_ADDR, b, 4, HW_I2C_TIMEOUT_MS) != HAL_OK) {
		bus_recover(dev->hi2c);
		return HW_ERR;
	}

	uint8_t  status = b[0] >> 6;                       /* 0 normal, 1 stale, 2 cmd mode */
	uint16_t rh_raw = ((uint16_t)(b[0] & 0x3F) << 8) | b[1];
	uint16_t t_raw  = ((uint16_t)b[2] << 6) | (b[3] >> 2);

	if (status == 2 || status == 3) return HW_ERR;

	out->hum  = rh_raw * 100.0f / 16382.0f;
	out->temp = t_raw * 165.0f / 16382.0f - 40.0f;

	return (status == 1) ? HW_STALE : HW_OK;
}
