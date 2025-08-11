python
import gdb

class AddEx(gdb.Command):
    """Add a symbol file for the current fno->fname with .gdb suffix."""

    def __init__(self):
        super(AddEx, self).__init__("addex", gdb.COMMAND_USER)

    def invoke(self, arg, from_tty):
        # Get the filename from the current program state
        fname = gdb.parse_and_eval('(char *)fno->fname').string()

        # Build the full path
        full_path = f"userland/gdb/{fname}.gdb"

        # Get relocation addresses
        reloc_text = int(gdb.parse_and_eval('(uintptr_t)reloc_text'))
        reloc_data = int(gdb.parse_and_eval('(uintptr_t)reloc_data'))
        reloc_bss  = int(gdb.parse_and_eval('(uintptr_t)reloc_bss'))

        # Log it for the user
        gdb.write(f"Adding {full_path} @ text=0x{reloc_text:x} data=0x{reloc_data:x} bss=0x{reloc_bss:x}\n")

        # Run add-symbol-file
        gdb.execute(f"add-symbol-file {full_path} {reloc_text} "
                    f"-s .data {reloc_data} "
                    f"-s .bss {reloc_bss}")

# Register the command
AddEx()
end




python
import gdb

def _walk_list(head_val, list_name):
    tasks = []
    visited = set()
    t = head_val
    while int(t) != 0:
        addr = int(t)
        if addr in visited:
            gdb.write(f"[{list_name}] cycle detected at 0x{addr:x}, stopping.\n")
            break
        visited.add(addr)
        tasks.append(t)

        tb = t.dereference()['tb']
        nxt = tb['next']
        if int(nxt) == 0:
            break
        t = nxt
    return tasks

def _fmt32(x):  # 8-digit hex (no 0x)
    return f"{x & 0xFFFFFFFF:08x}"

def _get_field(maybe_ptr, name):
    """Access struct field whether we got a struct or a pointer-to-struct."""
    try:
        return maybe_ptr[name]
    except gdb.error:
        return maybe_ptr.dereference()[name]

def _secure_info(pid):
    """Return dict with secure allocator fields or None if unavailable."""
    try:
        st = gdb.parse_and_eval(f"secure_tasks[{pid}]")
    except gdb.error:
        return None

    # If it's a pointer array and element is NULL, bail early
    try:
        if st.type.code == gdb.TYPE_CODE_PTR and int(st) == 0:
            return None
    except Exception:
        pass

    try:
        mempool_count = int(_get_field(st, 'mempool_count'))
        limits        = _get_field(st, 'limits')
        mem_used      = int(limits['mem_used'])
        mem_max       = int(limits['mem_max'])
        main_segment  = _get_field(st, 'main_segment')
        text_size     = int(main_segment['size'])
        return {
            'mempool_count': mempool_count,
            'mem_used': mem_used,
            'mem_max': mem_max,
            'text_size': text_size,
        }
    except gdb.error:
        return None

class Ps(gdb.Command):
    """List Frosted tasks with PID, exe, stack info, and secure allocator stats."""
    def __init__(self):
        super(Ps, self).__init__("ps", gdb.COMMAND_USER)

    def invoke(self, arg, from_tty):
        for name in ("tasks_running", "tasks_idling"):
            try:
                head = gdb.parse_and_eval(name)  # struct task *
            except gdb.error as e:
                gdb.write(f"{name}: not found ({e})\n")
                continue

            nodes = _walk_list(head, name)
            gdb.write(f"\n{name} ({len(nodes)}):\n")

            for t in nodes:
                tderef = t.dereference()
                tb     = tderef['tb']

                pid = int(tb['pid'])

                # exe name from argv[0] if present
                exe_str = None
                try:
                    argp = tb['arg']
                    if int(argp) != 0:
                        addr = int(t)
                        argv0 = gdb.parse_and_eval(f'(char*)((char**)((struct task*)0x{addr:x})->tb.arg)[0]')
                        if int(argv0) != 0:
                            exe_str = argv0.string()
                except gdb.error:
                    pass

                # stack base & SP
                stack_base = int(tderef['stack'])
                sp         = int(tb['sp'])
                available  = abs(sp - stack_base)  # best-effort (down/up stack safe)

                # secure allocator info
                sinfo = _secure_info(pid)

                gdb.write(f"--> pid {pid}:\t|")
                if pid == 0:
                    gdb.write(" [Frosted kernel, 128KB ] \t|") 
                else:
                    if exe_str:
                        if sinfo and 'text_size' in sinfo:
                            gdb.write(f" {exe_str} (size {sinfo['text_size']} bytes)\t|")
                        else:
                            gdb.write(f" {exe_str}\t|")

                gdb.write(f" Stack base 0x{_fmt32(stack_base)}, SP 0x{_fmt32(sp)}, avail {available} \t|")

                if sinfo:
                    mem_used = sinfo['mem_used']
                    mem_max  = sinfo['mem_max']
                    max_str  = "∞" if mem_max == 0xFFFFFFFF else str(mem_max)
                    gdb.write(f" {sinfo['mempool_count']} slots RAM {mem_used} / {max_str}\t|\n")

Ps()
end


file secure-supervisor/build/secure.elf
target remote :3333
mon reset init
break secure_main
add-symbol-file frosted/build/task0.elf
focus cmd
continue



