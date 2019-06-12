all: irc client

irc: irc.c
	gcc -pthread -o irc irc.c
	
client: client.c
	gcc -pthread -o client client.c

clean:
	rm -f irc client f2 err out *~
