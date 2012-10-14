azratool: main.c uart.c
	$(CC) -o azratool uart.c main.c -llua -lreadline

clean:
	rm azratool	
