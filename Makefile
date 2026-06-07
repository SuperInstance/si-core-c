CC      = gcc
CFLAGS  = -Wall -Wextra -std=c99 -O2
LDFLAGS = -lm

SRC     = si_core.c
OBJ     = $(SRC:.c=.o)
TEST_SRC = tests/test_core.c
TEST_BIN = test_core

.PHONY: all test clean memcheck

all: $(OBJ)

$(OBJ): %.o: %.c si_core.h
	$(CC) $(CFLAGS) -c $< -o $@

test: $(TEST_BIN)
	./$(TEST_BIN)

$(TEST_BIN): $(TEST_SRC) si_core.c si_core.h
	@mkdir -p tests
	$(CC) $(CFLAGS) -I. -o $@ $(TEST_SRC) si_core.c $(LDFLAGS)

memcheck: $(TEST_BIN)
	valgrind --leak-check=full --error-exitcode=1 ./$(TEST_BIN)

clean:
	rm -f $(OBJ) $(TEST_BIN)
