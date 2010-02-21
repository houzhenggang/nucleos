/*
 *  Copyright (C) 2009  Ladislav Klenovic <klenovic@nucleonsoft.com>
 *
 *  This file is part of Nucleos kernel.
 *
 *  Nucleos kernel is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 2 of the License.
 */
#ifndef __PM_PARAM_H
#define __PM_PARAM_H

/* The following names are synonyms for the variables in the input message. */
#define exec_name	m1_p1
#define exec_len	m1_i1
#define func		m6_f1
#define grp_id		m1_i1
#define namelen		m1_i2
#define pid		m1_i1
#define seconds		m1_i1
#define which_timer	m1_i1
#define new_val		m1_p1
#define old_val		m1_p2
#define sig		m6_i1
#define stack_bytes	m1_i2
#define stack_ptr	m1_p2
#define status		m1_i1
#define usr_id		m1_i1
#define request		m2_i2
#define data		m2_l2
#define sig_nr		m1_i2
#define sig_nsa		m1_p1
#define sig_osa		m1_p2
#define sig_ret		m1_p3
#define stat_nr		m2_i1
#define sig_set		m2_l1
#define sig_how		m2_i1
#define sig_context	m2_p1
#define info_what	m1_i1
#define info_where	m1_p1
#define reboot_flag	m1_i1
#define reboot_code	m1_p1
#define reboot_strlen	m1_i2
#define svrctl_req	m2_i1
#define svrctl_argp	m2_p1
#define stime		m2_l1
#define memsize		m4_l1
#define membase		m4_l2
#define sysuname_req	m1_i1
#define sysuname_field	m1_i2
#define sysuname_len	m1_i3
#define sysuname_value	m1_p1

/* The following names are synonyms for the variables in a reply message. */
#define reply_res	m_type
#define reply_res2	m2_i1
#define reply_res3	m2_i2
#define reply_ptr	m2_p1
#define reply_mask	m2_l1
#define reply_trace	m2_l2
#define reply_time	m2_l1
#define reply_utime	m2_l2
#define reply_t1	m4_l1
#define reply_t2	m4_l2
#define reply_t3	m4_l3
#define reply_t4	m4_l4
#define reply_t5	m4_l5

#endif /* __PM_PARAM_H */
