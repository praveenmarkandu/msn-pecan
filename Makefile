CC=gcc

PURPLE_CFLAGS=`pkg-config --cflags purple`
PURPLE_LIBS=`pkg-config --libs purple`

CFLAGS=-Wall -ggdb

objects = \
	cmdproc.o \
	command.o \
	dialog.o \
	directconn.o \
	error.o \
	group.o \
	history.o \
	httpconn.o \
	msg.o \
	msn.o \
	nexus.o \
	notification.o \
	object.o \
	page.o \
	servconn.o \
	session.o \
	slp.o \
	slpcall.o \
	slplink.o \
	slpmsg.o \
	slpsession.o \
	state.o \
	switchboard.o \
	sync.o \
	table.o \
	transaction.o \
	user.o \
	userlist.o \
	msn-utils.o

all: libmsn-pecan.so

libmsn-pecan.so: $(objects)
	$(CC) $(PURPLE_LIBS) $+ -shared -o $@

%.o: %.c
	$(CC) $(CFLAGS) $(PURPLE_CFLAGS) $+ -c -o $@

clean:
	rm -f libmsn-pecan.so $(objects)

install:
	cp libmsn-pecan.so /usr/lib/purple-2
	chcon -t textrel_shlib_t /usr/lib/purple-2/libmsn-pecan.so
