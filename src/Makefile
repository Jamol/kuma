#
##############################################################################
#

ROOTDIR = ..
SRCDIR = $(ROOTDIR)/src

OPENSSL_HOME = $(ROOTDIR)/../vendors/opensource/openssl-1.0.1c
OPENSSL_INCLUDE = $(OPENSSL_HOME)/include
ifneq ($(flag),1)
	OPENSSL_LIBS = $(OPENSSL_HOME)/lib64
else
	OPENSSL_LIBS = $(OPENSSL_HOME)/lib
endif

BINDIR = $(ROOTDIR)/bin/linux
LIBDIR = $(ROOTDIR)/lib
OBJDIR = $(ROOTDIR)/objs/linux
TARGET = libkuma.so

#
##############################################################################
#

INCLUDES = -I. -I../vendor -I$(OPENSSL_INCLUDE)
#
##############################################################################
#
#LIBS = $(OPENSSL_LIBS)/libssl.a $(OPENSSL_LIBS)/libcrypto.a

#
##############################################################################
#
CXX=g++

CXXFLAGS = -g -std=c++11 -pipe -fPIC -Wall -Wextra -pedantic
LDFLAGS = -shared -Wl,-Bsymbolic

SRCS =  \
    EventLoopImpl.cpp\
    TcpSocketImpl.cpp\
    UdpSocketImpl.cpp\
    TimerManager.cpp\
    TcpServerSocketImpl.cpp\
    poll/VPoll.cpp\
    poll/SelectPoll.cpp\
    http/Uri.cpp\
    http/HttpParserImpl.cpp\
    http/HttpRequestImpl.cpp\
    http/HttpResponseImpl.cpp\
    ws/WSHandler.cpp\
    ws/WebSocketImpl.cpp\
    util/util.cpp\
    util/kmtrace.cpp\
    util/base64.cpp\
    kmapi.cpp
    
OBJS = $(patsubst %.c,$(OBJDIR)/%.o,$(patsubst %.cpp,$(OBJDIR)/%.o,$(patsubst %.cxx,$(OBJDIR)/%.o,$(SRCS))))
#OBJS = $(SRCS:%.cpp=$(OBJDIR)/%.o)

testdir = @if test ! -d $(1);\
	then\
		mkdir -p $(1);\
	fi

$(BINDIR)/$(TARGET): $(OBJS)
	$(call testdir,$(dir $@))
	$(CXX) -o $(BINDIR)/$(TARGET) $(LDFLAGS) $(OBJS) $(LIBS)

$(OBJDIR)/%.o: %.c
	$(call testdir,$(dir $@))
	$(CXX) -c -o $@ $< $(CXXFLAGS) $(INCLUDES)

$(OBJDIR)/%.o: %.cpp
	$(call testdir,$(dir $@))
	$(CXX) -c -o $@ $< $(CXXFLAGS) $(INCLUDES)

$(OBJDIR)/%.o: %.cxx
	$(call testdir,$(dir $@))
	$(CXX) -c -o $@ $< $(CXXFLAGS) $(INCLUDES)

print-%  : ; @echo $* = $($*)
    
.PHONY: clean
clean:
	rm -f $(OBJS) $(BINDIR)/$(TARGET)
