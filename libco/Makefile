NAME := $(shell basename $(PWD))

ifeq ($(NAME),)
$(error Should make in each lab's directory)
endif

SRCS   := $(shell find . -maxdepth 1 -name "*.c")
DEPS   := $(shell find . -maxdepth 1 -name "*.h") $(SRCS)
CFLAGS += -O1 -std=gnu11 -ggdb -Wall -Werror -Wno-unused-result -Wno-unused-value -Wno-unused-variable \
		  -U_FORTIFY_SOURCE 

.PHONY: all git test clean commit-and-make

.DEFAULT_GOAL := all
all: $(NAME)-64.so $(NAME)-32.so

# $(NAME)-64: $(DEPS) # 64bit binary
# 	gcc -m64 $(CFLAGS) $(SRCS) -o $@ $(LDFLAGS)

# $(NAME)-32: $(DEPS) # 32bit binary
# 	gcc -m32 $(CFLAGS) $(SRCS) -o $@ $(LDFLAGS)
 
$(NAME)-64.so: $(DEPS) # 64bit shared library
	gcc -fPIC -shared -m64 $(CFLAGS) $(SRCS) -o $@ $(LDFLAGS)

$(NAME)-32.so: $(DEPS) # 32bit shared library
	gcc -fPIC -shared -m32 $(CFLAGS) $(SRCS) -o $@ $(LDFLAGS)

clean:
	rm -f $(NAME)-64 $(NAME)-32 $(NAME)-64.so $(NAME)-32.so
