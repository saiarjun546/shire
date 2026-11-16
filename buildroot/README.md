# Shire OS - Buildroot integration (untested)

Shire OS is an existing Linux kernel + BusyBox + the Shire Shell, assembled
by Buildroot. This directory is a Buildroot *external tree* (`BR2_EXTERNAL`)
that adds one package, `campuscompute`, which installs `/usr/bin/shire`
(and registers it in `/etc/shells`).

**Status:** written but NOT built or booted. A full Buildroot image build was
not possible where this repository was completed. Everything under
`campuscompute/` was built and tested on a normal Linux system; only this
packaging layer is unverified.

## Usage (in a Buildroot checkout)
```sh
make BR2_EXTERNAL=/path/to/shire/buildroot qemu_x86_64_defconfig
make menuconfig        # External options -> campuscompute (BR2_PACKAGE_CAMPUSCOMPUTE=y)
make                   # image in output/images/
```
BusyBox is the default in Buildroot; no extra step is needed for it.

To make the Shire Shell the login shell, change root's shell in
`/etc/passwd` to `/usr/bin/shire` (e.g. through a rootfs overlay), or start
`shire` manually from the BusyBox shell after boot. Which of these Shire OS
should do by default is a design decision for the project owner.
