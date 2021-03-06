#include "debug.h"
#include "kernel/fs.h"
#include "fs/fd.h"
#include "fs/poll.h"
#include "kernel/calls.h"

static int user_read_or_zero(addr_t addr, void *data, size_t size) {
    if (addr == 0)
        memset(data, 0, size);
    else if (user_read(addr, data, size))
        return _EFAULT;
    return 0;
}

dword_t sys_select(fd_t nfds, addr_t readfds_addr, addr_t writefds_addr, addr_t exceptfds_addr, addr_t timeout_addr) {
    STRACE("select(%d, 0x%x, 0x%x, 0x%x, 0x%x)", nfds, readfds_addr, writefds_addr, exceptfds_addr, timeout_addr);
    size_t fdset_size = BITS_SIZE(nfds);
    char readfds[fdset_size];
    if (user_read_or_zero(readfds_addr, readfds, fdset_size))
        return _EFAULT;
    char writefds[fdset_size];
    if (user_read_or_zero(writefds_addr, writefds, fdset_size))
        return _EFAULT;
    char exceptfds[fdset_size];
    if (user_read_or_zero(exceptfds_addr, exceptfds, fdset_size))
        return _EFAULT;

    int timeout = -1;
    if (timeout_addr != 0) {
        struct timeval_ timeout_timeval;
        if (user_get(timeout_addr, timeout_timeval))
            return _EFAULT;
        timeout = timeout_timeval.usec / 1000 + timeout_timeval.sec * 1000;
    }

    // current implementation only works with one fd
    fd_t fd = -1;
    int types = 0;
    for (fd_t i = 0; i < nfds; i++) {
        if (bit_test(i, readfds) || bit_test(i, writefds) || bit_test(i, exceptfds)) {
            if (fd != -1)
                TODO("select with multiple fds");
            fd = i;
            if (bit_test(i, readfds))
                types |= POLL_READ;
            if (bit_test(i, writefds))
                types |= POLL_WRITE;
            /* if (bit_test(i, exceptfds)) */
            /*     FIXME("poll exceptfds"); */
        }
    }

    struct poll *poll = poll_create();
    if (poll == NULL)
        return _ENOMEM;
    if (fd != -1)
        poll_add_fd(poll, f_get(fd), types);
    struct poll_event event;
    int err = poll_wait(poll, &event, timeout);
    poll_destroy(poll);
    if (err < 0)
        return err;

    memset(readfds, 0, fdset_size);
    memset(writefds, 0, fdset_size);
    memset(exceptfds, 0, fdset_size);
    if (fd != -1) {
        if (event.types & POLL_READ)
            bit_set(fd, readfds);
        if (event.types & POLL_WRITE)
            bit_set(fd, writefds);
    }
    if (readfds_addr && user_write(readfds_addr, readfds, fdset_size))
        return _EFAULT;
    if (writefds_addr && user_write(writefds_addr, writefds, fdset_size))
        return _EFAULT;
    if (exceptfds_addr && user_write(exceptfds_addr, exceptfds, fdset_size))
        return _EFAULT;

    return err;
}

dword_t sys_poll(addr_t fds, dword_t nfds, dword_t timeout) {
    STRACE("poll(0x%x, %d, %d)", fds, nfds, timeout);
    struct pollfd_ polls[nfds];
    if (fds != 0 || nfds != 0)
        if (user_read(fds, polls, sizeof(struct pollfd_) * nfds))
            return _EFAULT;
    struct poll *poll = poll_create();
    if (poll == NULL)
        return _ENOMEM;

    // check for bad file descriptors
    // also clear revents, which is reused to mark whether a pollfd has been added or not
    for (int i = 0; i < nfds; i++) {
        if (polls[i].fd >= 0 && f_get(polls[i].fd) == NULL)
            return _EBADF;
        polls[i].revents = 0;
    }

    // convert polls array into poll_add_fd calls
    // FIXME this is quadratic
    for (int i = 0; i < nfds; i++) {
        if (polls[i].fd < 0 || polls[i].revents)
            continue;

        // if the same fd is listed more than once, merge the events bits together
        int events = polls[i].events;
        struct fd *fd = f_get(polls[i].fd);
        polls[i].revents = 1;
        for (int j = 0; j < nfds; j++) {
            if (polls[j].revents)
                continue;
            if (fd == f_get(polls[j].fd)) {
                events |= polls[j].events;
                polls[j].revents = 1;
            }
        }

        poll_add_fd(poll, f_get(polls[i].fd), events);
    }

    struct poll_event event;
    int err = poll_wait(poll, &event, timeout);
    poll_destroy(poll);
    if (err < 0)
        return err;

    for (int i = 0; i < nfds; i++) {
        polls[i].revents = 0;
        if (f_get(polls[i].fd) == event.fd)
            polls[i].revents = polls[i].events & event.types;
    }
    if (fds != 0 || nfds != 0)
        if (user_write(fds, polls, sizeof(struct pollfd_) * nfds))
            return _EFAULT;
    return err;
}

dword_t sys_pselect(fd_t nfds, addr_t readfds_addr, addr_t writefds_addr, addr_t exceptfds_addr, addr_t timeout_addr, addr_t sigmask_addr) {
    // a system call can only take 6 parameters, so the last two need to be passed as a pointer to a struct
    struct {
        addr_t mask_addr;
        dword_t mask_size;
    } sigmask;
    if (user_get(sigmask_addr, sigmask))
        return _EFAULT;
    if (sigmask.mask_size != sizeof(sigset_t_))
        return _EINVAL;
    sigset_t_ mask, old_mask;
    if (user_get(sigmask.mask_addr, mask))
        return _EFAULT;

    do_sigprocmask(SIG_SETMASK_, mask, &old_mask);
    dword_t res = sys_select(nfds, readfds_addr, writefds_addr, exceptfds_addr, timeout_addr);
    do_sigprocmask(SIG_SETMASK_, old_mask, NULL);
    return res;
}
