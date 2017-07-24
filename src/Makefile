#
##############################################################################
#

ROOTDIR = ..
SRCDIR = $(ROOTDIR)/src
VENDORDIR = $(ROOTDIR)/vendor

OPENSSLDIR = $(VENDORDIR)/openssl

BINDIR = $(ROOTDIR)/bin/linux
LIBDIR = $(ROOTDIR)/lib
OBJDIR = $(ROOTDIR)/objs/linux
TARGET = libkuma.so

#
##############################################################################
#

INCLUDES = -I. -I$(VENDORDIR) -I$(OPENSSLDIR)/include
#
##############################################################################
#
LIBS = $(OPENSSLDIR)/lib/linux/libssl.a $(OPENSSLDIR)/lib/linux/libcrypto.a

#
##############################################################################
#
CXX=g++

CXXFLAGS = -g -std=c++11 -pipe -fPIC -Wall -Wextra -pedantic -DKUMA_HAS_OPENSSL
LDFLAGS = -shared -Wl,-Bsymbolic -lpthread -ldl

SRCS =  \
    EventLoopImpl.cpp \
    AcceptorBase.cpp \
    SocketBase.cpp \
    UdpSocketBase.cpp \
    TcpSocketImpl.cpp \
    UdpSocketImpl.cpp \
    TimerManager.cpp \
    TcpListenerImpl.cpp \
    TcpConnection.cpp \
    poll/EPoll.cpp \
    poll/VPoll.cpp \
    poll/SelectPoll.cpp \
    poll/Notifier.cpp \
    http/Uri.cpp \
    http/HttpHeader.cpp \
    http/HttpMessage.cpp \
    http/HttpParserImpl.cpp \
    http/HttpRequestImpl.cpp \
    http/Http1xRequest.cpp \
    http/HttpResponseImpl.cpp \
    http/Http1xResponse.cpp \
    http/v2/H2Frame.cpp \
    http/v2/FrameParser.cpp \
    http/v2/FlowControl.cpp \
    http/v2/H2Stream.cpp \
    http/v2/Http2Request.cpp \
    http/v2/Http2Response.cpp \
    http/v2/H2ConnectionImpl.cpp \
    http/v2/H2ConnectionMgr.cpp \
    http/v2/hpack/HPackTable.cpp \
    http/v2/hpack/HPacker.cpp \
    ws/WSHandler.cpp \
    ws/WebSocketImpl.cpp \
    util/util.cpp \
    util/kmtrace.cpp \
    util/base64.cpp \
    ssl/SslHandler.cpp \
    ssl/BioHandler.cpp \
    ssl/SioHandler.cpp \
    ssl/OpenSslLib.cpp \
    DnsResolver.cpp \
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
