Linux kernel
============

There are several guides for kernel developers and users. These guides can
be rendered in a number of formats, like HTML and PDF. Please read
Documentation/admin-guide/README.rst first.

In order to build the documentation, use ``make htmldocs`` or
``make pdfdocs``.  The formatted documentation can also be read online at:

    https://www.kernel.org/doc/html/latest/

There are various text files in the Documentation/ subdirectory,
several of them using the Restructured Text markup notation.

Please read the Documentation/process/changes.rst file, as it contains the
requirements for building and running the kernel, and information about
the problems which may result by upgrading your kernel.





PT_PREFETCH
===========
run the following on cloudlabs using the Ubuntu 24.04 OS image and a c6420 machine to get similar results as the paper:

Clone the repo

	git clone git@github.com:jiakevin7/linux.git ~/linux

Compile the new OS image

	cd ~/linux
	cp /boot/config-$(uname -r) .config
	make olddefconfig
	make -j$(nproc)
	sudo make modules_install
	sudo make install
	sudo update-initramfs -c -k "$(make kernelrelease)"
	sudo update-grub
	sudo reboot

Check that the compilation completed and the correct image is being used.

	uname -r

This should print out 6.1+

To run the micro-brenchmarks:

	cd ~/linux/tests
	make
	./run.sh
    
This will run each of the possible modes (control, pte_prefetch, pt_warm, and pte_prefetch with load) 10,000 times. 
The output will be written to .txt files prefixed with their respective test modes.

To run the btree e2e tests:

	cd ~/linux/e2e_tests/btree
	make
	./run.sh

This will run each of the possible modes (control, pte_prefetch, pt_warm, pte_prefetch with load), with both possible values
SYSCALL (0 and 1). The base mode is SYSCALL=0, which has no affect on the program. When SYSCALL=1, the program will call a 
SYSCALL that forces the pte_prefetch and pt_warm data structures to track the given address. Similar to the micro-benchmarks,
the results for the single threaded runs will be written to the st_res directory in .txt files prefixed with their respective 
test modes and postfixed with the SYSCALL mode. The results for the multi threaded runs will be written to the mt_res directory
in the same manner as the single threaded runs.

