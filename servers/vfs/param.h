/*
 *  Copyright (C) 2009  Ladislav Klenovic <klenovic@nucleonsoft.com>
 *
 *  This file is part of Nucleos kernel.
 *
 *  Nucleos kernel is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 2 of the License.
 */
/* The following names are synonyms for the variables in the input message. */
#define acc_time      m2_l1
#define addr	      m1_i3
#define buffer	      m1_p1
#define child_endpt      m1_i2
#define co_mode	      m1_i1
#define eff_grp_id    m1_i3
#define eff_user_id   m1_i3
#define erki          m1_p1
#define fd	      m1_i1
#define fd2	      m1_i2
#define ioflags       m1_i3
#define group	      m1_i3
#define real_grp_id   m1_i2
#define ls_fd	      m2_i1
#define mk_mode	      m1_i2
#define mk_z0	      m1_i3
#define mode	      m3_i2
#define c_mode        m1_i3
#define c_name        m1_p1
#define name	      m3_p1
#define name1	      m1_p1
#define name2	      m1_p2
#define	name_length   m3_i1
#define name1_length  m1_i1
#define name2_length  m1_i2
#define nbytes        m1_i2
#define owner	      m1_i2
#define parent_endpt      m1_i1
#define pathname      m3_ca1
#define pid	      m1_i3
#define ENDPT	      m1_i1
#define ctl_req       m4_l1
#define driver_nr     m4_l2
#define dev_nr	      m4_l3
#define dev_style     m4_l4
#define m_force	      m4_l5
#define rd_only	      m1_i3
#define real_user_id  m1_i2
#define request       m1_i2
#define sig	      m1_i2
#define endpt1	      m1_i1
#define tp	      m2_l1
#define utime_actime  m2_l1
#define utime_modtime m2_l2
#define utime_file    m2_p1
#define utime_length  m2_i1
#define utime_strlen  m2_i2
#define whence	      m2_i2
#define svrctl_req    m2_i1
#define svrctl_argp   m2_p1
#define pm_stime      m1_i1
#define info_what     m1_i1
#define info_where    m1_p1
#define md_label	m2_p1
#define md_label_len	m2_l1
#define md_major	m2_i1
#define md_style	m2_i2
#define md_force	m2_i3

/* The following names are synonyms for the variables in the output message. */
#define reply_type    m_type
#define reply_l1      m2_l1
#define reply_l2      m2_l2
#define reply_i1      m1_i1
#define reply_i2      m1_i2
#define reply_t1      m4_l1
#define reply_t2      m4_l2
#define reply_t3      m4_l3
#define reply_t4      m4_l4
#define reply_t5      m4_l5
