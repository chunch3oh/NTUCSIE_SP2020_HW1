all: server.c
	gcc server.c -D READ_SERVER -o read_server
	gcc server.c -o write_server
clean:
	rm -f read_server write_server 
