#
# Makefile for build using GNU C++(Unified for all Unix)
# The autoconf will not change this file
#
##############################################################################
#

ROOTDIR = ..
KUMADIR = ../..
SRCDIR = $(ROOTDIR)/server

BINDIR = $(KUMADIR)/bin/linux
LIBDIR = $(ROOTDIR)/lib
OBJDIR = $(ROOTDIR)/objs/server/linux
TARGET = server

#
##############################################################################
#

INCLUDES = -I. -I$(ROOTDIR)/../src -I$(ROOTDIR)/../include
#
##############################################################################
#
LIBS = $(BINDIR)/libkuma.so

#
##############################################################################
#
CXX=g++

CXXFLAGS = -g -std=c++14 -pipe -fPIC -Wall -Wextra -pedantic
LDFLAGS = -lpthread -ldl -lssl -lcrypt

SRCS =  \
    LoopPool.cpp\
    TestLoop.cpp\
    TcpServer.cpp\
    UdpServer.cpp\
    TcpTest.cpp\
    testutil.cpp\
    HttpTest.cpp\
    WsTest.cpp\
    H2ConnTest.cpp \
    ProtoDemuxer.cpp \
    main.cpp
    
OBJS = $(patsubst %.c,$(OBJDIR)/%.o,$(patsubst %.cpp,$(OBJDIR)/%.o,$(patsubst %.cxx,$(OBJDIR)/%.o,$(SRCS))))
#OBJS = $(SRCS:%.cpp=$(OBJDIR)/%.o)

testdir = @if test ! -d $(1);\
	then\
		mkdir -p $(1);\
	fi

$(BINDIR)/$(TARGET): $(OBJS)
	$(call testdir,$(dir $@))
	$(CXX) -o $(BINDIR)/$(TARGET) $(OBJS) $(LIBS) $(LDFLAGS)

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
