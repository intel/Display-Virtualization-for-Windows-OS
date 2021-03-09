/*===========================================================================
; DVServershmem.h
;----------------------------------------------------------------------------
; * Copyright © 2020 Intel Corporation
; SPDX-License-Identifier: MIT
;
; File Description:
;   DVServer shared memory interface declaration
;--------------------------------------------------------------------------*/
#ifndef __DVSERVER_SHMEM_H__
#define __DVSERVER_SHMEM_H__

HANDLE idd_shmem_init(void);
void idd_shmem_destroy(HANDLE handle);
int idd_get_config_info(void);

#endif /* __DVSERVER_SHMEM_H__ */
