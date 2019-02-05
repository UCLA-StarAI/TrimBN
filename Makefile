CC = gcc
BUILD_DIR = build
BUILD_CFLAGS = -std=c99 -O2 -Wall -finline-functions -Iinclude -DNDEBUG -fPIC
LIBRARY_FLAGS = -Llib -lsdd -lm

EXEC_FILE = trim
SRC = src/main.c \
  src/fnf/compiler.c src/fnf/utils.c src/fnf/fnf.c src/fnf/io.c \
  src/trim/move.c src/trim/search.c src/trim/utils.c
HEADERS = include/sddapi.h include/compiler.h include/search.h

OBJS = $(patsubst src/%.c,obj/%.o,$(SRC))
BUILD_OBJS = $(addprefix $(BUILD_DIR)/, $(OBJS))
BUILD_EXEC = $(BUILD_DIR)/$(EXEC_FILE)

SRC_DIRS = $(shell find src/ -mindepth 1 -type d)
OBJ_DIRS = $(patsubst src/%,obj/%,$(SRC_DIRS))
BUILD_DIRS = $(addprefix $(BUILD_DIR)/, $(OBJ_DIRS))

$(BUILD_EXEC): $(BUILD_DIRS) $(BUILD_OBJS)
	$(CC) $(BUILD_OBJS) $(LIBRARY_FLAGS) -o $@

$(BUILD_DIRS):
	mkdir -p $(BUILD_DIRS)

$(BUILD_DIR)/obj/%.o: src/%.c $(HEADERS)
	$(CC) $(BUILD_CFLAGS) -c $< -o $@

.PHONY: clean
clean:
	rm -f $(BUILD_OBJS) $(BUILD_EXEC)
