# Linux Device Driver in Action

[![CircleCI]](https://circleci.com/gh/keinohguchi/workflows/ldd)

[LDD] in action!

## Drivers

### Character drivers

- [open.c](open.c): open(2) and close(2) example driver
  - [open_test.c](tests/open_test.c): open.c unit test
- [read.c](read.c): read(2) example driver
  - [read_test.c](tests/read_test.c): read.c unit test

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
$ sudo make test
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
[CircleCI]: https://circleci.com/gh/keinohguchi/ldd.svg?style=svg
