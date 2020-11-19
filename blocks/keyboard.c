#include <stdio.h>
#include <string.h>

#include "../util.h"
#include "keyboard.h"

#include <X11/XKBlib.h>
#include <X11/extensions/XKBrules.h>

#define ICON COL1 "" COL0

int getlayout(char* buf, int sz) {
	int ret = 0;

	Display* dpy = XOpenDisplay(NULL);

	XkbStateRec state;
	XkbGetState(dpy, XkbUseCoreKbd, &state);

	XkbDescPtr desc	 = XkbGetKeyboard(dpy, XkbAllComponentsMask, XkbUseCoreKbd);
	char*	   group = XGetAtomName(dpy, desc->names->groups[state.group]);

	XkbRF_VarDefsRec vd;
	XkbRF_GetNamesProp(dpy, NULL, &vd);

	char* tok = strtok(vd.layout, ",");

	for (int i = 0; i < state.group; i++) {
		tok = strtok(NULL, ",");
		if (tok == NULL) {
			ret = 1;
			goto done;
		}
	}
	strcpy(buf, tok);
done:
	XFree(group);
	XCloseDisplay(dpy);
	return ret;
}

void keyboardu(char* str, int sigval) {
	char buf[7 + 1];
	if (getlayout(buf, sizeof buf) != 0) {
		return;
	}
	sprintf(str, ICON " %s", buf);
}

void keyboardc(int button) {
}
