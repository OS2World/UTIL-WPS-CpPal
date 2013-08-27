/****************************************************************************/
/*
    CPD.C - CpPal Dll
    CpPal (C) Copyright 2001 R L Walsh  All Rights Reserved

    CpPal is free software released under the terms of the
    GNU General Public License.  Refer to the file "COPYING"
    for the full text of this license.
*/
/****************************************************************************/
/*

    For an overview of CpPal's operation, see cpx.c.

    Note:  when compiling and linking this module, use your compiler's
           "subsystem" libraries.  Because they do not generate a runtime
           environment, potential problems involving exception handling
           in the runtime code are avoided.

*/
/****************************************************************************/

#define _CP_DLL_

#include "cp.h"

/****************************************************************************/

void _System    cpdInitDll( HMODULE hDll, ULONG ulSetMsg);
int  _System    cpdInputHook( HAB hab, PQMSG pqmsg, ULONG fs);
void            cpdProcessMsg( HAB hab, PQMSG pqmsg);

/****************************************************************************/

BOOL        fFalse     = FALSE;     // my compiler doesn't like "while(FALSE)"
HMODULE     hmodCPD    = 0;         // this dll's module handle
ULONG       ulCPM_SET  = CPM_SET;   // CP's private msg;  this value will be
                                    // replaced by an atom-based value

/****************************************************************************/

// this function only operates in cp.exe's context

void _System    cpdInitDll( HMODULE hDll, ULONG ulSetMsg)

{
    hmodCPD = hDll;
    if (ulSetMsg)
        ulCPM_SET = ulSetMsg;

    return;
}

/****************************************************************************/

// this hook function operates in the target process's context

int  _System   cpdInputHook( HAB hab, PQMSG pqmsg, ULONG fs)

{
    if (pqmsg->hwnd != 0 || pqmsg->msg != ulCPM_SET)
        return (FALSE);

// don't act until the msg is removed from the queue;  this
// should provide consistent results if WinPeekMsg() is called
    if (fs == PM_REMOVE)
    {
        cpdProcessMsg( hab, pqmsg);

        if (WinReleaseHook( hab, HMQ_CURRENT, HK_INPUT,
                            (PFN)cpdInputHook, hmodCPD) == FALSE)
            WinAlarm( HWND_DESKTOP, WA_ERROR);
    }

    return (TRUE);
}

/**********************************************************************/

// process our private msg;
// mp1 == hwnd to which we post a reply;
// mp2 == MPFROM2SHORT( PM codepage, Process codepage);
// FYI... hab == MAKEULONG( tid, pid);  since there's a max of
// 4096 threads, we can use the top 3 bits of the tid as flags

void        cpdProcessMsg( HAB hab, PQMSG pqmsg)

{
    BOOL    fBeep = FALSE;
    ULONG   ulPMCP;
    ULONG   aulCP[3] = {0,0,0};
    ULONG   cntCP;

do
{
// if the reply hwnd is no good, do nothing
    if (pqmsg->mp1 == 0 || WinIsWindow( hab, (HWND)pqmsg->mp1) == FALSE)
    {
        fBeep = TRUE;
        break;
    }

// if mp2 != 0, then we're to set one or both codepages;
// set flags accordingly
    if (pqmsg->mp2)
    {
        hab |= CPF_SET;
        if (SHORT1FROMMP( pqmsg->mp2))
        {
            if (WinSetCp( HMQ_CURRENT, SHORT1FROMMP( pqmsg->mp2)) == FALSE)
                hab |= CPF_SETPMERR;
        }

        if (SHORT2FROMMP( pqmsg->mp2))
        {
            if (DosSetProcessCp( SHORT2FROMMP( pqmsg->mp2)))
                hab |= CPF_SETPROCERR;
        }

        if (hab & (CPF_SETPMERR | CPF_SETPROCERR))
            fBeep = TRUE;
    }

// get current PM & process cp's
    ulPMCP = WinQueryCp( HMQ_CURRENT);
    DosQueryCp( sizeof(aulCP), aulCP, &cntCP);

// pass the tid/flags, pid, and codepages to the requesting window
    if (WinPostMsg( (HWND)pqmsg->mp1, pqmsg->msg, (MP)hab,
                    MPFROM2SHORT( ulPMCP, aulCP[0])) == FALSE)
        fBeep = TRUE;

} while (fFalse);

    if (fBeep)
        WinAlarm( HWND_DESKTOP, WA_ERROR);

    return;
}

/**********************************************************************/

