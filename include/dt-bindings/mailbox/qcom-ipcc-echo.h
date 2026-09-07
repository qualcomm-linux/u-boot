/* SPDX-License-Identifier: GPL-2.0-only OR BSD-2-Clause */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef __DT_BINDINGS_MAILBOX_IPCC_ECHO_H
#define __DT_BINDINGS_MAILBOX_IPCC_ECHO_H

/* Signal IDs for MPROC protocol */
#define IPCC_MPROC_SIGNAL_GLINK_QMP	0
#define IPCC_MPROC_SIGNAL_TZ		1
#define IPCC_MPROC_SIGNAL_SMP2P		2
#define IPCC_MPROC_SIGNAL_PING		3

/* Physical client IDs */
#define IPCC_MPROC_AOP		0
#define IPCC_MPROC_TZ		1
#define IPCC_MPROC_MPSS		2
#define IPCC_MPROC_CDSP		3
#define IPCC_MPROC_APSS		4

#define IPCC_COMPUTE_L0_MPSS		0
#define IPCC_COMPUTE_L0_CDSP		1
#define IPCC_COMPUTE_L0_APSS		2

#define IPCC_COMPUTE_L1_MPSS		0
#define IPCC_COMPUTE_L1_CDSP		1
#define IPCC_COMPUTE_L1_APSS		2

#define IPCC_PERIPH_MPSS		0
#define IPCC_PERIPH_APSS		1
#define IPCC_PERIPH_PCIE0		2
#define IPCC_PERIPH_PCIE1		3
#define IPCC_PERIPH_PCIE2		4
#define IPCC_PERIPH_PCIE3		5
#endif
