# Domain language

Terms this codebase uses in a specific way. Subsystem-level detail lives with
the subsystem , see `kernel/fs/README.md` for the filesystem object model.

## User address space

**Address space** , one `struct task_vm`: a page table, a table of
reservations, and two arenas. Kernel-only tasks have none. Owned by
`kernel/memory/uvm.{c,h}`.

**Reservation** , a range of user virtual addresses that is legal to touch,
carrying the page flags its pages get when they are materialised. A
reservation is not memory: it holds no frames until something faults.
`struct user_vma` is one record in the table.

**Materialise** (fault in) , turn one page of a reservation into a real
zeroed frame. The only two callers are the page-fault handler and
`uvm_buffer_ok`; both go through `uvm_fault_in`, so both enforce the
reservation's permissions identically. A write into a read-only reservation
is refused rather than mapped.

**Arena** , a bump-allocated region of the address space that hands out
auto-placed ranges. There are two: the mmap arena and the shmem arena. A
released mmap range goes back to the arena's **hole list** for reuse.

**Address-space map** , the fixed layout of the user half (image, framebuffer
window, mmap arena, shmem arena, stack). Constants in `kernel/loader/process.h`.

## Waiting

**Wait queue** , `struct wait_queue` in `kernel/sync/waitqueue.h`: the tasks
parked on one object until its state changes. The producer calls
`wq_wake_all`; each woken task re-checks its condition. Exclusion is
interrupts-off on the BSP, the scheduler's own rule, so check-then-park cannot
lose a wake. Entries live on the waiter's kernel stack and are unlinked when a
parked task is killed. Socket receive, pipes, `poll` and `wait4` use it.

**Standard descriptors** , fds 0-2 hold any object (pipe end, file,
socket), but an *unused* standard slot means the task's console, not a
closed fd. `TASK_FD_TTY` spells the console explicitly when `dup` copies it
elsewhere. There is no fork: a spawned child inherits its parent's fds 0-2
and nothing else, which is how a shell wires a pipeline.

**Signal** , per task, Linux x86_64 semantics as musl expects: pending and
blocked masks on `struct task`, dispositions in `task_context`. Delivered on
every return to ring 3 (syscall exit and IRQ exit), through a Linux
`rt_sigframe` and back via `rt_sigreturn`. A signal interrupts a wait-queue
sleep with `EINTR`; other blocks (VFS gate, futex, `exec`'s wait) are not
interruptible. No job control, no `sigaltstack`, no syscall restart.
`kernel/sched/signal.h` has the full contract.

**User FPU image** , `task_context.user_fx`: the ring-3 x87/SSE state,
saved on every entry from user mode and restored on every return. The kernel
is compiled with SSE, so live registers inside the kernel are never the
user's. Distinct from `fxstate`, which the context switch owns.

**Exit record** , what a child leaves when it exits with nobody blocked on
it: pid, parent, code. The reaper frees the slot as before; `wait4` reads
the record. Bounded, oldest dropped first, cleared when the parent exits.

## Storage

**Block device** , the 512-byte sector interface in
`kernel/drivers/storage/block.h`: read, write, flush, capacity. Two adapters
today, AHCI and USB mass storage. Buffers are ordinary virtual addresses; the
transport owns DMA. Both filesystems mount through it, so any filesystem can
be read from any transport.

**Transport** , what carries sectors to a device (AHCI, USB BOT/SCSI). Distinct
from the filesystem that interprets them.

## USB

**Host controller** , the hardware that drives a USB bus: UHCI (full speed),
EHCI (high speed), xHCI (a stub). Each owns its own registers, descriptor
rings and interrupt handling, and nothing else.

**Pipe** , `struct usb_pipe` in `kernel/drivers/usb/usb_device.h`: a device
addressed on some controller, plus the one primitive the shared code needs, a
control transfer. Everything above that , reading descriptors, assigning an
address, choosing a configuration, finding a HID pointer , is written once in
`usb_device.c` and works on any controller.

**HID pointer** , an interrupt-IN endpoint the kernel polls for mouse input.
Recognised either from a boot-protocol mouse interface, or from a HID report
descriptor exactly 74 bytes long, which is what QEMU's usb-tablet reports.
The match is deliberately narrow because there is no HID report parser.

**Root search** , the boot-time pairing of transports with filesystems in
`kernel/fs/rootfs.c`. Not a fixed order of special cases: it walks the devices
the drivers found and offers each to every registered filesystem until one
recognises the volume. The Multiboot ramdisk is the fallback, not the first
choice, so a machine with a formatted disk boots from the disk.

**Write-through** , FAT's persistence model: every changed sector is written to
the device as it changes, rather than being tracked and written later. ext2
instead marks blocks dirty and writes them at sync. Both cache the whole
volume in RAM.

## Display

**Scanout** , the framebuffer the host or hardware actually displays. A backend
either owns scanout or does not; `kernel/display/framebuffer.c` tracks which.

**Damage** , the accumulated rectangles that changed since the last present.
Accumulated separately in libgaia, in the `SYS_FB_PRESENT` ABI, and in the
kernel, with three different budgets.

## Userspace

**Syscall registry** , `kernel/arch/syscalls.def`: the one assignment of every
syscall name to a number. **Both** entries are visible to libgaia; **kernel**
entries implement Linux-compatible calls that musl issues directly. Aliases
are explicit. A number has one meaning: Linux futex is 202, while Gaia thread
join remains 1102.

**Syscall payload** , a struct copied byte-for-byte across the ring boundary.
All eleven live in `kernel/arch/syscall_abi.h`, included by both sides; there
are no kernel/userspace mirror structs whose field order can drift.

**Heimdall** , the desktop compositor, `userspace/bin/heimdall/`. Clients talk to
it over IPC through **libwm** (`userspace/lib/wm.h`) and never see the wire
protocol.

**libgaia** , the Gaia-specific userspace library: windows, graphics, audio, IPC,
process inspection. Distinct from musl, which supplies the standard C library.
