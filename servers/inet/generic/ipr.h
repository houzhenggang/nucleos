/*
 *  Copyright (C) 2012  Ladislav Klenovic <klenovic@nucleonsoft.com>
 *
 *  This file is part of Nucleos kernel.
 *
 *  Nucleos kernel is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 2 of the License.
 */
/*
ipr.h

Copyright 1995 Philip Homburg
*/

#ifndef IPR_H
#define IPR_H

typedef struct oroute
{
	int ort_port;
	ipaddr_t ort_dest;
	ipaddr_t ort_subnetmask;
	int ort_dist;
	i32_t ort_pref;
	u32_t ort_mtu;
	ipaddr_t ort_gateway;
	time_t ort_exp_tim;
	time_t ort_timestamp;
	int ort_flags;

	struct oroute *ort_nextnw;
	struct oroute *ort_nextgw;
	struct oroute *ort_nextdist;
} oroute_t;

#define ORTD_UNREACHABLE	512

#define ORTF_EMPTY		0
#define ORTF_INUSE		1
#define ORTF_STATIC		2

typedef struct iroute
{
	ipaddr_t irt_dest;
	ipaddr_t irt_gateway;
	ipaddr_t irt_subnetmask;
	int irt_dist;
	u32_t irt_mtu;
	int irt_port;
	int irt_flags;
} iroute_t;

#define IRTD_UNREACHABLE	512

#define IRTF_EMPTY		0
#define IRTF_INUSE		1
#define IRTF_STATIC		2

#define IPR_UNRCH_TIMEOUT	(60L * HZ)
#define IPR_TTL_TIMEOUT		(60L * HZ)
#define IPR_REDIRECT_TIMEOUT	(20 * 60L * HZ)
#define IPR_GW_DOWN_TIMEOUT	(60L * HZ)
#define IPR_MTU_TIMEOUT		(10*60L * HZ)	/* RFC-1191 */

/* Prototypes */

iroute_t *iroute_frag(int port_nr, ipaddr_t dest);
int oroute_frag(int port_nr, ipaddr_t dest, int ttl, size_t msgsize, ipaddr_t *nexthop);
void ipr_init(void);
int ipr_get_iroute(int ent_no, nwio_route_t *route_ent);
int ipr_add_iroute(int port_nr, ipaddr_t dest, ipaddr_t subnetmask, ipaddr_t gateway,
		   int dist, int mtu, int static_route, iroute_t **route_p);
int ipr_del_iroute(int port_nr, ipaddr_t dest, ipaddr_t subnetmask, ipaddr_t gateway,
		   int static_route);
void ipr_chk_itab(int port_nr, ipaddr_t addr, ipaddr_t mask);
int ipr_get_oroute(int ent_no, nwio_route_t *route_ent);
int ipr_add_oroute(int port_nr, ipaddr_t dest, ipaddr_t subnetmask, ipaddr_t gateway,
		   time_t timeout, int dist, int mtu, int static_route, i32_t preference,
		   oroute_t **route_p);
int ipr_del_oroute(int port_nr, ipaddr_t dest, ipaddr_t subnetmask, ipaddr_t gateway,
		   int static_route);
void ipr_chk_otab(int port_nr, ipaddr_t addr, ipaddr_t mask);
void ipr_gateway_down(int port_nr, ipaddr_t gateway, time_t timeout);
void ipr_redirect(int port_nr, ipaddr_t dest, ipaddr_t subnetmask, ipaddr_t old_gateway,
		  ipaddr_t new_gateway, time_t timeout);
void ipr_destunrch(int port_nr, ipaddr_t dest, ipaddr_t subnetmask, time_t timeout);
void ipr_ttl_exc(int port_nr, ipaddr_t dest, ipaddr_t subnetmask, time_t timeout);
void ipr_mtu(int port_nr, ipaddr_t dest, u16 mtu, time_t timeout);

#endif /* IPR_H */
