#
##############################################################################
#

ROOTDIR = ..
SRCDIR = $(ROOTDIR)/src
THIRDPARTYDIR = $(ROOTDIR)/third_party

#OPENSSLDIR = $(THIRDPARTYDIR)/openssl
OPENSSLDIR = /usr/include
ZLIBDIR = $(THIRDPARTYDIR)/zlib
LIBKEVDIR = $(THIRDPARTYDIR)/libkev
HPACKDIR = $(THIRDPARTYDIR)/HPacker

BINDIR = $(ROOTDIR)/bin/linux
LIBDIR = $(ROOTDIR)/lib
OBJDIR = $(ROOTDIR)/objs/linux
TARGET = libkuma.so

#
##############################################################################
#

INCLUDES = -I. -I../include -I$(THIRDPARTYDIR) -I$(OPENSSLDIR)
#
##############################################################################
#
#LIBS = $(OPENSSLDIR)/lib/linux/libssl.a $(OPENSSLDIR)/lib/linux/libcrypto.a
LIBS = $(LIBKEVDIR)/lib/linux/Release/libkev.a

#
##############################################################################
#
CXX=g++
CC=gcc

CXXFLAGS = -g -std=c++14 -pipe -fPIC -Wall -Wextra -pedantic -DKUMA_HAS_OPENSSL
LDFLAGS = -shared -lpthread -ldl $(LIBS) -lssl -lcrypto
LDFLAGS += -Wl,-Bsymbolic -Wl,-soname,libkuma.so

ZLIBSRCS = \
    $(ZLIBDIR)/adler32.c \
    $(ZLIBDIR)/compress.c \
    $(ZLIBDIR)/crc32.c \
    $(ZLIBDIR)/deflate.c \
    $(ZLIBDIR)/infback.c \
    $(ZLIBDIR)/inffast.c \
    $(ZLIBDIR)/inflate.c \
    $(ZLIBDIR)/inftrees.c \
    $(ZLIBDIR)/trees.c \
    $(ZLIBDIR)/uncompr.c \
    $(ZLIBDIR)/zutil.c

HPACKSRCS = \
    $(HPACKDIR)/src/HPackTable.cpp \
    $(HPACKDIR)/src/HPacker.cpp

SRCS =  \
    AcceptorBase.cpp \
    SocketBase.cpp \
    UdpSocketBase.cpp \
    TcpSocketImpl.cpp \
    UdpSocketImpl.cpp \
    TcpListenerImpl.cpp \
    TcpConnection.cpp \
    http/Uri.cpp \
    http/HttpHeader.cpp \
    http/HttpMessage.cpp \
    http/HttpParserImpl.cpp \
    http/H1xStream.cpp \
    http/HttpRequestImpl.cpp \
    http/Http1xRequest.cpp \
    http/HttpResponseImpl.cpp \
    http/Http1xResponse.cpp \
    http/HttpCache.cpp \
    http/httputils.cpp \
    http/v2/H2Frame.cpp \
    http/v2/FrameParser.cpp \
    http/v2/FlowControl.cpp \
    http/v2/H2Handshake.cpp \
    http/v2/H2Stream.cpp \
    http/v2/H2StreamProxy.cpp \
    http/v2/Http2Request.cpp \
    http/v2/Http2Response.cpp \
    http/v2/H2ConnectionImpl.cpp \
    http/v2/H2ConnectionMgr.cpp \
    http/v2/h2utils.cpp \
    http/v2/PushClient.cpp \
    compr/compr.cpp \
    compr/compr_zlib.cpp \
    ws/WSHandler.cpp \
    ws/WebSocketImpl.cpp \
    ws/WSConnection.cpp \
    ws/WSConnection_v1.cpp \
    ws/WSConnection_v2.cpp \
    ws/exts/ExtensionHandler.cpp \
    ws/exts/PMCE_Deflate.cpp \
    ws/exts/PMCE_Base.cpp \
    ws/exts/WSExtension.cpp \
    utils/utils.cpp \
    utils/base64.cpp \
    ssl/SslHandler.cpp \
    ssl/BioHandler.cpp \
    ssl/SioHandler.cpp \
    ssl/OpenSslLib.cpp \
    ssl/ssl_utils_linux.cpp \
    proxy/ProxyAuthenticator.cpp \
    proxy/BasicAuthenticator.cpp \
    proxy/ProxyConnectionImpl.cpp \
    ioop/OpAcceptor.cpp \
    ioop/OpContext.cpp \
    ioop/OpSocket.cpp \
    ioop/OpUdpSocket.cpp \
    DnsResolver.cpp \
    kmapi.cpp

SRCS += $(ZLIBSRCS) $(HPACKSRCS)

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
	$(CC) -c -o $@ $< $(CXXFLAGS) $(INCLUDES)

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
