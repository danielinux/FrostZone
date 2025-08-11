/*
 *      This file is part of frostzone.
 *
 *      frostzone is free software: you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License version 2, as
 *      published by the Free Software Foundation.
 *
 *
 *      frostzone is distributed in the hope that it will be useful,
 *      but WITHOUT ANY WARRANTY; without even the implied warranty of
 *      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *      GNU General Public License for more details.
 *
 *      You should have received a copy of the GNU General Public License
 *      along with frostzone.  If not, see <http://www.gnu.org/licenses/>.
 *
 *      Authors: Daniele Lacamera
 *
 */
/* The file syscall_table.c is auto generated. DO NOT EDIT, CHANGES WILL BE LOST. */
/* If you want to add syscalls, use syscall_table_gen.py  */

#include "frosted.h"
#include "sys/frosted.h"
/* External handlers (defined elsewhere) : */ 
extern int sys_sleep_hdlr(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
extern int sys_ptsname_hdlr(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
extern int sys_getpid_hdlr(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
extern int sys_getppid_hdlr(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
extern int sys_open_hdlr(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
extern int sys_close_hdlr(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
extern int sys_read_hdlr(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
extern int sys_write_hdlr(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
extern int sys_seek_hdlr(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
extern int sys_mkdir_hdlr(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
extern int sys_unlink_hdlr(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
extern int sys_mmap_hdlr(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
extern int sys_munmap_hdlr(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
extern int sys_opendir_hdlr(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
extern int sys_readdir_hdlr(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
extern int sys_closedir_hdlr(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
extern int sys_stat_hdlr(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
extern int sys_poll_hdlr(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
extern int sys_ioctl_hdlr(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
extern int sys_link_hdlr(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
extern int sys_chdir_hdlr(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
extern int sys_getcwd_hdlr(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
extern int sys_sem_init_hdlr(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
extern int sys_sem_post_hdlr(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
extern int sys_sem_wait_hdlr(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
extern int sys_sem_trywait_hdlr(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
extern int sys_sem_destroy_hdlr(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
extern int sys_mutex_init_hdlr(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
extern int sys_mutex_unlock_hdlr(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
extern int sys_mutex_lock_hdlr(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
extern int sys_mutex_destroy_hdlr(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
extern int sys_socket_hdlr(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
extern int sys_bind_hdlr(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
extern int sys_accept_hdlr(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
extern int sys_connect_hdlr(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
extern int sys_listen_hdlr(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
extern int sys_sendto_hdlr(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
extern int sys_recvfrom_hdlr(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
extern int sys_setsockopt_hdlr(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
extern int sys_getsockopt_hdlr(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
extern int sys_shutdown_hdlr(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
extern int sys_dup_hdlr(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
extern int sys_dup2_hdlr(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
extern int sys_mount_hdlr(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
extern int sys_umount_hdlr(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
extern int sys_kill_hdlr(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
extern int sys_isatty_hdlr(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
extern int sys_exec_hdlr(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
extern int sys_ttyname_hdlr(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
extern int sys_exit_hdlr(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
extern int sys_tcsetattr_hdlr(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
extern int sys_tcgetattr_hdlr(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
extern int sys_tcsendbreak_hdlr(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
extern int sys_pipe2_hdlr(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
extern int sys_sigaction_hdlr(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
extern int sys_sigprocmask_hdlr(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
extern int sys_sigsuspend_hdlr(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
extern int sys_vfork_hdlr(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
extern int sys_waitpid_hdlr(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
extern int sys_lstat_hdlr(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
extern int sys_uname_hdlr(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
extern int sys_getaddrinfo_hdlr(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
extern int sys_freeaddrinfo_hdlr(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
extern int sys_fstat_hdlr(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
extern int sys_getsockname_hdlr(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
extern int sys_getpeername_hdlr(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
extern int sys_readlink_hdlr(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
extern int sys_fcntl_hdlr(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
extern int sys_setsid_hdlr(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
extern int sys_ptrace_hdlr(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
extern int sys_reboot_hdlr(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
extern int sys_getpriority_hdlr(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
extern int sys_setpriority_hdlr(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
extern int sys_ftruncate_hdlr(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
extern int sys_truncate_hdlr(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
extern int sys_pthread_create_hdlr(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
extern int sys_pthread_exit_hdlr(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
extern int sys_pthread_join_hdlr(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
extern int sys_pthread_detach_hdlr(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
extern int sys_pthread_cancel_hdlr(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
extern int sys_pthread_self_hdlr(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
extern int sys_pthread_setcancelstate_hdlr(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
extern int sys_sched_yield_hdlr(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
extern int sys_pthread_mutex_init_hdlr(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
extern int sys_pthread_mutex_destroy_hdlr(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
extern int sys_pthread_mutex_lock_hdlr(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
extern int sys_pthread_mutex_trylock_hdlr(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
extern int sys_pthread_mutex_unlock_hdlr(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
extern int sys_pthread_kill_hdlr(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
extern int sys_clock_settime_hdlr(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
extern int sys_clock_gettime_hdlr(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
extern int sys_pthread_key_create_hdlr(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
extern int sys_pthread_setspecific_hdlr(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
extern int sys_pthread_getspecific_hdlr(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
extern int sys_alarm_hdlr(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
extern int sys_ualarm_hdlr(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);

void syscalls_init(void) {
	sys_register_handler(0, sys_sleep_hdlr);
	sys_register_handler(1, sys_ptsname_hdlr);
	sys_register_handler(2, sys_getpid_hdlr);
	sys_register_handler(3, sys_getppid_hdlr);
	sys_register_handler(4, sys_open_hdlr);
	sys_register_handler(5, sys_close_hdlr);
	sys_register_handler(6, sys_read_hdlr);
	sys_register_handler(7, sys_write_hdlr);
	sys_register_handler(8, sys_seek_hdlr);
	sys_register_handler(9, sys_mkdir_hdlr);
	sys_register_handler(10, sys_unlink_hdlr);
	sys_register_handler(11, sys_mmap_hdlr);
	sys_register_handler(12, sys_munmap_hdlr);
	sys_register_handler(13, sys_opendir_hdlr);
	sys_register_handler(14, sys_readdir_hdlr);
	sys_register_handler(15, sys_closedir_hdlr);
	sys_register_handler(16, sys_stat_hdlr);
	sys_register_handler(17, sys_poll_hdlr);
	sys_register_handler(18, sys_ioctl_hdlr);
	sys_register_handler(19, sys_link_hdlr);
	sys_register_handler(20, sys_chdir_hdlr);
	sys_register_handler(21, sys_getcwd_hdlr);
	sys_register_handler(22, sys_sem_init_hdlr);
	sys_register_handler(23, sys_sem_post_hdlr);
	sys_register_handler(24, sys_sem_wait_hdlr);
	sys_register_handler(25, sys_sem_trywait_hdlr);
	sys_register_handler(26, sys_sem_destroy_hdlr);
	sys_register_handler(27, sys_mutex_init_hdlr);
	sys_register_handler(28, sys_mutex_unlock_hdlr);
	sys_register_handler(29, sys_mutex_lock_hdlr);
	sys_register_handler(30, sys_mutex_destroy_hdlr);
	sys_register_handler(31, sys_socket_hdlr);
	sys_register_handler(32, sys_bind_hdlr);
	sys_register_handler(33, sys_accept_hdlr);
	sys_register_handler(34, sys_connect_hdlr);
	sys_register_handler(35, sys_listen_hdlr);
	sys_register_handler(36, sys_sendto_hdlr);
	sys_register_handler(37, sys_recvfrom_hdlr);
	sys_register_handler(38, sys_setsockopt_hdlr);
	sys_register_handler(39, sys_getsockopt_hdlr);
	sys_register_handler(40, sys_shutdown_hdlr);
	sys_register_handler(41, sys_dup_hdlr);
	sys_register_handler(42, sys_dup2_hdlr);
	sys_register_handler(43, sys_mount_hdlr);
	sys_register_handler(44, sys_umount_hdlr);
	sys_register_handler(45, sys_kill_hdlr);
	sys_register_handler(46, sys_isatty_hdlr);
	sys_register_handler(47, sys_exec_hdlr);
	sys_register_handler(48, sys_ttyname_hdlr);
	sys_register_handler(49, sys_exit_hdlr);
	sys_register_handler(50, sys_tcsetattr_hdlr);
	sys_register_handler(51, sys_tcgetattr_hdlr);
	sys_register_handler(52, sys_tcsendbreak_hdlr);
	sys_register_handler(53, sys_pipe2_hdlr);
	sys_register_handler(54, sys_sigaction_hdlr);
	sys_register_handler(55, sys_sigprocmask_hdlr);
	sys_register_handler(56, sys_sigsuspend_hdlr);
	sys_register_handler(57, sys_vfork_hdlr);
	sys_register_handler(58, sys_waitpid_hdlr);
	sys_register_handler(59, sys_lstat_hdlr);
	sys_register_handler(60, sys_uname_hdlr);
	sys_register_handler(61, sys_getaddrinfo_hdlr);
	sys_register_handler(62, sys_freeaddrinfo_hdlr);
	sys_register_handler(63, sys_fstat_hdlr);
	sys_register_handler(64, sys_getsockname_hdlr);
	sys_register_handler(65, sys_getpeername_hdlr);
	sys_register_handler(66, sys_readlink_hdlr);
	sys_register_handler(67, sys_fcntl_hdlr);
	sys_register_handler(68, sys_setsid_hdlr);
	sys_register_handler(69, sys_ptrace_hdlr);
	sys_register_handler(70, sys_reboot_hdlr);
	sys_register_handler(71, sys_getpriority_hdlr);
	sys_register_handler(72, sys_setpriority_hdlr);
	sys_register_handler(73, sys_ftruncate_hdlr);
	sys_register_handler(74, sys_truncate_hdlr);
	sys_register_handler(75, sys_pthread_create_hdlr);
	sys_register_handler(76, sys_pthread_exit_hdlr);
	sys_register_handler(77, sys_pthread_join_hdlr);
	sys_register_handler(78, sys_pthread_detach_hdlr);
	sys_register_handler(79, sys_pthread_cancel_hdlr);
	sys_register_handler(80, sys_pthread_self_hdlr);
	sys_register_handler(81, sys_pthread_setcancelstate_hdlr);
	sys_register_handler(82, sys_sched_yield_hdlr);
	sys_register_handler(83, sys_pthread_mutex_init_hdlr);
	sys_register_handler(84, sys_pthread_mutex_destroy_hdlr);
	sys_register_handler(85, sys_pthread_mutex_lock_hdlr);
	sys_register_handler(86, sys_pthread_mutex_trylock_hdlr);
	sys_register_handler(87, sys_pthread_mutex_unlock_hdlr);
	sys_register_handler(88, sys_pthread_kill_hdlr);
	sys_register_handler(89, sys_clock_settime_hdlr);
	sys_register_handler(90, sys_clock_gettime_hdlr);
	sys_register_handler(91, sys_pthread_key_create_hdlr);
	sys_register_handler(92, sys_pthread_setspecific_hdlr);
	sys_register_handler(93, sys_pthread_getspecific_hdlr);
	sys_register_handler(94, sys_alarm_hdlr);
	sys_register_handler(95, sys_ualarm_hdlr);
}
