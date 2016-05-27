TARGET: player master telnet_session

CC	 = g++
CPPFLAGS = -Wall -O2 -std=c++11 -lboost_regex 
LFLAGS	 = -Wall

player: player.o err.o
	$(CC) $(LFLAGS) $^ -o $@ -lboost_regex

master: master.o err.o
	$(CC) $(LFLAGS) $^ -o $@ -lboost_regex

telnet_session: telnet_session.o err.o
	$(CC) $(LFLAGS) $^ -o $@ -lboost_regex

clean:
	rm -f server player protocol *.o *~ *.bak
