/* Copyright (c) 2014, Fengping Bao <jamol@live.com>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "IOPoll.h"
#include "util/kmtrace.h"

KUMA_NS_BEGIN

#define WM_SOCKET_NOTIFY		0x0373

#define WM_POLLER_NOTIFY		WM_USER+101

#define KM_WIN_CLASS_NAME		"kuma_win_class_name"

class WinPoll : public IOPoll
{
public:
    WinPoll();
    ~WinPoll();
    
    bool init();
    int registerFd(SOCKET_FD fd, uint32_t events, IOCallback& cb);
    int registerFd(SOCKET_FD fd, uint32_t events, IOCallback&& cb);
    int unregisterFd(SOCKET_FD fd);
    int updateFd(SOCKET_FD fd, uint32_t events);
    int wait(uint32_t wait_ms);
    void notify();
    PollType getType() { return POLL_TYPE_WIN; }

public:
    void on_socket_notify(SOCKET_FD fd, uint32_t events);

    void on_poller_notify();
    
private:
    uint32_t get_events(uint32_t kuma_events);
    uint32_t get_kuma_events(uint32_t events);
    void resizePollItems(SOCKET_FD fd);

private:
    HWND            hwnd_;
    PollItemVector  poll_items_;

};

WinPoll::WinPoll()
: hwnd_(NULL)
{
    
}

WinPoll::~WinPoll()
{
    if (hwnd_) {

        if (::IsWindow(hwnd_)) {

            DestroyWindow(hwnd_);

        }

        hwnd_ = NULL;

    }
}

bool WinPoll::init()
{
    hwnd_ = ::CreateWindow(KM_WIN_CLASS_NAME, NULL, WS_OVERLAPPED, 0,
                                   0, 0, 0, NULL, NULL, NULL, 0);
    if (NULL == hwnd_) {
        return false;
    }
    SetWindowLong(hwnd_, 0, (LONG)this);
    return true;
}

uint32_t WinPoll::get_events(uint32_t kuma_events)
{
    uint32_t ev = 0;
    if(kuma_events & KUMA_EV_READ) {
        ev |= FD_READ;
    }
    if(kuma_events & KUMA_EV_WRITE) {
        ev |= FD_WRITE;
    }
    if(kuma_events & KUMA_EV_ERROR) {
        ev |= FD_CLOSE;
    }
    return ev;
}

uint32_t WinPoll::get_kuma_events(uint32_t events)
{
    uint32_t ev = 0;
    if (events & FD_CONNECT) { // writeable
        ev |= KUMA_EV_WRITE;
    }
    if (events & FD_ACCEPT) { // writeable
        ev |= KUMA_EV_READ;
    }
    if(events & FD_READ) {
        ev |= KUMA_EV_READ;
    }
    if(events & FD_WRITE) {
        ev |= KUMA_EV_WRITE;
    }
    if(events & FD_CLOSE) {
        ev |= KUMA_EV_ERROR;
    }
    return ev;
}

void WinPoll::resizePollItems(SOCKET_FD fd)
{
    if (fd >= poll_items_.size()) {
        poll_items_.resize(fd+1);
    }
}

int WinPoll::registerFd(SOCKET_FD fd, uint32_t events, IOCallback& cb)
{
    KUMA_INFOTRACE("WinPoll::registerFd, fd=" << fd << ", events=" << events);
    resizePollItems(fd);
    poll_items_[fd].fd = fd;
    poll_items_[fd].cb = cb;
    WSAAsyncSelect(fd, hwnd_, WM_SOCKET_NOTIFY, get_events(events) | FD_CONNECT);
    return KUMA_ERROR_NOERR;
}

int WinPoll::registerFd(SOCKET_FD fd, uint32_t events, IOCallback&& cb)
{
    KUMA_INFOTRACE("WinPoll::registerFd, fd=" << fd << ", events=" << events);
    resizePollItems(fd);
    poll_items_[fd].fd = fd;
    poll_items_[fd].cb = std::move(cb);
    WSAAsyncSelect(fd, hwnd_, WM_SOCKET_NOTIFY, get_events(events) | FD_CONNECT);
    return KUMA_ERROR_NOERR;
}

int WinPoll::unregisterFd(SOCKET_FD fd)
{
    KUMA_INFOTRACE("WinPoll::unregisterFd, fd="<<fd);
    SOCKET_FD max_fd = poll_items_.size() - 1;
    if (fd < 0 || -1 == max_fd || fd > max_fd) {
        KUMA_WARNTRACE("WinPoll::unregisterFd, failed, max_fd=" << max_fd);
        return KUMA_ERROR_INVALID_PARAM;
    }
    if (fd == max_fd) {
        poll_items_.pop_back();
    } else {
        poll_items_[fd].cb = nullptr;
        poll_items_[fd].fd = INVALID_FD;
    }
    WSAAsyncSelect(fd, hwnd_, 0, 0);
    return KUMA_ERROR_NOERR;
}

int WinPoll::updateFd(SOCKET_FD fd, uint32_t events)
{
    SOCKET_FD max_fd = poll_items_.size() - 1;
    if (fd < 0 || -1 == max_fd || fd > max_fd) {
        KUMA_WARNTRACE("WinPoll::updateFd, failed, fd="<<fd<<", max_fd=" << max_fd);
        return KUMA_ERROR_INVALID_PARAM;
    }
    if(poll_items_[fd].fd != fd) {
        KUMA_WARNTRACE("WinPoll::updateFd, failed, fd="<<fd<<", fd1="<<poll_items_[fd].fd);
        return KUMA_ERROR_INVALID_PARAM;
    }
    return KUMA_ERROR_NOERR;
}

int WinPoll::wait(uint32_t wait_ms)
{
    MSG msg;
    if (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return KUMA_ERROR_NOERR;
}

void WinPoll::notify()
{
    if (hwnd_) {
        ::PostMessage(hwnd_, WM_POLLER_NOTIFY, 0, 0);
    }
}

void WinPoll::on_socket_notify(SOCKET_FD fd, uint32_t events)
{
    int err = WSAGETSELECTERROR(events);
    int evt = WSAGETSELECTEVENT(events);
}

void WinPoll::on_poller_notify()
{

}

IOPoll* createWinPoll() {
    return new WinPoll();
}

//////////////////////////////////////////////////////////////////////////
//
LRESULT CALLBACK km_notify_wnd_proc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch(uMsg)
    {
        case WM_SOCKET_NOTIFY:
        {
            WinPoll* poll = (WinPoll*)GetWindowLong(hwnd, 0);
            if (poll)
                poll->on_socket_notify(wParam, lParam);
            return 0L;
        }
        
        case WM_POLLER_NOTIFY:
        {
            WinPoll* poll = (WinPoll*)GetWindowLong(hwnd, 0);
            if (poll)
                poll->on_poller_notify();
            return 0L;
        }
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

static void initWinClass()
{
    WNDCLASS wc = {0};
    wc.style = 0;
    wc.lpfnWndProc = (WNDPROC)km_notify_wnd_proc;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = sizeof(void*);
    wc.hInstance = NULL;
    wc.hIcon = 0;
    wc.hCursor = 0;
    wc.hbrBackground = 0;
    wc.lpszMenuName = NULL;
    wc.lpszClassName = KM_WIN_CLASS_NAME;
    RegisterClass(&wc);
    
    WSADATA wsaData;
    WORD wVersionRequested = MAKEWORD(1, 1);
    int nResult = WSAStartup(wVersionRequested, &wsaData);
    if (nResult != 0)
    {
        return ;
    }
}

static void uninitWinClass()
{
    UnregisterClass(KM_WIN_CLASS_NAME, NULL);
    WSACleanup();
}

//WBX_Init_Object g_init_obj(poller_load, poller_unload);

KUMA_NS_END
