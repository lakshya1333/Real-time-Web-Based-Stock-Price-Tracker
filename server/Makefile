CC = gcc
CFLAGS = -Wall -Wextra -g -O2 -I.
LDFLAGS = -lssl -lcrypto -lsqlite3 -pthread

SRCS = main.c network.c websocket.c database.c stock.c
OBJS = $(SRCS:.c=.o)
TARGET = stock_server

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) -o $@ $(OBJS) $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)
