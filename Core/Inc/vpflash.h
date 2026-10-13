/*
 * vpflash.h
 *
 *  Created on: 31 авг. 2022 г.
 *      Author: fml30
 */

#ifndef SRC_VPFLASH_H_
#define SRC_VPFLASH_H_

// Didn't forget to set right flash size in *.ld

#include "main.h"


//Flash_Write_Data(0x08004410 , (uint32_t *)data2, 9);
//Flash_Read_Data(0x08004410 , Rx_Data, 10);


uint32_t Flash_Write_Data (uint32_t StartPageAddress, uint32_t *Data, uint16_t numberofwords);
void Flash_Read_Data (uint32_t StartPageAddress, uint32_t *RxBuf, uint16_t numberofwords);


#endif /* SRC_VPFLASH_H_ */
