Design Document for Project 1: Threads
======================================

## Group Members

* Pelaiah Blue <pelaiahdavyblue@berkeley.edu>
* Feynman Liang <feynman@berkeley.edu>
* Kevin Lu <klu95@berkeley.edu>
* Muliang Shou <muliang.shou@berkeley.edu>

# Task 1: Argument Passing

## Data Structures and Functions

We will re-use the tokenizer from homework 1. Of particular interest is the
`tokens` data structure

```c
struct tokens {
  size_t tokens_length;
  char **tokens;
  size_t buffers_length;
  char **buffers;
};
```
as well as the `tokenize`, `tokens_get_length`, and `tokens_get_token` function

```c
struct tokens *tokenize(const char *line)
size_t tokens_get_length(struct tokens *tokens);
char *tokens_get_token(struct tokens *tokens, size_t n);
```

In addition, we will make the following modifications to `load()`:

1. Add a `struct tokens *tokens = tokenize(file_name)` to split the command
   string into individual words.
2. Modify the `filesys_open` call on line 230 to use `tokens_get_token(tokens,
   0)` (the name/path of the executable to load)
3. After the call to `setup_stack`, push the tokenized arguments onto the stack
   prior to returning control to `start_process`and simulating the interrupt
   return.

Since `load` calls `filesys_open` to open the provided `file_name` and load the
ELF executable, we will modify `load` to tokenize the provided `file_name`
string into individual words. `filesys_open` will be called on only the first
words, which should correspond to the file name of the executable being called.
After `setup_stack` is reached inside `load`, we will add the argument data to
the stack by first `PUSH`ing the words onto the stack, followed by each of the
strings in right-to-left order, follewed by a sentinel `NULL` pointer. Next, we
will push a `char** argv` pointing to the first (i.e. left-most, last word
pushed onto the stack, file name of executable) argument and finally a `int
argc` corresponding to the number of arguments. Lastly, we will push a fake
return address to satisfy the stack frame structure required by x86 convention.

## Algorithms

The tokenization algorithm defined in `tokenize()` is straightforward,
consuming the input character-by-character and utilizing a finite state machine
to parse the command string into a sequence of words.

Most of the modifications to `load()` are straightforward, with the exception
being how the stack is set up. Following x86 calling convention, we will:

1. `PUSH` the tokenized words corresponding to the arguments in right-to-left
   order onto the stack. These words are obtained by calling
   `tokens_get_token(tokens, n)` where `n` decreases starting at
   `tokens_get_length(tokens)` down to 0.
2. `PUSH` a sentinel `NULL` pointer
3. `PUSH` a `char **argv` pointing to the address of the first (i.e. left-most,
   last word pushed onto the stack, file name of the executable) argument.
4. `PUSH` an `int argc` obtained by calling `tokens_get_tokens`
5. Since x86 calling conventions require a return address, `PUSH` a fake return
   address. We will use a `NULL` pointer for this.

## Synchronization

As `process_execute` only creates and schedules the user process, it does not
possess any code which utilizes shared memory or synchronization mechanisms.
Hence, we do not need to worry about synchronization for this task.

## Rationale

We chose to re-use the tokenizer provided by prior homework because it solves
the task of splitting arguments into individual words and minimizes the amount
of code we need to write, speeding up our development while also reducing
opportunities to introduce bugs.

The was little design decisions to be made for setting up the arguments in the
stack because most of that is dictated by x86 convention. We chose to perform
tokenization and argument stack preparation within `load` because we needed to
split the first argument (the executable’s name/path) for the call to
`filesys_open` but also needed the tokenized arguments for pushing onto the
stack after the call to `setup_stack` returns.

# Task 2: Process Control Syscalls

## Data Structure and Functions

Add to `thread.h`

```c
struct return_status {
  // To allow parent process keep the ret_code of its child, even if child is terminated
  // This is usually NULL, only initialized when the thread is (1) a child thread (2) terminated in thread_exit().
  Int tid;
  Int ret_code;
  Struct list_elem elem;
};
```

Modify `struct thread` in `thread.h`

```c
#ifdef USERPROG
  ...
  struct semaphore * child_sema; // Initialized by process_wait()
  bool is_waited_by_parent = False; // Modified by process_wait()
  struct thread* parent;
  int ret_code;
  struct list children;
  struct list children_return_status; // contains struct return_status
#endif
```

Add a helper function

```c
int thread_get_return_status(tid_t)
```
that finds and returns the `struct return_status` of the designated child thread
(by iterating over `child_return_status`). If child doesn’t exist, return `NULL`.

Modify `thread_create` in `thread.c`

```c
(The previous line is tid = t->tid = allocate_tid(); )
#ifdef USERPROG
  // Add the child thread to its parent’s children list
#endif
```

Modify `init_thread` in `thread.c`

```c
// (The previous line is t->magic = THREAD_MAGIC; )
#ifdef USERPROG
  t->child_sema = NULL; // Lazy initialization in process_wait()
  Initialize list children, list children_return_status
#endif
```

Modify `thread_exit` in `thread.c`

```c
#ifdef USERPROG
  If (thread has a parent) {
  create a return_status struct, and push it onto parent’s return_status list.
  }
  Call sema_up on parent’s child_sema
...
#endif
```

Modification in `syscall.c`

```c
static int syscall_noa[NOA] /*Array containing number of arguments for every syscall. */
static void (* syscall_function[n]) (void *) /* Array containing pointers to every syscall helper func. */

int practice (int i, struct intr_frame *f UNUSED ) /*increments integer  argument by 1 and returns it to the user*/

void halt (void* arg UNUSED, struct intr_frame *f UNUSED) /*Terminates Pintos by calling shutdown_power_off()*/
{
  Shutdown_power_off ();
}

void exit (int status, struct intr_frame *f) /*Terminates the current user program, returning status to the kernel.*/
{
  f->eax = status;
  Syscall_thread_exit (thread_current() -> name, status);
}

pid t exec (const char* cmd_line, struct intr_frame *f) /*Runs the executable whose name/arguments is given in args, and returns the new process's program id (pid) by setting f->eax. */
{
  // Call process_execute()
}

int wait (pid_t pid, struct intr_frame *f)/* Waits for a child process pid and retrieves the child's exit status.*/
{
  // See Algorithm part for detailed analysis
}

// modification
void syscall_init(void) {
  …
  syscall_functions[SYS_HALT] = &syscall_halt;
  (... Initialize for all syscall helper functions ...)
  syscall_noa[SYS_PRACTICE] = &syscall_practice;

  syscall_noa[SYS_HALT] = 0;
  (... Similarly, initialize for every syscall ...)
  (Numbers of arguments are given in lib/user/syscall-nr.h)
  syscall_noa[SYS_PRACTICE] = 1;
  ...
}

void syscall_thread_exit (char* proc_name, int exit_code) /*Exit the current user process. */
{
  printf(“%s: exit(%d)\n”, proc_name, exit_code);
  thread_exit(); //See thread.c
}

void validate_ptr (const void * uaddr)  /* Validates user pointer. This is approach 1, not modifying exception handlers. */
{
  // See Algorithms for detailed explanation
  bool valid = (uaddr) &                                  // null check
              (is_user_addr(uaddr)) &                    // check if in user space
              (pagedir_get_page (active_pd(), uaddr))    // check if unmapped
  if (!valid) {
  call syscall_thread_exit and kills user proc.
  }
  return;
}

int* syscall_retrieve_args (struct intr_frame *f) /* Gets arguments from the stack. Kills the user process if read fails/not enough arguments read. */
// See Algorithms for detailed explanation

// modification
static void syscall_handler (struct intr_frame *f) {
  ...
  int syscall_number = args[0]
  if ((syscall_number < SYS_HALT) | (syscall_number > SYS_PRACTICE)) {
     syscall_thread_exit(thread_current () -> name, -1);
  }
  int *argv = syscall_retrieve_args(f); // This allocates dynamic memory for argv
  syscall_functions[syscall_number] (argv, f);
  free(argv);
}

/* Modification in process.c. */
// New implementation
int process_wait (tid_t child_tid) in process.c
{
  struct semaphore *child_is_alive;
  struct thread *child;
  struct thread *parent = thread_current ();
  struct list_elem *e;
  …
  // See Algorithms for detailed explanation
}
```

## Algorithms

The general workflow of handling a syscall is:

1. Reading from `$esp`
2. Validate user pointers
3. Executing the syscall by calling corresponding syscall helper functions
4. Put the return value on `frame->eax` (some syscall may not return)

### Reading from `$esp`

To begin with, reading from `$esp` is straightforward (see skeleton example). The
trick here is:

1. To validate that user wanted a valid syscall by checking if the syscall
   number is in range `[SYS_HALT, SYS_PRACTICE]`.
2. To accommodate for different number of arguments for various syscalls. This
   is handled by `syscall_retrieve_args()`, which looks for the correct number of
   arguments in the array `syscall_noa[NOA]` and read from `$esp`. If read
   fails/not enough arguments read, calls `syscall_thread_exit()` and kills the
   user process.

### Validating user pointers

One step down the workflow, we need to validate user pointers.  There are two
ways to do this as specified in the Project Specs, and we choose to go along
the "verify before deference" route. Thus, we don’t need to modify `exception.c`
(...for now). The idea is to have a helper method checking:

1. If the user pointer is `NULL`
2. If the pointer is below kernel address
3. If the pointer gives an unmapped address.

Those checks are handled by the provided functions `is_user_vaddr()` and
`pagedir_get_page()`. This validation should be put before any assignment of
locks, so that any "zombie" process hoarding a lock will never have acquired
the lock in the first place.

If user process doesn’t survive the validation, we kill it and return its
return code to the kernel. We also retain its `exit_code` in the aforementioned
`return_status` struct, so that the process can posthumously inform its parent of
its status.

### Executing the actual syscall

Finally, we need to execute syscalls. For simpler syscalls i.e. `HALT`, `PRACTICE`,
`EXIT` etc. we can run the helper method in `syscall.c`. For `WAIT` and `EXEC`, we call
`process_wait()` and `process_execute()` in `process.c`, and returns the result in
`$eax` respectively. Since `process_wait()` is not implemented, here’s a pseudocode
explanation of how it should work:

```c
process_wait(tid_t child_tid) {
  Iterate through parent’s children list and look for the child with the correct tid;
  If a direct child with the tid is not found, return -1;
  If (child found && child->is_waited_by_parent) return -1; // already waiting
  If (there is a struct return_status for the child) { //This means that the child is already killed
    return child’s ret_code in return_status;
  }
  // Sanity check done; now we wait for child process
  child->is_waited_by_parent = True;
  Initialize child_sema; // A semaphore that allows parent to wait for child
  Int ret_code = -1;
  If (child->status != IS_DYING) {
    sema_down(child_sema);
    // sema_up is called in thread_exit()
    Grab child’s return code;
  }
  free (child_sema);
  Return ret_code;
}
```

In other words, after some sanity checks, parent will wait on child by
`sema_down()`. When child terminates, it will call `sema_up()` during
`thread_exit()`, and thus unblocking the parent process.

To wrap up and accommodate for the functionalities above, we need to modify the
members of `struct thread`, `thread_create()`, `thread_exit()` and `init_thread()`.

## Synchronization

We used a semaphore for each parent process-child process pair. This enables
parent to wait for child’s death and acquire its `ret_code`, even after child is
killed by kernel or terminated naturally. The semaphore is down-ed by parent,
in `process_wait()`, and up-ed by child, in `thread_exit()`.

We also decides to put the validation of `$esp` (user stack pointer) before any
lock assignment. This is to promote compatibility with regards to Part 3.
file-system syscalls.

## Rationale

We’ve made quite a few design decisions regarding this part, namely
1. Using the "validate before deref." way to check user-passed stack pointers
2. Modify thread system in order to retain children’s ret_code in a posthumus fashion
3. Calling process methods to deal with `SYS_WAIT` and `SYS_EXEC `
4. Compared to the project spec, the Syscall functions have an extra argument `intr_frame*`.

We feel like this is the best way to implement this project after considering
some of the alternatives. For (1), we could’ve handled the dereference in
exception handlers and gain a speed boost through MMU. However, this is not as
straightforward and will create extra work for filesys lock implementations and
`process_exit()`, since we need to handle locks. For (4), the reason is to
accommodate for the array of syscall functions. We can instead have a huge
switch-case so that the Syscall functions look exactly like specified, but that
will create a lot of overhead when expanding our set of syscalls.


# Task 3: File Operation Syscalls

## Data Structures and Functions
```c
/* Lock on filesystem operations. */
static struct lock file_operation_lock;
```

```c
void thread_init(void) {
…
lock_init(&file_operation_lock);
}
```

```c
struct thread {
…
struct file *program_file; /* running program file */
}
```

```c
bool load (const char *file_name, void (**eip) (void), void **esp) {
struct thread *t = thread_current();
t->program_file = file_name;
file_deny_write(file_name);
…
}
```

```c
void process_exit(void) {
struct thread *cur = thread_current();
file_allow_write(cur->program_file);
…
}
```

## Algorithms

We will extend the implementation of task 2 to the following file operations
syscalls, each of which will leverage its respective function in the file
system library:

- `SYS_CREATE` calls `filesys_create`
- `SYS_REMOVE` calls `filesys_remove`
- `SYS_OPEN` calls `filesys_open`
- `SYS_FILESIZE` calls `file_length`
- `SYS_READ` calls `file_read`
- `SYS_WRITE` calls `file_write`
- `SYS_SEEK` calls `file_seek`
- `SYS_TELL` calls `file_tell`
- `SYS_CLOSE` calls `file_close`

The handler will call a helper function for each syscall. The helper function
will do the following:

 - Acquire the lock
 - Call the appropriate file system function
 - Release the lock and return

An edge case is fd 0 and fd 1; they should read/write to STDIN/STDOUT
respectively.

To prevent running program files from being modified, we store the pointer to
the file to `program_file` in the `thread` struct. When `load` is called to
load the executable into the current thread, we additionally store the pointer
to the executable in the thread struct and call `file_deny_write` to disable
writes. It should be noted that the file should not be closed (because
`file_deny_write` will fail to deny writes to the file if the file is closed).
When the process is finished and `process_exit` is called, we get the
executable from the current thread and call `file_allow_write` to reenable
writes. We can then close the file here.

## Synchronization

As suggested by the spec, we use a global lock on filesystem operations to
ensure thread safety. All file operation syscalls must first acquire the lock
before proceeding, and will release the lock when it is done.

While a user process is running, we ensure that nobody can modify its
executable on disk by calling disabling writes to the file. We leverage
`file_deny_write` and `file_allow_write` to disable and reenable writes
respectively.


## Rationale

Our implementation closely follows guidelines listed in the spec; we feel this
is the simplest implementation and will make it easy to extend our design to
project 3 (as it will likely depend on this project).

As noted, using a global lock on file system operations may not be ideal later
on, but for the time being, there is no need for more sophisticated
synchronization. The rest of our implementation is pretty straightforward;
because the file operations are already provided for us, it makes no sense to
implement them ourselves. Our syscalls simply call the appropriate functions in
the file system library. Likewise, to deny writes to current-running program
files, we simply use the suggested functions `file_deny_write` and
`file_allow_write`.


# Additional Questions

> Take a look at the Project 2 test suite in pintos/src/tests/userprog. Some of
> the test cases will intentionally provide invalid pointers as syscall
> arguments, in order to test whether your implementation safely handles the
> reading and writing of user process memory. Please identify a test case that
> uses an invalid stack pointer ($esp) when making a syscall. Provide the name of
> the test and explain how the test works. (Your explanation should be very
> specific: use line numbers and the actual names of variables when explaining
> the test case.)

One of the test case, `bad-jump2.c`, attempts to pass in an invalid `$esp`
(`0xC0000000`, which is kernel territory) at line 11.

> Please identify a test case that uses a valid stack pointer when making a
> syscall, but the stack pointer is too close to a page boundary, so some of
> the syscall arguments are located in invalid memory. (Your implementation
> should kill the user process in this case.) Provide the name of the test and
> explain how the test works. (Your explanation should be very specific: use
> line numbers and the actual names of variables when explaining the test
> case.)

Test case `write-bad-ptr.c` passes in stack pointer `0x10123420` and an
argument "123" indicating the buffer size at line 14. The pointer itself points
to a valid user address, but it’s too close to the next page boundary to fit in
123 char pointers, or 492 bytes. More explicitly, since we have 4KB = 4096
bytes = 0x1000 bytes per page, the next page boundary lies at `0x10124000`.
However, to fit in 123 char pointers we will reach `0x10123420` + `(123x4x8)DEC` =
`0x10124380` > `0x10124000`.

> Identify one part of the project requirements which is not fully tested by
> the existing test suite. Explain what kind of test needs to be added to the
> test suite, in order to provide coverage for that part of the project. (There
> are multiple good answers for this question.)

  The user could have passed in a buffer than spans between the user space
  and kernel space (it starts in user space but ends in kernel space, so if
  you only check the head of the buffer, it will come out as valid). This is
  similar to `write-bad-ptr.c`, but the difference is that the buffer ends up
  in kernel address space instead of unmapped pages. We can pass in a buffer
  starting at (`0xC0000000` - `0x1`) and have a size of >=2  to provide coverage
  for this test case.

### GDB Questions

> Set a break point at process_execute and continue to that point. What is the
> name and address of the thread running this function? What other threads are
> present in pintos at this time? Copy their struct threads. (Hint: for the last
> part dumplist &alllist thread allelem may be useful.)

The thread running `process_execute` is called `main` and has address
`0xc000e000`. The other threads present at this time are:

```c
(gdb) dumplist &all_list thread allelem
pintos-debug: dumplist #0: 0xc000e000 {tid = 1, status = THREAD_RUNNING, name = "main", '
\000' <repeats 11 times>, stack = 0xc000ee0c "\210", <incomplete sequence \357>, priority
 = 31, allelem = {prev = 0xc0034b50 <all_list>, next = 0xc0104020}, elem = {prev = 0xc003
4b60 <ready_list>, next = 0xc0034b68 <ready_list+8>}, pagedir = 0x0, magic = 3446325067}
pintos-debug: dumplist #1: 0xc0104000 {tid = 2, status = THREAD_BLOCKED, name = "idle", '
\000' <repeats 11 times>, stack = 0xc0104f34 "", priority = 0, allelem = {prev = 0xc000e0
20, next = 0xc0034b58 <all_list+8>}, elem = {prev = 0xc0034b60 <ready_list>, next = 0xc00
34b68 <ready_list+8>}, pagedir = 0x0, magic = 3446325067}
```

> What is the backtrace for the current thread? Copy the backtrace from gdb as
> your answer and also copy down the line of c code corresponding to each
> function call.

```
#0  process_execute (file_name=file_name@entry=0xc0007d50 "args-none") at ../../userprog/
process.c:32
```

This corresponds to the location of the current breakpoint.

---

```
#1  0xc002025e in run_task (argv=0xc0034a0c <argv+12>) at ../../threads/init.c:288
```
This corresponds to the function call


```c
  process_wait (process_execute (task));
```

---

```
#2  0xc00208e4 in run_actions (argv=0xc0034a0c <argv+12>) at ../../threads/init.c:340
```

This corresponds to the function call

```c
      a->function (argv);
```

---

```
#3  main () at ../../threads/init.c:133
```

This corresponds to the function call

```c
  run_actions (argv);
```

> Set a breakpoint at start_process and continue to that point. What is the name
> and address of the thread running this function? What other threads are present
> in pintos at this time? Copy their struct threads.

The thread running this function is called `args-none` and has address
`0xc010a000`, corresponding to a user rather than kernel thread. All the
threads running at this time are:

```c
pintos-debug: dumplist #0: 0xc000e000 {tid = 1, status = THREAD_BLOCKED, name = "main", '
\000' <repeats 11 times>, stack = 0xc000eebc "\001", priority = 31, allelem = {prev = 0xc
0034b50 <all_list>, next = 0xc0104020}, elem = {prev = 0xc0036554 <temporary+4>, next = 0
xc003655c <temporary+12>}, pagedir = 0x0, magic = 3446325067}
pintos-debug: dumplist #1: 0xc0104000 {tid = 2, status = THREAD_BLOCKED, name = "idle", '
\000' <repeats 11 times>, stack = 0xc0104f34 "", priority = 0, allelem = {prev = 0xc000e0
20, next = 0xc010a020}, elem = {prev = 0xc0034b60 <ready_list>, next = 0xc0034b68 <ready_
list+8>}, pagedir = 0x0, magic = 3446325067}
pintos-debug: dumplist #2: 0xc010a000 {tid = 3, status = THREAD_RUNNING, name = "args-non
e\000\000\000\000\000\000", stack = 0xc010afd4 "", priority = 31, allelem = {prev = 0xc01
04020, next = 0xc0034b58 <all_list+8>}, elem = {prev = 0xc0034b60 <ready_list>, next = 0x
c0034b68 <ready_list+8>}, pagedir = 0x0, magic = 3446325067}
```

> Where is the thread running start_process created? Copy down this line of code.

The thread running `start_process` is created in `process_execute`, on this line of code:

```c
  tid = thread_create (file_name, PRI_DEFAULT, start_process, fn_copy);
```

> Continue one more time. The userprogram should cause a page fault and thus cause the page fault handler to be executed. It’ll look something like
>     [Thread <main>] #1 stopped.
>     pintos-debug: a page fault exception occurred in user mode
>     pintos-debug: hit ’c’ to continue, or ’s’ to step to intr_handler
>     0xc0021ab7 in intr0e_stub ()
> Please find out what line of our user program caused the page fault. Don’t worry if it’s just an hex address. (Hint: btpagefault may be useful)

```c
(gdb) btpagefault
#0  0x0804870c in ?? ()
```

The reason why btpagefault returns an hex address is because pintos-gdb
build/kernel.o only loads in the symbols from the kernel. The instruction that
caused the page fault is in our userprogram so we have to load these symbols
into gdb. To do this use loadusersymbols build/tests/userprog/args-none. Now do
btpagefault again and copy down the results.

> Why did our user program page fault on this line?

```c
(gdb) loadusersymbols build/tests/userprog/args-none
add symbol table from file "build/tests/userprog/args-none" at
        .text_addr = 0x80480a0
(gdb) btpagefault
#0  _start (argc=<error reading variable: can't compute CFA for this frame>, argv=<error
reading variable: can't compute CFA for this frame>) at ../../lib/user/entry.c:9
```
This shows that the page fault occurred when calling `main` from the `_start`
entrypoint in `user/entry.c`. The page fault occurs because x86 calling
convention requires pointers to arguments to be directly above the current
stack pointer (which is currently set to all zeros by `setup_stack`). Trying to
dereference this zero pointer results in accessing unmapped virtual memory,
producing a page fault.

