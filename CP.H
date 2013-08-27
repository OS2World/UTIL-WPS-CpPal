/**********************************************************************/
/*
    CP.H - header shared by CPX.C and CPD.C
    CpPal (C) Copyright 2001 R L Walsh  All Rights Reserved
*/
/**********************************************************************/

#ifdef _CP_EXE_
    #include <stdlib.h>
    #include <stdio.h>
    #include <string.h>
    #include "cpx.h"
#endif

#define INCL_WIN
#define INCL_DOS

#include <os2.h>

#define CPF_SET         0x8000UL
#define CPF_SETPMERR    0x4000UL
#define CPF_SETPROCERR  0x2000UL
#define CPF_MASK        0xE000UL

#define CPM_SET         (WM_USER+0x2e3b)

#define CPX_TIMER       (TID_USERMAX-0x2e3d)

#define CPBUFSIZE       CCHMAXPATH

#define MP              MPARAM

#define LOSHORT(l)      ((SHORT)((ULONG)l))
#define HISHORT(l)      ((SHORT)(((ULONG)(l) >> 16) & 0xffffUL))

#define ERRBREAK(msg)   {pErr = (#msg); break;}

/**********************************************************************/

// exported from CPD

void _System    cpdInitDll( HMODULE hDll, ULONG ulSetMsg);
int  _System    cpdInputHook( HAB hab, PQMSG pqmsg, ULONG fs);

/**********************************************************************/

