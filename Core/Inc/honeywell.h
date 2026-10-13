/*
 * honeywell.h - Honeywell HumidIcon (HIH6xxx / HIH8xxx) I2C driver
 */
#ifndef INC_HONEYWELL_H_
#define INC_HONEYWELL_H_

#include "main.h"

#define HW_I2C_ADDR        (0x27u << 1)
#define HW_MEAS_TIME_MS    50u      /* datasheet: 36.65 ms typ */

typedef enum {
	HW_OK = 0,      /* fresh data */
	HW_STALE,       /* sensor answered, but data not yet updated */
	HW_ERR,         /* no answer / bus error */
	HW_BUSY         /* measurement still in progress */
} hw_status_t;

typedef struct {
	float hum;      /* %RH */
	float temp;     /* degC */
} hw_data_t;

typedef struct {
	I2C_HandleTypeDef *hi2c;
	uint32_t t_req;     /* tick of measurement request */
	uint8_t  pending;
} hw_dev_t;

void        hw_init(hw_dev_t *dev, I2C_HandleTypeDef *hi2c);
hw_status_t hw_request(hw_dev_t *dev);                 /* start measurement */
hw_status_t hw_read(hw_dev_t *dev, hw_data_t *out);    /* HW_BUSY until ready */

#endif /* INC_HONEYWELL_H_ */
