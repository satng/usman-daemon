CC=gcc

ask_daemon:	ask_daemon.c
	$(CC) -Wall -o ask_daemon ask_daemon.c -lmysqlclient

clean:
	rm -rf ask_daemon