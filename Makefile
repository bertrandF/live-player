CC = gcc
CFLAGS = -Wall -g
LDFLAGS = -lSDL -lavformat -lavcodec -lavutil -lswscale -lm -lz -lbz2
EXEC = webcam
SRC = $(wildcard *.c)
OBJ = $(SRC:.c=.o)
DEPFLAGS = 

all: dep $(EXEC)
dep: Makefile.dep
Makefile.dep: $(SRC)
	@touch Makefile.dep
	$(CC) -MM $(DEPFLAGS) $(SRC) > $@
$(EXEC): $(OBJ)
	$(CC) -o $@ $^ $(LDFLAGS)
%.o: %.c
	$(CC) -o $@ -c $< $(CFLAGS)

.PHONY: clean mrproper dep
clean:
	rm -f $(OBJ)
mrproper: clean
	rm -f *~ $(EXEC) Makefile.dep

ifneq ($(wildcard Makefile.dep),)
include Makefile.dep
endif
