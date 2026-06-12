# Lethe

An AMD-V (SVM) hypervisor for Windows 11. It takes an already-running
kernel and transitions it into a guest, so the system keeps running with
hardware virtualization layered underneath it.

I started this as an undergrad project to actually understand how SVM
works in practice, not just on paper. It has grown into a long-running
platform for working with NPT, VMEXIT handling, and low-level
virtualization mechanics. I keep building on it to understand CPU
architecture and OS behavior from underneath.

Built on Windows 11 24H2. Validated multi-core on a Ryzen 9 5900X
(12 cores / 24 threads); every logical processor enters guest mode and
the VMEXIT loop runs on all of them. **NOT** tested on other Windows
versions or other CPUs.

## What's in here

The core pieces, roughly in order of how much they hurt to get right:

* **Bringing SVM (Secure Virtual Machine) up on every logical processor.**
  Per-core setup pinned to each CPU in turn, then a DPC (Deferred
  Procedure Call) fires the launch so all cores enter guest mode within
  a tight window. Most of the difficulty is keeping state consistent
  across cores during that window.
* **A VMCB (Virtual Machine Control Block) save area rebuilt from live
  processor state.** Segments decoded out of the GDT (Global Descriptor
  Table), descriptor tables snapshotted, control registers and EFER
  (Extended Feature Enable Register) mirrored, SYSCALL MSRs (Model
  Specific Registers) preserved, enough that the running kernel resumes
  as a guest without noticing anything changed.
* **Identity-mapped NPT (Nested Page Tables)** built from
  `MmGetPhysicalMemoryRanges`, 4KB everywhere, with lazy on-demand
  installation for any GPA that wasn't covered by the initial walk.
* **VMEXIT (VM Exit) handlers** for MSR, VMMCALL, SVM instructions,
  NPF (Nested Page Fault), #DB (debug exception), INVD (invalidate
  cache without writeback), and WBINVD.
* **A custom VMMCALL hypercall** gated on a key. Wrong key gets a #GP
  (general protection fault) injected back at the guest, which exercises
  the VMEXIT-side event injection path.

## How the launch works

The trick to Type-2 virtualization is that there is not really a separate
guest. The hypervisor grabs whatever code is currently running on the CPU,
copies its state into the VMCB, and `VMRUN`s into a guest whose initial
state is exactly what was already executing. Same RIP, same RSP, same CR3.
The kernel does not notice anything changed. The only real difference is
that the CPU is now in guest mode, so any intercepted instruction trips a
VMEXIT back to the host.

1. `SVM::Initialize` runs as a system thread. For each logical processor
   it allocates a VMCB, a host save area, and a per-vCPU VMM stack, then
   queues a DPC onto the target core for per-core setup.
2. The per-core setup enables `EFER.SVME`, writes `MSR_VM_HSAVE_PA`, and
   populates the VMCB from live CPU state. Segments are decoded out of
   the GDT, descriptor tables stored, control registers and EFER
   mirrored, SYSCALL MSRs preserved.
3. The VMCB's `save.rip` is set to a one-instruction stub
   (`guest_launched`), and `save.rsp` is set to the caller's stack
   pointer.
4. `svm_launch` switches RSP to the VMM stack, VMSAVEs host extended
   state to the host save area, and `VMRUN`s. Control transfers to guest
   mode at `save.rip` with `save.rsp`.
5. The guest stub runs `mov al, 1; ret`. The `ret` pops the return
   address from the caller's saved stack and "returns" from `svm_launch`
   as if it had been a normal function call.
6. The caller sees `svm_launch` return `true`. It has no idea it is now
   running as a guest. Every subsequent VMEXIT bounces control into the
   `vmexit_entrypoint` loop in `Asm/Entrypoint.asm`, which dispatches to
   handlers and eventually re-`VMRUN`s back into the guest.

The reason this works is that VMRUN's guest state includes everything
you would need to resume execution on bare metal: registers, control
registers, segments, paging. If you point those at the same values the
host already has, the transition is invisible to the code that was
running. The guest IS the host, just now running with a new CPU mode and
a hypervisor layered underneath.

## Project layout

```
SVM/
  Asm/Launch.asm        initial VMRUN, host save area setup
  Asm/Entrypoint.asm    VMEXIT entry, VMSAVE/VMLOAD, dispatch
  Svm.cpp               capability checks, per-core init, launch
  Vmcb.cpp              VMCB initialization from live CPU state
  Npt.cpp / Npt.h       identity NPT, allocator, NPF handling
  Handlers/             C++ VMEXIT handlers
HypercallTest/          user-mode test harness
```

## Build

Visual Studio 2022 with the WDK installed, and MASM. Build x64 Release
(Debug also works). Output is `Hv2.sys`.

Enable test signing, then load as a standard kernel service:

```cmd
bcdedit /set testsigning on
sc create lethe type= kernel binPath= "C:\path\to\Hv2.sys"
sc start lethe
```

## Status

What is solid:

* Multi-core SVM bring-up and the VMRUN / VMEXIT loop on Ryzen 9 5900X.
* MSR, VMMCALL, NPF (Nested Page Fault), SVM-instruction, INVD, WBINVD
  intercepts.
* 4KB identity NPT, allocator and table pool.
* `hv2_ping` VMMCALL surface for guest-to-host liveness checks.

What is wobbly:

* **Single-ASID, no TLB tracking.** NPT mutations flush the whole TLB
  via `tlb_control = 3`. Fine for correctness, not great for throughput.

## Things I learned

A few non-obvious things that bit me while building this, in case you are
working on something similar.

**Multicore launch matters.** Type-2 hypervisors have to transition every
logical processor into a guest at roughly the same time, with consistent
state. Without the two-phase setup-then-DPC dance, you will hit clock
watchdog the first time one core sees another diverging.

**Host save area is not optional.** AMD VMEXIT does not restore the
host's FS, GS (segment registers; on x86-64 their bases are used as
per-thread and per-CPU pointers), or KERNEL_GS_BASE for you. If the exit
handler does not VMLOAD the host save area at the very top, it runs with
the guest's GS still active. Windows kernel APIs read `gs:[...]` for the
PCR (Processor Control Region, the per-CPU kernel struct) on every call,
so the second the handler touches one it is reading from the guest's TEB
(Thread Environment Block) instead of the host's, and from there
everything quietly corrupts until something faults far away from the
actual cause. Took me embarrassingly long to find.

**NPT identity at 4KB.** I originally built the identity NPT with 1GB
large pages and split on demand. Worked for a while, then started
bluescreening in ways I could not reproduce. Building 4KB everywhere
upfront costs ~256MB of pool but kills the whole class of bugs.


## References

* **AMD APM Volume 2**, chapters 15 and 16. The actual spec.
* [**SimpleSvm**](https://github.com/tandasat/SimpleSvm) by Satoshi Tanda.
  Cleanest minimal SVM bring-up I have seen, good if you just want to
  get VMCB init right, alongside getting the OS virtualized.
* [**Memhv**](https://github.com/SamuelTulach/memhv) by Samuel Tulach.
  Useful reference for the MSR interception surface.

If you want to use this for something, read the code first.


## License

MIT License

Copyright (c) 2026 Rolen Louie

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

## Third-Party License Notices

Portions of this software contain code derived or adapted from third-party repositories under the MIT License. The original copyright notices are preserved below:

* **SimpleSvm**  
  Copyright (c) Satoshi Tanda. All rights reserved.

* **Memhv**  
  Copyright (c) Samuel Tulach. All rights reserved.
