/*
 * vpmenu.c
 *
 *  Created on: Jan 10, 2024
 *      Author: catch
 *
 *  Ported from hmp555: params parsed with strtof (works with newlib-nano
 *  without -u _scanf_float), cmd copy bounded, missing params = 0.
 */


#include "vpmenu.h"
#include "main.h"
#include <string.h>
#include <stdlib.h>

uint8_t *string_storage;
uint32_t string_storage_capacity;

#define MAX_HANDLER_COUNT (16)
#define CMD_LEN (16)

typedef struct tag_handler{
	char cmd[CMD_LEN];
	THandleFunction handler_function;
} THandlerRecord;

THandlerRecord handlers[MAX_HANDLER_COUNT];
uint8_t handler_count = 0;
static int param_count = 0;

void vpmenu_init( uint8_t * str, uint32_t max_size){
	string_storage = str;
	string_storage_capacity = max_size;
}

void vpmenu_register_handler(const char *cmd, THandleFunction handler_function){
	if( handler_count < MAX_HANDLER_COUNT ){
		strncpy( handlers[handler_count].cmd, cmd, CMD_LEN - 1 );
		handlers[handler_count].cmd[CMD_LEN - 1] = 0;
		handlers[handler_count].handler_function = handler_function;
		handler_count += 1;
	}
}

static int find_cmd(const char *word){
	for(int i = 0; i < handler_count; i++){
		if( strcmp(handlers[i].cmd, word) == 0 ){
			return i;
		}
	}
	return -1;
}

int vpmenu_param_count(){
	return param_count;
}

void vpmenu_print_commands(void (*print)(const char *s)){
	for(int i = 0; i < handler_count; i++){
		print(handlers[i].cmd);
	}
}

int vpmenu_parse_str(){
	char word[CMD_LEN];
	const char *p = (const char *)string_storage;
	float prm[2] = {0, 0};

	while( *p == ' ' || *p == '\t' ) p++;
	int n = 0;
	while( *p && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n' ){
		if( n >= CMD_LEN - 1 ) return -1;
		word[n++] = *p++;
	}
	word[n] = 0;
	if( n == 0 ) return -1;

	int cmd_id = find_cmd(word);
	if( cmd_id < 0 ) return -1;

	param_count = 0;
	for(int i = 0; i < 2; i++){
		char *end;
		float v = strtof(p, &end);
		if( end == p ) break;
		prm[i] = v;
		param_count++;
		p = end;
	}

	return handlers[cmd_id].handler_function(prm[0], prm[1]);
}
