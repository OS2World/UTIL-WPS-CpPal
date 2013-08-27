/****************************************************************************/
/*
    CPX.C - CpPal Exe v0.30
    CpPal (C) Copyright 2001 R L Walsh  All Rights Reserved

    CpPal is free software released under the terms of the
    GNU General Public License.  Refer to the file "COPYING"
    for the full text of this license.

    Please send your comments and suggestions to:
    Rich Walsh <dragtext@e-vertise.com>
*/
/****************************************************************************/
/*

Overview
========

CpPal is an on-the-fly codepage switching utility for PM-based programs.
The user can change or query both a program's Process codepage and the
codepage used by PM for its user interface thread.  By design, the user
may query any process's CPs, and may change them for any process except
the shell (pmshell #1), the WPS, and CpPal itself.

CpPal consists of two functional units:  an exe which provides the user
interface and a dll containing a message queue hook which does the work.

The exe uses a dialog box to display the two process CPs and a list
of the 100+ CPs available to PM.  It uses the contents of CpPal.Lst
to provide descriptive text about each CP.

To set or get CPs, the user drags from one of two icons in the dialog.
The icon's window captures the mouse and changes the pointer to reflect
the operation.  During the drag, the pointer is updated to identify
whether a drop is permitted on the window under the pointer.

When the source window receives a WM_ENDDRAG msg, it releases the
mouse capture and identifies the specific window where the drop occurred.
If a drop was permitted, it obtains the message queue handle for the
thread which created the window.  It then sets an input hook on that
queue and posts a unique, atom-based msg to the queue.  The msg contains
the source window's handle in mp1 and the PM and Process CPs to set in
mp2.  If either CP is to remain unchanged, its value is set to zero;
for queries, both values are zero.

When the hook procedure (contained in cppal.dll) receives the msg, it
sets the CPs indicated in mp2, queries the current process and message
queue CPs, then posts a reply msg to the source window using the same
message ID.  mp2 contains the CPs, while mp1 contains the target's HAB,
which is actually the queue's TID and PID.  Since there can be only 4096
threads, the 3 high-order bits of the TID are used as result flags.
Finally, the hook code unhooks itself and returns FALSE.

When the reply msg is received by the source, it displays the results
in the dialog's status text field.  Because a window's existing text
is not immediately updated when the PM CP changes, the source window
posts a WM_PRESPARAM changed msg to the target window immediately
after posting its queue msg to force an update.  This works for MLEs
but may not work for other types of windows.  A future version of
CpPal (if any) will deal with this in a more comprehensive way.

Note
----
As you will see, most functions are bracketed by a dummy
"do{ }while(FALSE)" statement and are structured like this:

do {
    test conditions;
    allocate resources;
    perform processing;
} while (FALSE);
    display error message;
    deallocate resources;
    return;

IMHO, this helps ensure that code is both readable and reliable.
If an error occurs or processing completes early, the code uses
a break statement to move directly to the finalization stage.
This makes it easy to display error messages when needed and
guarantees that resource deallocation will not be bypassed.  The
only downside is that some local variables must be initialized
to a known value (usually zero) so the deallocation code can
determine whether a given resource was actually allocated.

*/
/****************************************************************************/

#define _CP_EXE_

#include "cp.h"

/****************************************************************************/

// main
int         main( void);
void        cpxChangeAppType( void);
MRESULT _System MainWndProc( HWND hwnd, ULONG msg, MPARAM mp1, MPARAM mp2);

// init
BOOL        cpxInitDlg( HWND hwnd);
BOOL        cpxInitPIDs( HWND hwnd);
BOOL        cpxInitProcessCP( HWND hwnd);
BOOL        cpxInitCPList( HWND hwnd);
void        cpxFormatCPLine( char * pszIn, char * pszOut);
int         cpxQSortCompare( const void * p1, const void * p2);
FILE *      cpxOpenListFile( char * pszText);
ULONG       cpxGetNextCPLine ( FILE* file, ULONG ulCP, char * pszText);

// drag/drop
MRESULT _System DragSubProc( HWND hwnd, ULONG msg, MPARAM mp1, MPARAM mp2);
BOOL        cpxBeginDrag( HWND hSrc);
BOOL        cpxEndDrag( HWND hSrc, ULONG ulPos);
ULONG       cpxGetDragCPs( HWND hSrc);
HWND        cpxGetDroppableWindow( HWND hSrc, ULONG ulPos, BOOL fExact);
void        cpxShowReply( HWND hSrc, ULONG mp1, ULONG mp2);

// errors
BOOL        cpxErrBox( PSZ pTtl, PSZ pErr);
BOOL        cpxRcErrBox( PSZ pTtl, PSZ pErr, ULONG rc);

/****************************************************************************/


BOOL        fFalse      = FALSE; // my compiler doesn't like "while(FALSE)"
BOOL        fInCapture  = FALSE; // TRUE when dragging
HAB         habMain     = 0;    // needed to set hook and timer
HWND        hMain       = 0;    // easier if global
HWND        hFocus      = 0;    // used to restore correct focus after drag
HPOINTER    hptrSet     = 0;    // the "OK to drop here" mouse pointer
HPOINTER    hptrGet     = 0;    // the "OK to query" mouse pointer
HPOINTER    hptrIll     = 0;    // the "not OK to drop here" mouse pointer
HPOINTER    hptrCur     = 0;    // the current OK mouse pointer
HMODULE     hmodCPD     = 0;    // dll mod handle, used to set a hook
PID         pidCP       = 0;    // pids we're not allowed to drop on
PID         pidPM       = 0;
PID         pidWPS      = 0;
ULONG       ulCP2Drag   = 0;    // the codepages we're dragging
ULONG       ulPriCP     = 0;    // first prepared (or default) codepage
ULONG       ulSecCP     = 0;    // second prepared codepage
ULONG       ulCPM_SET   = CPM_SET; // our private msg; the default
                                   // value will be replaced by an atom

char        szCPD[]     = "CPPAL";          // the dll's u/q name
char        szCPM_SET[] = "CPM_SET";        // string used to create atom
char        szCPListFile[]  = "CPPAL.LST";  // our list of documented cp's

/**********************************************************************/

// just your basic Init / Msg Loop / Shutdown code

int         main( void)

{
    BOOL    fRtn = FALSE;
    HMQ     hmq = 0;
    HWND    hwnd = 0;
    QMSG    qmsg;

do
{
// not really needed now, but handy if we want to add
// a console window to display printf's
    cpxChangeAppType();

    habMain = WinInitialize( 0);
    if (!habMain)
        break;

    hmq = WinCreateMsgQueue( habMain, 0);
    if (!hmq)
        break;

    hwnd = WinLoadDlg(
                HWND_DESKTOP,               //  parent-window
                NULLHANDLE,                 //  owner-window
                MainWndProc,                //  dialog proc
                NULLHANDLE,                 //  EXE module handle
                IDD_MAIN,                   //  dialog id
                NULL);                      //  pointer to create params

    if (!hwnd)
        break;

    if (!WinRestoreWindowPos( "CPPAL", "WNDPOS", hwnd))
        WinShowWindow( hwnd, TRUE);

    while (WinGetMsg( habMain, &qmsg, NULLHANDLE, 0, 0))
        WinDispatchMsg( habMain, &qmsg);

    fRtn = TRUE;

} while (fFalse);

    if (fRtn == FALSE)
        DosBeep( 440, 150);

    if (hwnd)
    {
        WinStoreWindowPos( "CPPAL", "WNDPOS", hwnd);
        WinDestroyWindow( hwnd);
    }
    if (hmq)
        WinDestroyMsgQueue( hmq);
    if (habMain)
        WinTerminate( habMain);

    return (0);
}

/**********************************************************************/

//  if we link this exe as a VIO app, we'll get a console window where
//  we can display printf's;  however, before calling any PM function,
//  we have to change it back to a PM-type app

void    cpxChangeAppType( void)

{
    PTIB    ptib;
    PPIB    ppib;

    DosGetInfoBlocks( &ptib, &ppib);
    ppib->pib_ultype = 3;
    return;
}

/**********************************************************************/

//  the main dialog window;
//  its main purpose currently is just to init the dlg

MRESULT _System MainWndProc( HWND hwnd, ULONG msg, MPARAM mp1, MPARAM mp2)
 
{
    switch (msg)
    {
// init the app;  if anything major fails, discontinue the
// dialog creation process
        case WM_INITDLG:
            if (cpxInitDlg( hwnd) == FALSE)
                return ((MRESULT)-1);
            break;

// prevent command & control msgs from getting to the
// default dlg proc to keep it from dismissing the window
        case WM_COMMAND:
        case WM_CONTROL:
            break;

        case WM_CLOSE:
            WinPostMsg( hwnd, WM_QUIT, NULL, NULL);
            break;

// free the persistent resources we allocated
        case WM_DESTROY:
            if (ulCPM_SET != CPM_SET)
                WinDeleteAtom( WinQuerySystemAtomTable(), ulCPM_SET);
            if (hptrSet)
                WinDestroyPointer( hptrSet);
            if (hptrGet)
                WinDestroyPointer( hptrGet);
            if (hptrIll)
                WinDestroyPointer( hptrIll);
            return (WinDefDlgProc( hwnd, msg, mp1, mp2));

        default:
            return (WinDefDlgProc( hwnd, msg, mp1, mp2));
    }

    return (0);
}

/****************************************************************************/

// everything needed to get the app going

BOOL        cpxInitDlg( HWND hwnd)

{
    BOOL        fRtn = FALSE;
    HWND        hSrc;
    HPOINTER    hIco;
    HATOMTBL    haTbl;
    ATOM        atom;
    PFNWP       pfnOrg;
    char *      pErr = NULL;

do
{
    hMain = hwnd;

// subclass the drag icons

    if ((hSrc = WinWindowFromID( hwnd, IDC_SET)) == 0 ||
        (pfnOrg = (PFNWP)WinQueryWindowPtr( hSrc, QWP_PFNWP)) == NULL ||
        WinSetWindowULong( hSrc, QWL_USER, (ULONG)pfnOrg) == FALSE ||
        WinSetWindowPtr( hSrc, QWP_PFNWP, (PVOID)&DragSubProc) == FALSE)
        ERRBREAK( Error subclassing IDC_SET)

    if ((hSrc = WinWindowFromID( hwnd, IDC_GET)) == 0 ||
        (pfnOrg = (PFNWP)WinQueryWindowPtr( hSrc, QWP_PFNWP)) == NULL ||
        WinSetWindowULong( hSrc, QWL_USER, (ULONG)pfnOrg) == FALSE ||
        WinSetWindowPtr( hSrc, QWP_PFNWP, (PVOID)&DragSubProc) == FALSE)
        ERRBREAK( Error subclassing IDC_GET)

// get the lists of read-only PIDs and available Process & PM CP's
    if (cpxInitPIDs( hwnd) == FALSE ||
        cpxInitProcessCP( hwnd) == FALSE ||
        cpxInitCPList( hwnd) == FALSE)
        break;

// set the dialog's sysmenu icon
    if ((hIco = WinLoadPointer( HWND_DESKTOP, 0, IDI_ICON)) != 0)
        WinSendMsg( hwnd, WM_SETICON, (MP)hIco, 0);

// load our drag mouse pointers
    if ((hptrSet = WinLoadPointer( HWND_DESKTOP, 0, IDP_SET)) == 0 ||
        (hptrGet = WinLoadPointer( HWND_DESKTOP, 0, IDP_GET)) == 0 ||
        (hptrIll = WinLoadPointer( HWND_DESKTOP, 0, IDP_ILL)) == 0)
        ERRBREAK( WinLoadPointer)

// the dll's hmod is needed to set & release hooks
    if (DosQueryModuleHandle( szCPD, &hmodCPD))
        ERRBREAK( DosQueryModuleHandle)

// create a unique ID for our private msg
    haTbl = WinQuerySystemAtomTable();
    atom = WinAddAtom( haTbl, szCPM_SET);
    if (atom)
        ulCPM_SET = atom;

// have the dll store this info
    cpdInitDll( hmodCPD, ulCPM_SET);

    fRtn = TRUE;

} while (fFalse);

    if (pErr)
        cpxErrBox( __FUNCTION__, pErr);

    return (fRtn);
}

/****************************************************************************/

//  identify the PIDs whose CPs can't be changed;  this restriction is
//  solely a matter of "policy" -  changing these is likely to cause
//  system-wide problems (e.g. some files may become inaccessible);
//  a future version may make this optional

BOOL        cpxInitPIDs( HWND hwnd)

{
    BOOL        fRtn = FALSE;
    LONG        lMatch = 1;
    HENUM       hEnum;
    HWND        hTemp;
    TID         tid;
    char *      pErr = NULL;
    char        szText[32];

do
{
// our own PID
    if (WinQueryWindowProcess( hwnd, &pidCP, &tid) == FALSE)
        ERRBREAK( WinQueryWindowProcess)

// the shell process's PID
    hTemp = WinQueryDesktopWindow( HWND_DESKTOP, NULLHANDLE);
    if (WinQueryWindowProcess( hTemp, &pidPM, &tid) == FALSE)
        ERRBREAK( WinQueryWindowProcess)

// a cheesy way to get the WPS's PID;  it works on the assumption
// that the WPS desktop folder window is always bottommost
    hTemp = WinQueryWindow( hTemp, QW_BOTTOM);
    if (WinQueryClassName( hTemp, sizeof( szText), szText) == 0)
        ERRBREAK( WinQueryClassName)

// if that didn't work, do it the "right" way
    if ((lMatch = strcmp( szText, "wpFolder window")) != 0)
    {
        if ((hEnum = WinBeginEnumWindows( HWND_DESKTOP)) == 0)
            ERRBREAK( WinBeginEnumWindows)

        while ((hTemp = WinGetNextWindow( hEnum)) != 0)
        {
            if (WinQueryClassName( hTemp, sizeof( szText), szText) == 0)
                ERRBREAK( WinQueryClassName)

            if ((lMatch = strcmp( szText, "wpFolder window")) == 0)
                break;
        }

        WinEndEnumWindows( hEnum);
        if (pErr)
            break;
    }

// if we found a "wpFolder window", get the PID that created it
    if (lMatch == 0 &&
        WinQueryWindowProcess( hTemp, &pidWPS, &tid) == FALSE)
           ERRBREAK( WinQueryWindowProcess)

    fRtn = TRUE;

} while (fFalse);

    if (pErr)
        cpxErrBox( __FUNCTION__, pErr);

    return (fRtn);
}

/****************************************************************************/

// get the current, primary, and secondary codepages;
// init the buttons accordingly, and disable process
// CP switching if there isn't a secondary CP

BOOL        cpxInitProcessCP( HWND hwnd)

{
    ULONG       cntCP;
    ULONG       aulCP[3] = {0,0,0};
    char *      pErr = NULL;

// get all 3 codepages
    if (DosQueryCp( sizeof(aulCP), aulCP, &cntCP))
        pErr = "DosQueryCP";

    WinCheckButton( hwnd, IDC_CP1, TRUE);

// if there's no CODEPAGE entry in config.sys, I _think_ the
// primary CP will be zero;  if so, use the entry for the current CP
    if (aulCP[1])
        ulPriCP = aulCP[1];
    else
    {
        ulPriCP = aulCP[0];
        WinSetDlgItemText( hwnd, IDC_CP1TXT, "default");
    }
    if (ulPriCP)
        WinSetDlgItemShort( hwnd, IDC_CP1, (USHORT)ulPriCP, FALSE);

    ulSecCP = aulCP[2];

// if there's a secondary CP, allow user to change process CP;
// if not, it's pointless to do so (and can produce errors),
// so disable this feature
    if (ulSecCP)
    {
        WinSetDlgItemShort( hwnd, IDC_CP2, (USHORT)ulSecCP, FALSE);
        WinCheckButton( hwnd, IDC_CPPROC, TRUE);
    }
    else
    {
        WinEnableControl( hwnd, IDC_CPPROC, FALSE);
        WinEnableControl( hwnd, IDC_CP1, FALSE);
        WinEnableControl( hwnd, IDC_CP2, FALSE);
        WinEnableControl( hwnd, IDC_CP1TXT, FALSE);
        WinEnableControl( hwnd, IDC_CP2TXT, FALSE);
    }

    if (pErr)
        cpxErrBox( __FUNCTION__, pErr);

    return (TRUE);
}

/****************************************************************************/

//  get PM's list of available CPs, try to match them with
//  entries in CpPal.Lst, then insert them in the combobox

BOOL        cpxInitCPList( HWND hwnd)

{
    BOOL        fRtn = FALSE;
    ULONG       ulFileCP = 0;
    ULONG       ul;
    ULONG       ctr;
    HWND        hCB;
    SHORT       ndx;
    SHORT       ndxSav;
    FILE *      file = NULL;
    PULONG      pul = NULL;
    char *      pErr = NULL;
    char *      pszIn;
    char *      pszOut;

do
{
// find the combobox
    if ((hCB = WinWindowFromID( hwnd, IDC_LIST)) == 0)
        ERRBREAK( WinWindowFromID)

// alloc enough space for 1024 CPs plus 2 text buffers
    if ((pul = malloc( 4096 + 2*CPBUFSIZE)) == NULL)
        ERRBREAK( malloc)

// set the ptrs to the text buffers
    pszIn = (char *)&pul[(4096/sizeof(ULONG))];
    pszOut = &pszIn[CPBUFSIZE];

// get the CPs, then sort them
    if ((ul = WinQueryCpList( WinQueryAnchorBlock( hwnd),
                              (4096/sizeof(ULONG)), pul)) == 0)
        ERRBREAK( WinQueryCpList)

    qsort( (PVOID)pul, (size_t)ul, (size_t)sizeof( ULONG), &cpxQSortCompare);

// open our list of CP descriptions and get the first entry
    file = cpxOpenListFile( pszIn);
    if (file == NULL)
        pErr = "unable to open CP description file";
    else
    {
        ulFileCP = cpxGetNextCPLine ( file, 1, pszIn);
        if (ulFileCP == 0)
            pErr = "unable to read CP description file";
    }

// for each CP, try to match it to a line in the file; if found,
// format the file text;  if not, format the nbr;  then insert text;
// save each CP in the LB's item handle;  also, save the ndx of
// of the item which matches the primary CP so we can select it
    for (ctr=0, ndxSav=0; ctr < ul; ctr++)
    {
        if (ulFileCP > 0 && ulFileCP < pul[ctr])
            ulFileCP = cpxGetNextCPLine ( file, pul[ctr], pszIn);

        if (ulFileCP == pul[ctr])
            cpxFormatCPLine( pszIn, pszOut);
        else
            sprintf( pszOut, "%-5d %-3s ", pul[ctr], "?");
        ndx = (SHORT)WinSendMsg( hCB, LM_INSERTITEM, (MP)LIT_END, (MP)pszOut);

        WinSendMsg( hCB, LM_SETITEMHANDLE, (MP)ndx, (MP)pul[ctr]);

        if (pul[ctr] == ulPriCP)
            ndxSav = ndx;
    }

    WinSendMsg( hCB, LM_SELECTITEM, (MP)ndxSav, (MP)TRUE);
    WinCheckButton( hwnd, IDC_CPPM, TRUE);
    fRtn = TRUE;

} while (fFalse);

    if (pErr)
        cpxErrBox( __FUNCTION__, pErr);

    if (file)
        fclose( file);

    if (pul)
        free( pul);

    return (fRtn);
}

/**********************************************************************/

//  provide consistent formatting for the columns,
//  regardless of how the info was entered in cppal.lst

void        cpxFormatCPLine( char * pszIn, char * pszOut)

{
    char *      pNbr;
    char *      pFlags = "?";
    char *      pDesc = "";
    char *      ptr;

    pNbr = pszIn + strspn( pszIn, " \t");
    ptr = strpbrk( pNbr, " \t");
    if (ptr)
    {
        *ptr++ = '\0';
        pFlags = ptr + strspn( ptr, " \t");
        ptr = strpbrk( pFlags, " \t");
        if (ptr)
        {
            *ptr++ = '\0';
            pDesc = ptr + strspn( ptr, " \t");
        }
    }

    sprintf( pszOut, "%-5s %-3s %s", pNbr, pFlags, pDesc);

    return;
}

/**********************************************************************/

//  sort integers in ascending order

int         cpxQSortCompare( const void * p1, const void * p2)

{
    return (*(PLONG)p1 - *(PLONG)p2);
}

/**********************************************************************/

//  open CpPal.Lst;  it must be in the current directory
//  or in the same directory as CpPal.Exe

FILE *      cpxOpenListFile( char * pszText)

{
    PPIB    ppib;
    PTIB    ptib;
    FILE *  file = NULL;
    char *  ptr;
    char *  pErr = NULL;

do
{
// try to open the file in the current directory
    file = fopen( szCPListFile, "r");
    if (file)
        break;

// identify the path to CpPal.Exe
    DosGetInfoBlocks( &ptib, &ppib);

    if (DosQueryModuleName( ppib->pib_hmte, CPBUFSIZE, pszText))
        ERRBREAK( DosQueryModuleName)

    ptr = strrchr( pszText, '\\');
    if (ptr)
        ptr++;
    else
        ptr = pszText;

// append CpPal.Lst to that path
    strcpy( ptr, szCPListFile);

// try to open the file in the exe directory
    file = fopen( pszText, "r");

} while (fFalse);

    if (pErr)
        cpxErrBox( __FUNCTION__, pErr);

    return (file);
}

/**********************************************************************/

//  read the file seeking the first line whose CP is >= the
//  requested CP;  return the CP found (zero if not found);
//  lines starting with "#" are comments and are skipped

ULONG       cpxGetNextCPLine ( FILE* file, ULONG ulCP, char * pszText)

{
    ULONG   ulRtn = 0;
    char *  ptr;

    while (ferror( file) == 0)
    {
        ptr = fgets( pszText, CPBUFSIZE, file);

        if (ptr == NULL || *ptr == '#')
            ulRtn = 0;
        else
            ulRtn = (ULONG)atol( ptr);

        if (ulRtn >= ulCP)
            break;
    }

// if we're returning a line, remove any CR or LF
    if (ulRtn &&
        (ptr = strpbrk( pszText, "\r\n")) != NULL)
        *ptr = '\0';

    return (ulRtn);
}

/**********************************************************************/

//  this subclass proc provides all the drag and drop functionality

//  Note:  this is NON-STANDARD d&d similar to that used by the
//  various WPS palettes.  It is controlled entirely by the source
//  and is invisible to the target window.

MRESULT _System DragSubProc( HWND hwnd, ULONG msg, MPARAM mp1, MPARAM mp2)
 
{
    PFNWP       pfnOrg;

    switch (msg)
    {
        case WM_BEGINDRAG:

// if this proc were attached to a DragText-enabled window (as it was
// in an earlier version), limiting ourselves to "no keys pressed" would
// let the separate d&d features coexist
            if (((ULONG)WinGetKeyState( HWND_DESKTOP, VK_SHIFT) & 0x8000UL) ||
                ((ULONG)WinGetKeyState( HWND_DESKTOP, VK_CTRL)  & 0x8000UL) ||
                ((ULONG)WinGetKeyState( HWND_DESKTOP, VK_ALT)   & 0x8000UL))
                break;

// regardless of whether we actually begin a drag or not,
// we will *not* pass this msg on to the original window proc
            cpxBeginDrag( hwnd);
            return ((MRESULT)TRUE);

        case WM_ENDDRAG:

// if we never began a drag, ignore this msg
            if (fInCapture == FALSE)
                break;

// as with BeginDrag, this msg will not be passed along
            cpxEndDrag( hwnd, (ULONG)mp1);
            return ((MRESULT)TRUE);

// after posting a msg to the hook, we set a 3-second timer;
// if no reply is received before it expires, an error msg is displayed

        case WM_TIMER:
            if ((int)mp1 != CPX_TIMER)
                break;

            WinStopTimer( habMain, hwnd, CPX_TIMER);
            WinSetDlgItemText( hMain, IDC_INFO,
                               "Error:  no reply from target thread");
            return (0);

// determine whether we can drop on the window under the mouse ptr,
// then set the ptr accordingly;  since we've done everything needed
// to handle this msg, we won't pass it on

        case WM_MOUSEMOVE:
            if (fInCapture == FALSE)
                break;

            WinSetPointer( HWND_DESKTOP,
                    (cpxGetDroppableWindow( hwnd, (ULONG)mp1, FALSE) ?
                    hptrCur : hptrIll));
            return ((MRESULT)TRUE);

// cancel the drag if Escape is pressed;  note that we had
// to get the focus before dragging to ensure this works

        case WM_CHAR:
            if (fInCapture &&
               (((CHARMSG(&msg)->fs & (USHORT)KC_VIRTUALKEY) &&
                   CHARMSG(&msg)->vkey == VK_ESC) ||
                ((CHARMSG(&msg)->fs & (USHORT)KC_CHAR) &&
                   CHARMSG(&msg)->chr == 0x1b)))
            {
                if (WinSetCapture( HWND_DESKTOP, NULLHANDLE) ||
                    WinQueryCapture( HWND_DESKTOP) != hwnd)
                    fInCapture = FALSE;
                return ((MRESULT)TRUE);
            }
            break;

        default:

// our private msg contains the hook's reply to our request
            if (msg == ulCPM_SET)
            {
                cpxShowReply( hwnd, (ULONG)mp1, (ULONG)mp2);
                return (0);
            }
            break;
    }

// call the original window proc for all msgs we didn't handle

    pfnOrg = (PFNWP)WinQueryWindowULong( hwnd, QWL_USER);
    return (pfnOrg( hwnd, msg, mp1, mp2));
}

/**********************************************************************/

//  if conditions permit, save the CPs to set (if any),
//  get appropriate mouse pointers, then capture the mouse

BOOL        cpxBeginDrag( HWND hSrc)

{
    BOOL    fRtn = FALSE;
    HWND    hTemp;
    char *  pErr = NULL;

do
{
// determine whether this is a get or set operation,
// and if a set op, the CP(s) we'll be setting
    ulCP2Drag = 0;
    if ((ulCP2Drag = cpxGetDragCPs( hSrc)) == (ULONG)-1)
        break;

// if the mouse is already captured, we can't drag
// (this is highly unlikely but worth testing for anyway)
    hTemp = WinQueryCapture( HWND_DESKTOP);
    if (hTemp)
    {
        pErr = "mouse already captured";
        if (hTemp == hSrc &&
            WinSetCapture( HWND_DESKTOP, NULLHANDLE) == TRUE)
            fInCapture = FALSE;
        break;
    }

// we need to get the focus so the user can cancel the drag
// by pressing Escape
    hFocus = WinQueryFocus( HWND_DESKTOP);
    if (hFocus != hSrc)
        WinFocusChange( HWND_DESKTOP, hSrc, 0);

// set the mouse pointer appropriate to a get or set operation
    if (ulCP2Drag)
    {
        hptrCur = hptrSet;
        WinSetPointer( HWND_DESKTOP, hptrIll);
    }
    else
    {
        hptrCur = hptrGet;
        WinSetPointer( HWND_DESKTOP, hptrCur);
    }

// capture the mouse to begin the drag
    fInCapture = WinSetCapture( HWND_DESKTOP, hSrc);
    if (fInCapture == FALSE)
        ERRBREAK( WinSetCapture)

    fRtn = TRUE;

} while (fFalse);

    if (pErr)
        cpxErrBox( __FUNCTION__, pErr);

    return (fRtn);
}

/**********************************************************************/

//  identify the window the user dropped on, get its msg queue,
//  hook that queue, then post our private msg to it with our request

BOOL        cpxEndDrag( HWND hSrc, ULONG ulPos)

{
    BOOL        fRtn = FALSE;
    HWND        hDrop;
    HMQ         hmqDrop;
    char *      pErr = NULL;

do
{
// release the mouse capture
    if (WinSetCapture( HWND_DESKTOP, NULLHANDLE) == FALSE)
    {
        if (WinQueryCapture( HWND_DESKTOP) != hSrc)
            fInCapture = FALSE;
        break;
    }

    fInCapture = FALSE;

// restore focus to whatever window had it before the drag
    if (hFocus != hSrc)
        WinFocusChange( HWND_DESKTOP, hFocus, 0);

// identify window that was dropped on & get its msg queue
    hDrop = cpxGetDroppableWindow( hSrc, ulPos, TRUE);
    if (hDrop == 0)
        break;

    hmqDrop = WinQueryWindowULong( hDrop, QWL_HMQ);
    if (hmqDrop == 0)
        ERRBREAK( WinQueryWindowULong for QWL_HMQ)

// hook that specific queue using the hook proc in our dll
    if (WinSetHook( habMain, hmqDrop, HK_INPUT,
                    (PFN)cpdInputHook, hmodCPD) == FALSE)
        ERRBREAK( WinSetHook)

// post our private msg to that queue;  if this fails,
// unhook the queue immediately
    if (WinPostQueueMsg( hmqDrop, ulCPM_SET,               
                         (MP)hSrc, (MP)ulCP2Drag) == FALSE)
    {
        if (WinReleaseHook( habMain, hmqDrop, HK_INPUT,
                            (PFN)cpdInputHook, hmodCPD) == FALSE)
            ERRBREAK( WinPostQueueMsg and WinReleaseHook)
        else
            ERRBREAK( WinPostQueueMsg)
    }

// changing the CP won't cause existing text to be redisplayed
// automatically;  posting a presparam msg to the target will
// force this to happen (at least for MLEs)
    WinPostMsg( hDrop, WM_PRESPARAMCHANGED, (MP)PP_FONTNAMESIZE, 0);

// start a timer, then set the info line;  if hook doesn't reply
// within 3 seconds, an error msg will be displayed
    WinSetDlgItemText( hMain, IDC_INFO, "Waiting for reply...");
    WinStartTimer( habMain, hSrc, CPX_TIMER, 3000);

    fRtn = TRUE;

} while (fFalse);

    if (pErr)
        cpxErrBox( __FUNCTION__, pErr);

    return (fRtn);
}

/**********************************************************************/

//  identify the CPs to set (if any) based on the dialog settings
//  and on the source of the drag (i.e. the get or set icons)

ULONG       cpxGetDragCPs( HWND hSrc)

{
    ULONG       ulRtn = (ULONG)-1;
    ULONG       ulPMCP = 0;
    ULONG       ulProcCP = 0;
    SHORT       ndx;
    char *      pErr = NULL;

do
{

// if dragging from the get icon, set the CPs to 0,0
// which equates to "get"
    if (WinQueryWindowUShort( hSrc, QWS_ID) == IDC_GET)
    {
        ulRtn = 0;
        break;
    }

// if the "change PM" box is checked, get the new PM CP
    if (WinQueryButtonCheckstate( hMain, IDC_CPPM))
    {
        ndx = (SHORT)WinSendDlgItemMsg( hMain, IDC_LIST, LM_QUERYSELECTION, (MP)LIT_FIRST, 0);
        if (ndx == LIT_NONE)
            ERRBREAK( LM_QUERYSELECTION)

        ulPMCP = (ULONG)WinSendDlgItemMsg( hMain, IDC_LIST, LM_QUERYITEMHANDLE, (MP)ndx, 0);
        if (ulPMCP == 0)
            ERRBREAK( LM_QUERYITEMHANDLE)
    }

// if the "change Process" box is checked, get the new Process CP
    if (WinQueryButtonCheckstate( hMain, IDC_CPPROC))
        if (WinQueryButtonCheckstate( hMain, IDC_CP2))
            ulProcCP = ulSecCP;
        else
            ulProcCP = ulPriCP;

// this assumes no CP will be numbered higher than 64k
    ulRtn = MAKEULONG( ulPMCP, ulProcCP);

} while (fFalse);

    if (pErr)
        cpxErrBox( __FUNCTION__, pErr);

    return (ulRtn);
}

/**********************************************************************/

//  identify the window under the pointer;  if this is a get operation,
//  any window is OK;  if it is a set op, prohibit drops on windows
//  belonging to PM, the WPS, and CpPal

HWND        cpxGetDroppableWindow( HWND hSrc, ULONG ulPos, BOOL fExact)

{
    HWND        hRtn;
    PID         pid;
    TID         tid;
    POINTL      ptl;

do
{
    ptl.x = LOSHORT( ulPos);
    ptl.y = HISHORT( ulPos);
    WinMapWindowPoints( hSrc, HWND_DESKTOP, &ptl, 1);

    hRtn = WinWindowFromPoint( HWND_DESKTOP, &ptl, fExact);
    if (hRtn == 0)
        break;

    if (ulCP2Drag)
        if (WinQueryWindowProcess( hRtn, &pid, &tid) == FALSE ||
            pid == pidWPS || pid == pidCP ||
            pid == pidPM || pid == 0)
                hRtn = 0;

} while (fFalse);

    return (hRtn);
}

/**********************************************************************/

//  Display the info posted to the source window by the hook in
//  response to our request.  mp1 contains the target's tid & pid,
//  mp2 contains its current PM & Process CPs.  Since there can
//  only be 4096 threads, the 3 high-order bits of that word are
//  used as flags.

void        cpxShowReply( HWND hSrc, ULONG mp1, ULONG mp2)

{
    char szText[128];

// stop the timer which would cause an error msg to be displayed
    WinStopTimer( habMain, hSrc, CPX_TIMER);

// if the flags indicate an error, put an asterisk after the
// CP(s) which didn't get changed
    if (mp1 & (CPF_SETPMERR | CPF_SETPROCERR))
        sprintf( szText, "Error:  Pid %hd  cp=%hd%s - Tid %hd  PM cp=%hd%s",
                 HISHORT( mp1), HISHORT( mp2),
                 ((mp1 & CPF_SETPROCERR) ? "*" : ""),
                 LOSHORT( (mp1 & ~CPF_MASK)), LOSHORT( mp2),
                 ((mp1 & CPF_SETPMERR) ? "*" : ""));
    else
    sprintf( szText, "%s:  Pid %hd  Cp=%hd - Tid %hd  PM Cp=%hd",
             ((mp1 & CPF_SET) ? "Update" : "Query"),
             HISHORT( mp1), HISHORT( mp2),
             LOSHORT( (mp1 & ~CPF_MASK)), LOSHORT( mp2));

    WinSetDlgItemText( hMain, IDC_INFO, szText);

    return;
}

/**********************************************************************/

// invokes the main errorbox routine with a rtn code of zero

BOOL        cpxErrBox( PSZ pTtl, PSZ pErr)

{
    return (cpxRcErrBox( pTtl, pErr, 0));
}

/**********************************************************************/

// a dumb little msg box

BOOL        cpxRcErrBox( PSZ pTtl, PSZ pErr, ULONG rc)

{
    char    szText[256];

    if (rc)
        sprintf( szText, "%s:  %s  rc=%x", pTtl, pErr, rc);
    else
        sprintf( szText, "%s:  %s", pTtl, pErr);

    WinMessageBox( HWND_DESKTOP, NULLHANDLE, szText, "CpPal", IDD_ERRBOX,
                   (ULONG)MB_OK | (ULONG)MB_ICONEXCLAMATION | (ULONG)MB_MOVEABLE);

    return (FALSE);
}

/**********************************************************************/


