/*
 * app.h - HMP556 application (main loop, menu, settings)
 */
#ifndef INC_APP_H_
#define INC_APP_H_

#include "main.h"

void app_clock_init(void);   /* HSE 8 MHz -> 72 MHz, call before peripheral init */
void app_pullups_init(void); /* PB1/PB5/PB8 = 1, feeds I2C pull-ups, call before I2C init */
void app_init(I2C_HandleTypeDef *i2c_s1, I2C_HandleTypeDef *i2c_s2);
void app_loop(void);         /* call from while(1) */

#endif /* INC_APP_H_ */
