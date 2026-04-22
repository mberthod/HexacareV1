<!-- Source: _sources/api-guides/core_dump_internals.rst.txt (ESP-IDF v6.0 documentation) -->

# Anatomy of Core Dump Image

Core dump files are generated in ELF format, which provides comprehensive information regarding the software's state at the moment the crash occurs, including CPU registers and memory contents.

The memory state embeds a snapshot of all tasks mapped in the memory space of the program. The CPU state contains register values when the core dump has been generated. The core dump file uses a subset of the ELF structures to register this information.

Loadable ELF segments are used to store the process' memory state, while ELF notes (`ELF.PT_NOTE`) are used to store the process' metadata (e.g., PID, registers, signal etc). In particular, the CPU's status is stored in a note with a special name and type (`CORE`, `NT_PRSTATUS type`).

Here is an overview of the core dump layout:

<figure>
<img src="../../_static/core_dump_format_elf.png" class="align-center" alt="../../_static/core_dump_format_elf.png" />
<figcaption>Core Dump ELF Image Format</figcaption>
</figure>

> **Note**
>
> # Overview of Implementation

The figure below describes some basic aspects related to the implementation of the core dump:

<figure>
<img src="../../_static/core_dump_impl.png" class="align-center" alt="../../_static/core_dump_impl.png" />
<figcaption>Core Dump Implementation Overview</figcaption>
</figure>

> **Note**
>
> 