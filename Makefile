CC = gcc
CFLAGS = -Wall -O2

TARGET = httpd
SOURCES = httpd.c net.c

# Default target to build the executable
all: $(TARGET)

$(TARGET): $(SOURCES)
	$(CC) $(CFLAGS) -o $(TARGET) $(SOURCES)

# Run the server on port 8080
run: $(TARGET)
	./$(TARGET) 8080
