#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <readline/readline.h>
#include <readline/history.h>
 
#include "uart.h"
#include "lualib.h"
#include "lauxlib.h"

#define START '['
#define STOP  ']'

struct uart_settings_t* us;

static unsigned char csum(unsigned char* data, size_t len) {
	unsigned char csum=0;
	while (len--)
		csum+=data[len];
	return csum;
}

char* fetch_packet(int fd) {
	unsigned char tmp[128];
restart:
	do {
		read(fd,tmp,1);
	} while (tmp[0] != START);
	block_read(fd, &tmp[1], 1);
	int len = (int) tmp[1];
	unsigned char* data = malloc(len+10);
	memcpy(data,tmp,2);
	block_read(fd, &data[2], len+1);
	int i;
	for (i=0; i<len+3; i++)
			printf("0x%hhx ", data[i]);
	if (csum(&data[1],len) != data[len+1]) {
		printf("\nchecksum error: %hhx vs %hhx\n", csum(&data[1],len), data[len+1]);
		free(data);
		goto restart;
	}
	data[len+1]=0;
	printf("\n");
	return data+2;
}

struct packet {
	char flags;
	char id;
	char data[];
};


struct object {
	char* name;
	char* arg;
	char* reply;
};

struct object objects[255];
int obj=0;
/* actual call has arg0 as method number */
int write_argument(char* buffer, lua_State *L, int arg, char* tok, char* fmt) {
	char* str;
	char tmp;
	int bytes;
	if (*tok == 's') {
			printf("string\n");	
			str = lua_tostring(L,arg);
			strcpy(buffer,str);
			return strlen(str)+1;
		} else {
			sscanf(tok,"%d%s",&bytes,fmt);
			printf("fmt: %s bytes: %d", fmt, bytes);		
			printf("FixMe: Proper scanning of different numbers\n");
			tmp = (unsigned char) lua_tonumber(L,arg);
			*buffer=tmp;
			return 1;
			}
	
	return 0;
}

int l_urpc_call(lua_State* L) {

	char payload[256];
	int payloadptr=3;
	int argc = lua_gettop(L);
	int id = lua_tonumber(L,1);
	printf("args %d, id %d\n",argc,id);
	int i;
	payload[0]=START;
	payload[2]=(unsigned char) id;
	char* tmp = strdup(objects[id].arg);
	char* tok = strtok(objects[id].arg, ";");
	char fmt[16];
	for (i=2;i<=argc;i++)
	{
		payloadptr+=write_argument(&payload[payloadptr], L, i, tok, fmt);				
	}
	payload[1]=(unsigned char) payloadptr-1;
	payload[payloadptr] = csum(&payload[1],payloadptr-1);
	payloadptr++;
	payload[payloadptr++]=STOP;
	write(us->fd,payload,payloadptr);
}

void report_errors(lua_State *L, int status) {
	if ( status!=0 ) {
		printf("ooops: %s \n", lua_tostring(L, -1));
		lua_pop(L, 1); // remove error message
	}
}

void runscript(lua_State* L, char* file)
{
	printf("Loading lua script: %s\n", file);
	int s = luaL_loadfile(L, file);
	if ( s==0 ) {
		s = lua_pcall(L, 0, LUA_MULTRET, 0);
		if (s!=0) report_errors(L, s);
	}
	report_errors(L, s);
	return L;
}


void l_pushtablestring(lua_State* L , int key , char* value) {
    lua_pushnumber(L, key);
    lua_pushstring(L, value);
    lua_settable(L, -3);
} 

int parse_data(struct packet* pkt, lua_State* L) {
	if (pkt->flags==0x00) {
		return 1;
	}
	int len;
	char * type = (pkt->flags & 2) ? "event" : "method";
	printf("%s #%hhd\n", type, pkt->id);
	char* name = pkt->data;
	len = strlen(pkt->data)+1;	
	char* arg = &pkt->data[len];
	len = strlen(arg)+1;
	char* reply = &arg[len];

	printf("Name:\t %s\n", name);
	if (*arg) 
		printf("Arg:\t %s\n", arg);
	if (*reply) 
		printf("Reply:\t %s\n", reply);
	objects[obj].name  = name;
	objects[obj].arg   = arg;
	objects[obj].reply = reply;
	/* Push id and name to lua table */
	l_pushtablestring(L, obj, name);
	obj++;
	 
	return 0;
}

int main() {
	lua_State *L = lua_open();
	luaL_openlibs(L);
	printf("uRPC/Azra proof-of-concept shell\n");
	printf("Warning: only small 8-bit model and only serial transport supported\n");
	printf("Events are NOT supported\n");
	us = stc_uart_settings("/dev/ttyUSB0",19200);
	if (uart_init(us) < 0) {
		exit(1);
	}
	printf("Triggering discovery\n");
	char tmp[] = {'[',0x0,0x0,']'};
	write(us->fd, tmp, 4);
	char* name = fetch_packet(us->fd);
	printf("io tag: %s\n",name);
	/* Now, goes the discovery. 
	 * We need to push all relevant stuff to lua.
	 * In lua, we only need name\id pairs for methods
	 * and events. The rest of env is prepared in lua.
	 */
        lua_register(L, "urpc_call", l_urpc_call);
	lua_newtable(L);
	while(1) {
		char* data = fetch_packet(us->fd);
		if (parse_data(data,L)) break;
	}
	lua_setglobal(L, "_urpc");
	runscript(L,"init.lua");
	char* input, shell_prompt[100];
 
	for(;;)
	{
        // inputing...
        input = readline("azra# ");
        // eof
        if (input==NULL)
            break;
        // path autocompletion when tabulation hit
        rl_bind_key('\t', rl_complete);
        // adding the previous input into history
        add_history(input);
	int l = luaL_dostring(L,input);
        report_errors(L,l);
 
        // Т.к. вызов readline() выделяет память, но не освобождает(а возвращает), то эту память нужно вернуть(освободить).
        free(input);
    
	}	
}

