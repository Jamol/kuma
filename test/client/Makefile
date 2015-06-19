#
# Makefile for build using GNU C++(Unified for all Unix)
# The autoconf will not change this file
#
##############################################################################
#

ROOTDIR = ..
KUMADIR = ../..
SRCDIR = $(ROOTDIR)/client

OPENSSL_HOME = $(ROOTDIR)/../vendors/opensource/openssl-1.0.1c
OPENSSL_INCLUDE = $(OPENSSL_HOME)/include
ifneq ($(flag),1)
	OPENSSL_LIBS = $(OPENSSL_HOME)/lib64
else
	OPENSSL_LIBS = $(OPENSSL_HOME)/lib
endif

BINDIR = $(KUMADIR)/bin/linux
LIBDIR = $(ROOTDIR)/lib
OBJDIR = $(ROOTDIR)/objs/client/linux
TARGET = client

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
#LINKFLAGS = -Wl,-R -Wl,/Users/xxx/work/mygit/kuma/bin/linux

SRCS =  \
    LoopPool.cpp\
    TestLoop.cpp\
    Client.cpp\
    HttpClient.cpp\
    UdpClient.cpp\
    WsClient.cpp\
    main.cpp
    
OBJS = $(patsubst %.c,$(OBJDIR)/%.o,$(patsubst %.cpp,$(OBJDIR)/%.o,$(patsubst %.cxx,$(OBJDIR)/%.o,$(SRCS))))
#OBJS = $(SRCS:%.cpp=$(OBJDIR)/%.o)

testdir = @if test ! -d $(1);\
	then\
		mkdir -p $(1);\
	fi

$(BINDIR)/$(TARGET): $(OBJS)
	$(call testdir,$(dir $@))
	$(CXX) $(LINKFLAGS) -o $(BINDIR)/$(TARGET) $(OBJS) $(LIBS)

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
