CC=gcc

GOBJECT_CFLAGS=`pkg-config --cflags gobject-2.0`
GOBJECT_LIBS=`pkg-config --libs gobject-2.0`

CFLAGS=-Wall -ggdb -I..

parse_test_objects=parse.o \
		   ../io/pecan_cmd_server.o \
		   ../io/pecan_node.o \
		   ../cmd/cmdproc.o \
		   ../cmd/command.o \
		   ../cmd/history.o \
		   ../cmd/transaction.o \
		   ../cmd/msg.o \
		   ../pecan_log.o

buffer_test_objects=buffer.o \
		   ../io/pecan_buffer.o

ab_test_objects=ab.o \
		../ab/pecan_contact.o \
		../ab/pecan_group.o \
		../ab/pecan_contactlist.o \
		../pecan_util.o

printf_test_objects=printf.o \
		    ../pecan_printf.o

# all: parse_test buffer_test ab_test printf_test
all: printf_test

parse_test: $(parse_test_objects)
	$(CC) $(GOBJECT_LIBS) $+ -o $@

buffer_test: $(buffer_test_objects)
	$(CC) $(GOBJECT_LIBS) $+ -o $@

ab_test: $(ab_test_objects)
	$(CC) $(GOBJECT_LIBS) $+ -o $@

printf_test: $(printf_test_objects)
	$(CC) $(GOBJECT_LIBS) $+ -o $@

%.o: %.c
	$(CC) $(CFLAGS) $(GOBJECT_CFLAGS) $< -c -o $@

clean:
	rm -f printf_test $(printf_test_objects)
	rm -f ab_test $(ab_test_objects)
	rm -f parse_test $(parse_test_objects)
	rm -f buffer_test $(buffer_test_objects)
