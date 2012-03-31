TARGET = d

WARN = \
	-Wextra \
	-Wall

OPT = -pthread

DEBUG = -g

CFLAGS = \
	$(WARN) \
	$(OPT) \
	$(DEBUG) \
	$(NULL)

INCLUDE = \
	-I/opt/local/include \
	$(NULL)

LIBDIR = \
	-L/opt/local/lib \
	$(NULL)

LIBS = \
	-lao \
	-lmad \
	$(NULL)

CC = gcc

SRC = d.c

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(INCLUDE) $(LIBDIR) $(LIBS) $(SRC) -o $(TARGET)

clean:
	rm -f $(TARGET)
