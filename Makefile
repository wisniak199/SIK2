TARGET: player master

CC	 = g++
CPPFLAGS = -Wall -O2 -std=c++11
LFLAGS	 = -Wall

player: player.o err.o
	$(CC) $(LFLAGS) $^ -o $@ -lboost_regex 

master: master.o err.o
	$(CC) $(LFLAGS) $^ -o $@ -lboost_regex

clean:
	rm -f server player protocol *.o *~ *.bak
