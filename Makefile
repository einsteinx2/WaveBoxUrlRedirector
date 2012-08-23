all: wavebox_url_redirector

wavebox_url_redirector: wavebox_url_redirector.c
	gcc -W -Wall -lfcgi -lsqlite3 -g -o wavebox_url_redirector wavebox_url_redirector.c

clean:
	rm wavebox_url_redirector
