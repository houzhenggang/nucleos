#ifndef _NUCLEOS_NUMA_H
#define _NUCLEOS_NUMA_H

#ifdef CONFIG_NODES_SHIFT
#define NODES_SHIFT	CONFIG_NODES_SHIFT
#else
#define NODES_SHIFT	0
#endif

#define MAX_NUMNODES	(1 << NODES_SHIFT)

#define	NUMA_NO_NODE	(-1)

#endif /* _NUCLEOS_NUMA_H */
