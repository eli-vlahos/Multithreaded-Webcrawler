# Makefile, ECE252  
# Yiqing Huang, 2018/11/02

CC = gcc 
CFLAGS = -Wall -std=c99 -g # "curl-config --cflags" output is empty  
LD = gcc
LDFLAGS = -std=c99 -g 
LDLIBS = -lcurl -pthread -ldl -lz -lm # "curl-config --libs" output 

SRCS   = paster.c
OBJS3  = paster.o
TARGETS = paster

all: ${TARGETS}

paster: $(OBJS3) main_write_header_cb.o crc.o zutil.o
	$(LD) -o $@ $^ $(LDLIBS) $(LDFLAGS) 

%.o: %.c 
	$(CC) $(CFLAGS) -c $< 

%.d: %.c
	gcc -MM -MF $@ $<

-include $(SRCS:.c=.d)

.PHONY: clean
clean:
	rm -f *~ *.d *.o $(TARGETS) *.png
