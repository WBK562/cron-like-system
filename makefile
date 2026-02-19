CC = gcc
CFLAGS = -Iinclude -Wall -Wextra
LIBS = -pthread -lrt
SRC = src/cron.c src/logger.c src/main.c
OBJ = $(SRC:.c=.o)
TARGET = cron_app

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(OBJ) -o $(TARGET) $(LIBS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f src/*.o $(TARGET) logger_file.txt dump_*.txt
