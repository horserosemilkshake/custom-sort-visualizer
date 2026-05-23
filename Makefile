CC := gcc
CFLAGS := -std=c11 -Wall -Wextra -Wpedantic -O2 $(shell pkg-config --cflags gtk+-3.0 gmodule-2.0)
LDFLAGS := $(shell pkg-config --libs gtk+-3.0 gmodule-2.0)

SRC := src/main.c src/sort_engine.c
OBJ := $(SRC:.c=.o)
TARGET := sort-visualizer
WORKER := sort-visualizer-worker
WORKER_SRC := src/custom_worker.c
WORKER_OBJ := $(WORKER_SRC:.c=.o)

.PHONY: all clean run

ifeq ($(OS),Windows_NT)
BUILD_WORKER := 0
WORKER_TARGETS :=
else
BUILD_WORKER := 1
WORKER_TARGETS := $(WORKER)
LDFLAGS += -ldl
endif

all: $(TARGET) $(WORKER_TARGETS)

$(TARGET): $(OBJ)
	$(CC) $(OBJ) -o $@ $(LDFLAGS)

$(WORKER): $(WORKER_OBJ)
	$(CC) $(WORKER_OBJ) -o $@ $(LDFLAGS)

src/%.o: src/%.c include/sort_engine.h
	$(CC) $(CFLAGS) -Iinclude -c $< -o $@

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(OBJ) $(WORKER_OBJ) $(TARGET) $(WORKER)
