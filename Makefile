CC = g++
CFLAGS = -std=c++17 -Wall -Werror -Wextra -O3
BIN = wask

$(BIN): main.cpp
	$(CC) $(CFLAGS) -o $@ $^
clear:
	rm -f $(BIN)
