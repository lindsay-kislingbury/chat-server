CC = gcc
CFLAGS = -Wall -pthread

all: chat_server chat_client

chat_server: chat_server.c
	$(CC) $(CFLAGS) -o chat_server chat_server.c

chat_client: chat_client.c
	$(CC) $(CFLAGS) -o chat_client chat_client.c

run_server:
	@echo "This is the Makefile"
	./chat_server 3652

run_client:
	@echo "This is the Makefile"	
	./chat_client 3652

clean:
	rm -f chat_server chat_client

