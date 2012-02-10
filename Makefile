ROOT=/opt/memcached
INCLUDE=-I${ROOT}/include

CC = gcc
CFLAGS=-std=gnu99 -g -DNDEBUG -Wall -fno-strict-aliasing -Wstrict-prototypes -Wmissing-prototypes -Wmissing-declarations \
 -Wredundant-decls ${INCLUDE} -DHAVE_CONFIG_H
LDFLAGS=-shared

all: fs_engine.so

install: all
	${CP} fs_engine.so ${ROOT}/lib

SRC = fs_engine.c
OBJS = ${SRC:%.c=%.o}

fs_engine.so: $(OBJS)
	${LINK.c} -o $@ ${OBJS}

%.o: %.c
	${COMPILE.c} $< -o $@   

clean:  
	$(RM) fs_engine.so $(OBJS)


