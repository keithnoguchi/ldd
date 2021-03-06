# Linux Device Drivers in Action

[![CircleCI]](https://circleci.com/gh/keithnoguchi/workflows/ldd)

[LDD] in action!

## Drivers/Modules

### Character Device Drivers

- [open.c](open.c): open(2) and close(2) sample driver
  - [open_test.c](tests/open_test.c): open.c self test
- [read.c](read.c): read(2) sample driver
  - [read_test.c](tests/read_test.c): read.c self test
- [write.c](write.c): write(2) sample driver
  - [write_test.c](tests/write_test.c): write.c self test
- [readv.c](readv.c): readv(2) sample driver
  - [readv_test.c](tests/readv_test.c): readv.c self test
- [writev.c](writev.c): writev(2) sample driver
  - [writev_test.c](tests/writev_test.c): writev.c self test
- [append.c](append.c): open(O_APPEND) sample driver
  - [append_test.c](tests/append_test.c): append.c self test
- [scull.c](scull.c): Simple Character Utility for Loading Localities driver
  - [scull_test.c](tests/scull_test.c): scull.c self test

### Debugging Primitive Test Modules

- [proc.c](proc.c): <linux/proc_fs.h> test module
  - [proc_test.c](tests/proc_test.c): proc.c self test
- [seq.c](seq.c): <linux/seq_file.h> test module
  - [seq_test.c](tests/seq_test.c): seq.c self test
- [faulty.c](faulty.c): dump_stack() test module
  - [faulty_test.c](tests/faulty_test.c): faulty.c self test

### Concurrency Primitive Test Modules

- [sem.c](sem.c): <linux/semaphore.h> test module
  - [sem_test.c](tests/sem_test.c): sem.c self test
- [rwsem.c](rwsem.c): <linux/rwsem.h> test module
  - [rwsem_test.c](tests/rwsem_test.c): rwsem.c self test
- [mutex.c](mutex.c): <linux/mutex.h> test module
  - [mutex_test.c](tests/mutex_test.c): mutex.c self test
- [comp.c](comp.c): <linux/completion.h> test module
  - [comp_test.c](tests/comp_test.c): comp.c self test
- [spinlock.c](spinlock.c): spinlock_t test module
  - [spinlock_test.c](tests/spinlock_test.c): spinlock.c self test
- [rwlock.c](rwlock.c): rwlock_t test module
  - [rwlock_test.c](tests/rwlock_test.c): rwlock.c self test
- [kfifo.c](kfifo.c): <linux/kfifo.h> test module
  - [kfifo_test.c](tests/kfifo_test.c): kfifo.c self test
- [seqlock.c](seqlock.c): <linux/seqlock.h> test module
  - [seqlock_test.c](tests/eqlock_test.c): seqlock.c self test
- [rculock.c](rculock.c): <linux/rcupdate.h> test module
  - [rculock_test.c](tests/rculock_test.c): rculock.c self test

### Advanced Character Device Drivers

- [sleepy.c]: <linux/wait.h> wait queue test module
  - [sleepy_test.c]: [sleepy.c] self test
- [scullpipe.c]: <linux/wait.h> low level wait queue test module
  - [scullpipe_test.c]: [scullpipe.c] self test
- [scullfifo.c]: FIFO version of the scullpipe.c
  - [scullfifo_test.c]: [scullfifo.c] self test
- [poll.c]: select(2), poll(2), and epoll(7) test module
  - [poll_test.c]: [poll.c] self test
- [lseek.c]: lseek(2) test module
  - [lseek_test.c]: [lseek.c] self test

[sleepy.c]: sleepy.c
[scullpipe.c]: scullpipe.c
[scullfifo.c]: scullfifo.c
[poll.c]: poll.c
[lseek.c]: lseek.c
[sleepy_test.c]: tests/sleepy_test.c
[scullpipe_test.c]: tests/scullpipe_test.c
[scullfifo_test.c]: tests/scullfifo_test.c
[poll_test.c]: tests/poll_test.c
[lseek_test.c]: tests/lseek_test.c

### Time, Delays, and Deferred Work

- [hz.c]: Exposing HZ and USER_HZ through /proc file system
  - [hz_test.c]: [hz.c] self test
- [jiffies.c]: Exposing jiffies, jiffies_64 and the like
  - [jiffies_test.c]: [jiffies.c] self test
- [jitbusy.c]: Busy wait based delay module
  - [jitbusy_test.c]: [jitbusy.c] self test
- [jitsched.c]: Schedule based delay module
  - [jitsched_test.c]: [jitsched.c] self test
- [jitqueue.c]: Wait queue based delay module
  - [jitqueue_test.c]: [jitqueue.c] self test
- [jitimer.c]: Kernel timer based delay module
  - [jitimer_test.c]: [jitimer.c] self test
- [jitasklet.c]: Tasklet based delay module
  - [jitasklet_test.c]: [jitasklet.c] self test
- [jiwq.c]: Work queue based delay module
  - [jiwq_test.c]: [jiwq.c] self test

[hz.c]: hz.c
[jiffies.c]: jiffies.c
[jitbusy.c]: jitbusy.c
[jitsched.c]: jitsched.c
[jitqueue.c]: jitqueue.c
[jitschedto.c]: jitschedto.c
[jitimer.c]: jitimer.c
[jitasklet.c]: jitasklet.c
[jiwq.c]: jiwq.c
[hz_test.c]: tests/hz_test.c
[jiffies_test.c]: tests/jiffies_test.c
[jitbusy_test.c]: tests/jitbusy_test.c
[jitsched_test.c]: tests/jitsched_test.c
[jitqueue_test.c]: tests/jitqueue_test.c
[jitschedto_test.c]: tests/jitschedto_test.c
[jitimer_test.c]: tests/jitimer_test.c
[jitasklet_test.c]: tests/jitasklet_test.c
[jiwq_test.c]: tests/jiwq_test.c

### Memory related modules

- [alloc.c]: Various memory allocation functionalities, kmalloc, vmalloc etc.
  - [alloc_test.c]: [alloc.c] self test
- [scullc.c]: Scull backed by the lookaside cache
  - [scullc_test.c]: [scullc.c] self test

[alloc.c]: alloc.c
[scullc.c]: scullc.c
[alloc_test.c]: tests/alloc_test.c
[scullc_test.c]: tests/scullc_test.c

## Build

```sh
$ make
make -C /lib/modules/5.0.6.1/build M=/home/kei/git/ldd modules
make[1]: Entering directory '/home/kei/src/linux-5.0.6'
  CC [M]  /home/kei/git/ldd/scull.o
  CC [M]  /home/kei/git/ldd/sleepy.o
  CC [M]  /home/kei/git/ldd/ldd.o
  CC [M]  /home/kei/git/ldd/sculld.o
  Building modules, stage 2.
  MODPOST 4 modules
  CC      /home/kei/git/ldd/ldd.mod.o
  LD [M]  /home/kei/git/ldd/ldd.ko
  CC      /home/kei/git/ldd/scull.mod.o
  LD [M]  /home/kei/git/ldd/scull.ko
  CC      /home/kei/git/ldd/sculld.mod.o
  LD [M]  /home/kei/git/ldd/sculld.ko
  CC      /home/kei/git/ldd/sleepy.mod.o
  LD [M]  /home/kei/git/ldd/sleepy.ko
make[1]: Leaving directory '/home/kei/src/linux-5.0.6'
```

## Test

```sh
$ sudo make run_tests
make -C /lib/modules/5.0.6.1/build M=/home/kei/git/ldd modules_install
make[1]: Entering directory '/home/kei/src/linux-5.0.6'
  INSTALL /home/kei/git/ldd/ldd.ko
  INSTALL /home/kei/git/ldd/scull.ko
  INSTALL /home/kei/git/ldd/sculld.ko
  INSTALL /home/kei/git/ldd/sleepy.ko
  DEPMOD  5.0.6.1
make[1]: Leaving directory '/home/kei/src/linux-5.0.6'
for mod in scull sleepy ldd sculld; do modprobe $mod; done
make -C tests top_srcdir=/lib/modules/5.0.6.1/build OUTPUT=/home/kei/git/ldd/tests \
        CFLAGS="-I/lib/modules/5.0.6.1/build/tools/testing/selftests -I/home/kei/git/ldd" run_tests
make[1]: Entering directory '/home/kei/git/ldd/tests'
TAP version 13
selftests: tests: sculld_test
========================================
Pass 44 Fail 0 Xfail 0 Xpass 0 Skip 0 Error 0
1..44
ok 1..1 selftests: tests: sculld_test [PASS]
selftests: tests: sleepy_test
========================================
Pass 3 Fail 0 Xfail 0 Xpass 0 Skip 0 Error 0
1..3
ok 1..2 selftests: tests: sleepy_test [PASS]
selftests: tests: ldd_test
========================================
Pass 2 Fail 0 Xfail 0 Xpass 0 Skip 0 Error 0
1..2
ok 1..3 selftests: tests: ldd_test [PASS]
selftests: tests: scull_test
========================================
Pass 28 Fail 0 Xfail 0 Xpass 0 Skip 0 Error 0
1..28
ok 1..4 selftests: tests: scull_test [PASS]
make[1]: Leaving directory '/home/kei/git/ldd/tests'
```

## Cleanup

```sh
sudo make clean
```

Happy Hacking!

[LDD]: https://lwn.net/Kernel/LDD3
[LKD]: https://www.oreilly.com/library/view/linux-kernel-development/9780768696974/
[LKP]: https://www.kernel.org/doc/html/v4.16/process/development-process.html
[LKD2017]: https://go.pardot.com/l/6342/2017-10-24/3xr3f2/6342/188781/Publication_LinuxKernelReport_2017.pdf
[CircleCI]: https://circleci.com/gh/keithnoguchi/ldd.svg?style=svg
