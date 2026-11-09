#pragma once

// App icon. 100 is the conventional screensaver icon ID (ID_APP in scrnsave.h);
// as the first icon resource it's also what Explorer shows for the .exe/.scr.
#define IDI_APP           100

// String 1 = the name Windows shows in the Screen Saver Settings dropdown.
#define IDS_DESCRIPTION     1

// Config dialog (/c) control IDs.
#define IDD_CONFIG        101

#define IDC_PICPATH      1001
#define IDC_BROWSE       1002
#define IDC_SENS         1003
#define IDC_SENS_VAL     1004
#define IDC_BRIGHT       1005
#define IDC_BRIGHT_VAL   1006
#define IDC_IDLETEXT     1007
#define IDC_SCREENMODE   1008
#define IDC_SCREENLIST   1009
#define IDC_IDENTIFY     1010

#ifndef IDC_STATIC
#define IDC_STATIC         -1
#endif
