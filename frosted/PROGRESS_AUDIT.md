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
| CRIT-1  | Critical  | Stack buffer overflow in symlink traversal | fixed       | Remediated with snprintf and bounds check in _fno_search() |
| CRIT-2  | Critical  | Mount table corruption in vfs_umount      | done        |              |
| CRIT-3  | Critical  | NULL pointer deref in sock_accept         | fixed       | Remediated: wolfIP_sock_close now uses sock_fd directly if s is NULL |
| CRIT-4  | Critical  | Timer heap buffer overflow                | fixed       | Bounds check in timers_binheap_insert() |
| CRIT-5  | Critical  | USB RX buffer overflow from input         | fixed       | Bounds check in tusb_net_push_rx (ac829e1) |
| CRIT-6  | Critical  | USB poll out-of-bounds read               | fixed       | Tracked RX size, copy min(sz, actual_size) in ll_usb_poll() [7e9a1b0] |
| CRIT-7  | High      | Off-by-one NUL in _fno_fullpath           | fixed       | NUL terminator written at correct position in vfs.c |
