Final Report for Project 2: User Programs
=========================================

## Changes to initial design document

### Argument Passing

Our initial design used the `tokenizer` from homework 1. However, we found its
implementation to cause page faults and hence changed our design to use `strtok_r`
instead. This meant that our tokenizer code was changed to:

```c
  int argc = 0;
  char *argv[256];
  char *token, *save_ptr;

  token = strtok_r (file_name, " ", &save_ptr);
  while (token != NULL) {
    argv[argc] = token + offset;
    token = strtok_r (NULL, " ", &save_ptr);
    argc++;
  }
  argv[argc] = NULL;
```

### Process Control Syscalls

We made some vital upgrade from our original designs, taking inspiration from what we’ve discussed with our TA, Cory, and the discussion solutions for `wait` and `exit`. Originally we wanted to put the semaphore dead in the thread struct, now we put the semaphore inside the shared struct to avoid interference with regards to multiple children. We also did the address validation using MMU/page fault handler, a different approach than what was envisioned in the original design document. 

Everything else was as true as our words in the original design document: we used a large case-switch to accommodate every syscall, and inside each case, we called the corresponding helper function. We modified `process_exit` and `process_wait` according to the standard solution provided by the discussion. We also modified `thread_create()` and `thread_init()` and faithfully reflect the parent/child relationship between processes, and initialized everything we added in the thread struct e.g. the shared struct `proc_data`. 

One thing to mention is that our shared struct is not exactly the same as the discussion solution: in the discussion we’ve looked at a version using `ref_cnt` to signal child/parent status. However, it was confounding and seemed to require two `sema_down` and `sema_up` in different places, which is detrimental to the robustness of our code base. We decided to abandon `ref_cnt` and instead to free all the shared struct the parent owns once it died. On one hand, if parent died before child gets a chance to run, there would be no memory leak since `proc_data` of child is create in `thread_create` and exists in parent’s list. If on the hand, child died before parent (e.g. `wait`), parent can grab the exit status of the child posthumously from the shared struct.

### File Operation Syscalls

For the most part, we followed our original proposed design. We extended the syscall handler from Task 2 to the file operation syscalls. As suggested by the spec, we used a global lock `file_op_lock` to guarantee thread safety, handled within the helper functions for each syscall. Additionally, each process kept track and denied writes to its current executable and until it finishes.

During our design review, we discussed with our TA two ways to associate fds with files. We had proposed either using a global linked list to store files, or have it done at a per-thread level. Our TA noted the increased memory overhead of the per-thread implementation, but advised us that it would be viable and likely easier. As a result, we had every thread keep track of its own `file_list` and its highest assigned fd.

Additionally, for several syscalls, we realized that we needed to validate the addr of the buffer. We had assumed that this would be handled within the handler itself, but syscalls like `read` and `write` that pass an additional argument needed an additional layer of validation.

## Reflection on project

Feynman was responsible for task 1 and implemented the tokenizer and argument-pushing code in `process_execute`. Both Paolo and Muliang worked on task 2, implementing the other syscalls as well as memory access checking and the synchronization and `struct`s required for `exec` and `wait`. Kevin was responsible for task 3 and implemented the file operation syscalls.

One thing that went particularly well was identification of the project's critical path and an active effort to minimize blocking dependencies. We saw that task 3 relied on task 2's implementation of the syscall mechanism and hence prioritized implementing syscall dispatch. This allowed us to begin work on task 3 before task 2 was completely finished, enabling us to work
efficiently in a distributed manner. We also noticed that implementation of task 2's `exec` syscall depended on the argument passing in task 1 and appropriately prioritized the implementation of task 1.

Most of what could have been done better falls under time management. Despite having all of spring break to complete the project, we ended up using our slip days because we did not coordinate and assign work over break as well as we could have. Additionally, we could have spent more time and effort while writing the design document so that we we would have known the need for a `wait` and `exec` `struct` to allow parent processes to keep track of the execution of child processes. Furthermore, there was some problems which arose from re-using the tokenizer from Homework 1 which blocked work on Task 2. We could have collaborated better to identify this blockage earlier and worked closer together to resolve it.


## Student Testing Report

We’ve added two tests write-buffer and read-buffer regarding invalid buffer sizes for two syscalls --  read() and write(). The two syscalls are file operation syscalls -- write() writes from buffer to a file descriptor and return the bytes actually written, while read() read from a file to buffer and return the bytes actually read.

The basic mechanism of the two test cases is that --  they will try to call read/write with a valid file (sample.txt), a valid buffer pointer, and an abnormally large size of buffer that, if accepted, will allow operations into kernel space. More hideously, the size we (as test creators) passed in is so big that it will overflow (and potentially return a number lower than PHYS_BASE) if one simply add the two arguments up (the pointer to the buffer and size).

If the address validation only checks the buffer pointer, we would allow operations into kernel space. If the validation simply prevent this by adding the arguments up, they overflow and give a seemly valid result that is in user space, thus allowing operations into kernel space. In both cases, our tests cases will report that we read/write 239 bytes (the size of sample.txt), while the correct result should be 0 (or silently exits).

##### Outputs of our Pintos kernel: (write-buffer.output)
```
Copying tests/userprog/write-buffer to scratch partition...
Copying ../../tests/userprog/sample.txt to scratch partition...
qemu -hda /tmp/2rw6xWrN9c.dsk -m 4 -net none -nographic -monitor null
PiLo hda1
Loading...........
Kernel command line: -q -f extract run write-buffer
Pintos booting with 4,088 kB RAM...
382 pages available in kernel pool.
382 pages available in user pool.
Calibrating timer...  419,020,800 loops/s.
hda: 5,040 sectors (2 MB), model "QM00001", serial "QEMU HARDDISK"
hda1: 176 sectors (88 kB), Pintos OS kernel (20)
hda2: 4,096 sectors (2 MB), Pintos file system (21)
hda3: 105 sectors (52 kB), Pintos scratch (22)
filesys: using hda2
scratch: using hda3
Formatting file system...done.
Boot complete.
Extracting ustar archive from scratch device into file system...
Putting 'write-buffer' into the file system...
Putting 'sample.txt' into the file system...
Erasing ustar archive...
Executing 'write-buffer':
(write-buffer) begin
(write-buffer) create "test.txt"
(write-buffer) open "test.txt"
(write-buffer) end
write-buffer: exit(0)
Execution of 'write-buffer' complete.
Timer: 55 ticks
Thread: 0 idle ticks, 54 kernel ticks, 1 user ticks
hda2 (filesys): 114 reads, 221 writes
hda3 (scratch): 104 reads, 2 writes
Console: 1016 characters output
Keyboard: 0 keys pressed
Exception: 0 page faults
Powering off...
```

##### Outputs of our Pintos kernel: (write-buffer.result)
```
PASS
```

##### Outputs of our Pintos kernel: (read-buffer.output)
```
Copying tests/userprog/read-buffer to scratch partition...
Copying ../../tests/userprog/sample.txt to scratch partition...
qemu -hda /tmp/NQgWfvgfDj.dsk -m 4 -net none -nographic -monitor null
PiLo hda1
Loading...........
Kernel command line: -q -f extract run read-buffer
Pintos booting with 4,088 kB RAM...
382 pages available in kernel pool.
382 pages available in user pool.
Calibrating timer...  419,020,800 loops/s.
hda: 5,040 sectors (2 MB), model "QM00001", serial "QEMU HARDDISK"
hda1: 176 sectors (88 kB), Pintos OS kernel (20)
hda2: 4,096 sectors (2 MB), Pintos file system (21)
hda3: 104 sectors (52 kB), Pintos scratch (22)
filesys: using hda2
scratch: using hda3
Formatting file system...done.
Boot complete.
Extracting ustar archive from scratch device into file system...
Putting 'read-buffer' into the file system...
Putting 'sample.txt' into the file system...
Erasing ustar archive...
Executing 'read-buffer':
(read-buffer) begin
(read-buffer) open "sample.txt"
(read-buffer) end
read-buffer: exit(0)
Execution of 'read-buffer' complete.
Timer: 55 ticks
Thread: 0 idle ticks, 54 kernel ticks, 1 user ticks
hda2 (filesys): 92 reads, 214 writes
hda3 (scratch): 103 reads, 2 writes
Console: 976 characters output
Keyboard: 0 keys pressed
Exception: 0 page faults
Powering off...
```

##### Outputs of our Pintos kernel: (read-buffer.result)
```
PASS
```



## Experience writing tests for pintos

Writing tests for the Pintos system felt like writing a firewall or a sandbox, where we had to focus on improving our filtering system for all system calls. At this point of the project we were thinking adversarially and looking for holes in our design, which we thought was already robust after passing the given tests. Before we could start creating our own tests, we had to examine all of the test files to ensure that we were making new ones. Creating a test that our “finished’ system fails to pass also tells us that we were doing the right thing.

Other things needed to be improved in the Pintos system involve corruption within the thread’s stack frame. We had intended to test further with an implementation of a buffer overflow attack. In that attack, we would run a valid user program with a vulnerable buffer that saves user input and overwrite the `eip` in one of the current thread’s stack frame that points to `switch_entry()`. The attack, while not sophisticated, was abandoned due to the considerable chunk of time it required for debugging the user program for the exact addresses and size of the buffer.
