MPICH_PREFIX ?= /opt/mpich-4.2.0
CC = $(MPICH_PREFIX)/bin/mpicc
CFLAGS = -std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Wpedantic -O2
LDFLAGS = -pthread
TARGET = image_analyzer
TARGET_ARM = image_processor_arm
TARGET_X86 = image_processor_x86
SOURCES = main.c bmp.c filters.c task_pool.c timing.c
OBJECTS = $(SOURCES:.c=.o)

.PHONY: all clean build-arm build-x86

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) -o $@ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

build-arm: clean
	$(MAKE) TARGET=$(TARGET_ARM)

build-x86: clean
	$(MAKE) TARGET=$(TARGET_X86)

clean:
	rm -f $(OBJECTS) $(TARGET) $(TARGET_ARM) $(TARGET_X86)
