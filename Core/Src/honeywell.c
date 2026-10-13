/*
 * honeywell.c - Honeywell HumidIcon (HIH6xxx / HIH8xxx) I2C driver
 * Protocol: Honeywell TN 009061-2 "I2C Communication with HumidIcon".
 */
#include "honeywell.h"

#define HW_I2C_TIMEOUT_MS 5u

static void bus_recover(I2C_HandleTypeDef *hi2c)
{
	/* re-init peripheral, clears stuck BUSY flag */
	HAL_I2C_DeInit(hi2c);
	HAL_I2C_Init(hi2c);
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
