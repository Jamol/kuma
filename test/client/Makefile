#
# Makefile for build using GNU C++(Unified for all Unix)
# The autoconf will not change this file
#
##############################################################################
#

ROOTDIR = ..
KUMADIR = ../..
SRCDIR = $(ROOTDIR)/client

BINDIR = $(KUMADIR)/bin/linux
LIBDIR = $(ROOTDIR)/lib
OBJDIR = $(ROOTDIR)/objs/client/linux
TARGET = client

#
##############################################################################
#

INCLUDES = -I. -I$(ROOTDIR)/../src -I$(ROOTDIR)/../include
#
##############################################################################
#
LIBS = -L$(BINDIR) -lkuma

#
##############################################################################
#
CXX=g++

CXXFLAGS = -g -std=c++14 -pipe -fPIC -Wall -Wextra -pedantic
LDFLAGS = -lpthread -ldl -lssl -lcrypt -Wl,-rpath,.

SRCS =  \
    LoopPool.cpp\
    TestLoop.cpp\
    TcpClient.cpp\
    DataSender.cpp\
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
