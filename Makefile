CFLAGS = -Wall -fPIC
INCLUDES := -I./include \
		-I/usr/include \
		-I/usr/include/libdrm
LDFLAGS := -L../sysroot/lib
LIBS := -lkms -ldrm

CC := gcc

SRCS := $(wildcard *.c)
OBJS := $(SRCS:.c=.o)

NAME := nx-renderer
LIB_TARGET := lib$(NAME).so

.c.o:
	$(CC) $(INCLUDES) $(CFLAGS) -c $^

$(LIB_TARGET): $(OBJS)
	$(CC) $(LDFLAGS) -shared -Wl,-soname,$(TARGET) -o $@ $^ $(LIBS)

all: $(LIB_TARGET)

install: $(LIB_TARGET)
	cp $^ ../sysroot/lib
	cp include/dp.h ../sysroot/include
	cp include/dp_common.h ../sysroot/include
.PHONY: clean

clean:
	rm -f *.o
	rm -f $(LIB_TARGET)
	rm -f $(APP_TARGET)
