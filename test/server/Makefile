#
# Makefile for build using GNU C++(Unified for all Unix)
# The autoconf will not change this file
#
##############################################################################
#

ROOTDIR = ..
KUMADIR = ../..
SRCDIR = $(ROOTDIR)/server

OPENSSL_HOME = $(ROOTDIR)/../vendors/opensource/openssl-1.0.1c
OPENSSL_INCLUDE = $(OPENSSL_HOME)/include
ifneq ($(flag),1)
	OPENSSL_LIBS = $(OPENSSL_HOME)/lib64
else
	OPENSSL_LIBS = $(OPENSSL_HOME)/lib
endif

BINDIR = $(KUMADIR)/bin/linux
LIBDIR = $(ROOTDIR)/lib
OBJDIR = $(ROOTDIR)/objs/server/linux
TARGET = server

#
##############################################################################
#

INCLUDES = -I. -I$(ROOTDIR)/../src -I$(OPENSSL_INCLUDE)
#
##############################################################################
#
#LIBS = $(OPENSSL_LIBS)/libssl.a $(OPENSSL_LIBS)/libcrypto.a
LIBS = $(BINDIR)/libkuma.so

#
##############################################################################
#
CXX=g++

CXXFLAGS = -g -std=c++11 -pipe -fPIC -Wall -Wextra -pedantic
#LDFLAGS = -Wl,-R -Wl,/Users/xxx/work/mygit/kuma/bin/linux

SRCS =  \
    LoopPool.cpp\
    TestLoop.cpp\
    AutoHelper.cpp\
    TcpServer.cpp\
    UdpServer.cpp\
    Connection.cpp\
    HttpTest.cpp\
    WsTest.cpp\
    main.cpp
    
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
