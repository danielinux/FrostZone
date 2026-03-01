# Audit Remediation Progress

This file tracks the status of each finding from AUDIT.md as they are addressed. Each entry includes:
- Finding ID (e.g., CRIT-1)
- Description (short)
- Status: [pending|in_progress|fixed]
- Notes/commit reference

## Legend
- **pending**: Not yet started
- **in_progress**: Being actively worked on
- **fixed**: Remediated and committed

## Findings

| ID      | Severity  | Short Description                        | Status      | Notes/Commit |
|---------|-----------|------------------------------------------|-------------|--------------|
| MED-2   | Medium    | DHCP option parsing loop missing bounds   | fixed       | Bounds check added to dhcp_parse_offer() |
| MED-3   | Medium    | Race condition in pipe_close              | fixed       | Mutex added to protect close/free sequence |
| MED-4   | Medium    | Stale pointer in TTY tasklet callback     | fixed       | PID copied to heap for tasklet, callback checks validity |
| HIGH-1  | High      | Unbounded copy in basename_r              | fixed       | Remediated: size param, bound strncpy, all callers updated |
| CRIT-1  | Critical  | Stack buffer overflow in symlink traversal | fixed       | Remediated with snprintf and bounds check in _fno_search() |
| CRIT-2  | Critical  | Mount table corruption in vfs_umount      | fixed       | strncpy 256→sizeof(ep->d_name) in sys_readdir_hdlr |
| CRIT-3  | Critical  | NULL pointer deref in sock_accept         | fixed       | Remediated: wolfIP_sock_close now uses sock_fd directly if s is NULL |
| CRIT-4  | Critical  | Timer heap buffer overflow                | fixed       | Bounds check in timers_binheap_insert() |
| CRIT-5  | Critical  | USB RX buffer overflow from input         | fixed       | Bounds check in tusb_net_push_rx (ac829e1) |
| CRIT-6  | Critical  | USB poll out-of-bounds read               | fixed       | Tracked RX size, copy min(sz, actual_size) in ll_usb_poll() [7e9a1b0] |
| CRIT-7  | High      | Off-by-one NUL in _fno_fullpath           | fixed       | NUL terminator written at correct position in vfs.c |
| HIGH-4  | High      | NULL deref in vfs.c:sys_seek_hdlr (fno->owner) | fixed | Added NULL check for fno->owner in sys_seek_hdlr (see commit for details); Also fixed missing return in sys_ioctl_hdlr (commit 550d589) |
| HIGH-3  | High      | NULL pointer deref in fno_create          | fixed       | fno->flags set only if fno is non-NULL (commit: fix applied) |
| MED-1   | Medium    | Always-false unsigned comparison in sys_sleep_hdlr/sys_alarm_hdlr | fixed | Cast to int32_t before comparison; see commit for details |
