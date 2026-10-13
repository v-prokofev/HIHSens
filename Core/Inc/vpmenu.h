/*
 * vpmenu.h
 *
 *  Created on: Jan 10, 2024
 *      Author: catch
 */

#ifndef SRC_VPMENU_H_
#define SRC_VPMENU_H_

#include "main.h"

typedef int (*THandleFunction)(float param1, float param2);

void vpmenu_init( uint8_t * str, uint32_t max_size);
void vpmenu_register_handler(const char *cmd, THandleFunction handler_function);
int vpmenu_parse_str();
int vpmenu_param_count();   /* number of numeric params parsed for last command */
void vpmenu_print_commands(void (*print)(const char *s));

#endif /* SRC_VPMENU_H_ */
