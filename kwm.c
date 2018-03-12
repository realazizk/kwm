#include <X11/Xatom.h>
#include <unistd.h>
#include <X11/Xft/Xft.h>
#include <X11/Xlib.h>
#include <X11/Xproto.h>
#include <X11/cursorfont.h>
#include <X11/keysym.h>
#include <locale.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#ifdef XINERAMA
#include <X11/extensions/Xinerama.h>
#endif /* XINERAMA */

#include "util.h"
#include "drw.h"



/* Macros */
#define UNUSED(x) (void)(x)
#define LENGTH(X)               (sizeof X / sizeof X[0])
#define INTERSECT(x,y,w,h,m)    (MAX(0, MIN((x)+(w),(m)->wx+(m)->ww) - MAX((x),(m)->wx)) \
                               * MAX(0, MIN((y)+(h),(m)->wy+(m)->wh) - MAX((y),(m)->wy)))
#define CLEANMASK(mask)         (mask & (ShiftMask|ControlMask|Mod1Mask|Mod2Mask|Mod3Mask|Mod4Mask|Mod5Mask))


/* Enums */
enum { NetSupported, NetWMName, NetWMState, NetWMCheck,
       NetWMFullscreen, NetActiveWindow, NetWMWindowType,
       NetWMWindowTypeDialog, NetClientList, NetLast }; /* EWMH atoms */
enum { WMProtocols, WMDelete, WMState, WMTakeFocus, WMLast }; /* default atoms */
enum { CurNormal, CurLeaderKey, CurResize, CurMove, CurLast }; /* cursor */

/* Data structures */
typedef struct Client  Client;
typedef struct Monitor Monitor;

struct Client {
	char name[256];
	float mina, maxa;
	int x, y, w, h;
	int oldx, oldy, oldw, oldh;
	int basew, baseh, incw, inch, maxw, maxh, minw, minh;
	int bw, oldbw;
	int isfixed, isfloating, isurgent, neverfocus, oldstate, isfullscreen;
	Client *next;
	Client *snext;
	Monitor *mon;
	Window win;
};


struct Monitor {
	int num;
	int mx, my, mw, mh;   /* screen size */
	int wx, wy, ww, wh;   /* window area  */
	Client *clients;
	Client *sel;
	Client *stack;
	Monitor *next;
	Window barwin;
};

typedef union {
	int i;
	unsigned int ui;
	float f;
	const void *v;
} Arg;

typedef struct {
	unsigned int mod;
	KeySym keysym;
	void (*func)(const Arg *);
	const Arg arg;
} Key;


typedef struct Keys Keys;

struct Keys {
	Keys *child;
	Keys *siblings;
	Key key;
};

/* Procedures */
void die(const char *);
static void checkotherwm();
static int xerrorstart(Display *dpy, XErrorEvent *ee);
static int xerror(Display *dpy, XErrorEvent *ee);
static void setup(void);
static void run(void);
static void sigchld(int);
static int updategeom(void);
static void keypress(XEvent *);
static Monitor *createmon(void);
static void cleanupmon(Monitor *);
static Monitor *wintomon(Window);
static int getrootptr(int *, int *);
static Monitor* recttomon(int, int, int, int);
static Client* wintoclient(Window);
static void grabkeys(void);
static void spawn(const Arg *);
static void toggleleader(const Arg *);
static void banish(void);

/* static void cleanup(void); */
static void banish(const Arg *);
static void quit(const Arg *);

/* Variables */
static Display *dpy;
static int (*xerrorxlib)(Display *, XErrorEvent *);
static void (*handler[LASTEvent]) (XEvent *) = {
	/* [ClientMessage] = clientmessage, */
	/* [ConfigureRequest] = configurerequest, */
	/* [ConfigureNotify] = configurenotify, */
	/* [DestroyNotify] = destroynotify, */
	/* [EnterNotify] = enternotify, */
	/* [FocusIn] = focusin, */
	[KeyPress] = keypress,
	/* [MappingNotify] = mappingnotify, */
	/* [MapRequest] = maprequest, */
	/* [MotionNotify] = motionnotify, */
	/* [PropertyNotify] = propertynotify, */
	/* [UnmapNotify] = unmapnotify */
};
static int running = 1;
static int screen;
static int sw, sh;
static Window root, wmcheckwin;
static Drw *drw;
static Atom wmatom[WMLast], netatom[NetLast];
static Cur *cursor[CurLast];
static Monitor *mons, *selmon;

/* Configuration file */
#include "config.h"

static Keys *currkey = &keys;


void
quit(const Arg *arg)
{
	running = 0;
}

void
toggleleader(const Arg *arg)
{
	if (arg->i) XDefineCursor(dpy, root, cursor[CurLeaderKey]->cursor);
	else XDefineCursor(dpy, root, cursor[CurNormal]->cursor);
}

void
banish(const Arg *arg)
{
	XWarpPointer(dpy, None, root, 0, 0, 0, 0, selmon->mx + selmon->mw - 2, selmon->my + selmon->mh - 2);
}

void
spawn(const Arg *arg)
{
	if (fork() == 0) {
		if (dpy)
			close(ConnectionNumber(dpy));
		setsid();
		execvp(((char **)arg->v)[0], (char **)arg->v);
		fprintf(stderr, "kwm: execvp %s", ((char **)arg->v)[0]);
		perror(" failed");
		exit(EXIT_SUCCESS);
	}
}

int
xerrorstart(Display *dpy, XErrorEvent *ee)
{
	UNUSED(dpy);
	UNUSED(ee);
	
	die("kwm: another window manager is already running");
	return -1;
}

int
xerror(Display *dpy, XErrorEvent *ee)
{
	if (ee->error_code == BadWindow
	|| (ee->request_code == X_SetInputFocus && ee->error_code == BadMatch)
	|| (ee->request_code == X_PolyText8 && ee->error_code == BadDrawable)
	|| (ee->request_code == X_PolyFillRectangle && ee->error_code == BadDrawable)
	|| (ee->request_code == X_PolySegment && ee->error_code == BadDrawable)
	|| (ee->request_code == X_ConfigureWindow && ee->error_code == BadMatch)
	|| (ee->request_code == X_GrabButton && ee->error_code == BadAccess)
	|| (ee->request_code == X_GrabKey && ee->error_code == BadAccess)
	|| (ee->request_code == X_CopyArea && ee->error_code == BadDrawable))
		return 0;
	fprintf(stderr, "kwm: fatal error: request code=%d, error code=%d\n",
		ee->request_code, ee->error_code);
	return xerrorxlib(dpy, ee); /* may call exit */
}

void
sigchld(int unused)
{
	if (signal(SIGCHLD, sigchld) == SIG_ERR)
		die("can't install SIGCHLD handler:");
	while (0 < waitpid(-1, NULL, WNOHANG));
}

#ifdef XINERAMA
static int
isuniquegeom(XineramaScreenInfo *unique, size_t n, XineramaScreenInfo *info)
{
	while (n--)
		if (unique[n].x_org == info->x_org && unique[n].y_org == info->y_org
		&& unique[n].width == info->width && unique[n].height == info->height)
			return 0;
	return 1;
}
#endif /* XINERAMA */


Monitor *
recttomon(int x, int y, int w, int h)
{
	Monitor *m, *r = selmon;
	int a, area = 0;

	for (m = mons; m; m = m->next)
		if ((a = INTERSECT(x, y, w, h, m)) > area) {
			area = a;
			r = m;
		}
	return r;
}


void
cleanupmon(Monitor *mon)
{
	Monitor *m;

	if (mon == mons)
		mons = mons->next;
	else {
		for (m = mons; m && m->next != mon; m = m->next);
		m->next = mon->next;
	}
	XUnmapWindow(dpy, mon->barwin);
	XDestroyWindow(dpy, mon->barwin);
	free(mon);
}

int
updategeom(void)
{
	int dirty = 0;

#ifdef XINERAMA
	if (XineramaIsActive(dpy)) {
		int i, j, n, nn;
		Client *c;
		Monitor *m;
		XineramaScreenInfo *info = XineramaQueryScreens(dpy, &nn);
		XineramaScreenInfo *unique = NULL;

		for (n = 0, m = mons; m; m = m->next, n++);
		/* only consider unique geometries as separate screens */
		unique = ecalloc(nn, sizeof(XineramaScreenInfo));
		for (i = 0, j = 0; i < nn; i++)
			if (isuniquegeom(unique, j, &info[i]))
				memcpy(&unique[j++], &info[i], sizeof(XineramaScreenInfo));
		XFree(info);
		nn = j;
		if (n <= nn) { /* new monitors available */
			for (i = 0; i < (nn - n); i++) {
				for (m = mons; m && m->next; m = m->next);
				if (m)
					m->next = createmon();
				else
					mons = createmon();
			}
			for (i = 0, m = mons; i < nn && m; m = m->next, i++)
				if (i >= n
				|| unique[i].x_org != m->mx || unique[i].y_org != m->my
				|| unique[i].width != m->mw || unique[i].height != m->mh)
				{
					dirty = 1;
					m->num = i;
					m->mx = m->wx = unique[i].x_org;
					m->my = m->wy = unique[i].y_org;
					m->mw = m->ww = unique[i].width;
					m->mh = m->wh = unique[i].height;
				}
		} else { /* less monitors available nn < n */
			for (i = nn; i < n; i++) {
				for (m = mons; m && m->next; m = m->next);
				while ((c = m->clients)) {
					dirty = 1;
					m->clients = c->next;
					/* detachstack(c); */
					c->mon = mons;
					/* attach(c); */
					/* attachstack(c); */
				}
				if (m == selmon)
					selmon = mons;
				cleanupmon(m);
			}
		}
		free(unique);
	} else
#endif /* XINERAMA */
	{ /* default monitor setup */
		if (!mons)
			mons = createmon();
		if (mons->mw != sw || mons->mh != sh) {
			dirty = 1;
			mons->mw = mons->ww = sw;
			mons->mh = mons->wh = sh;
		}
	}
	if (dirty) {
		selmon = mons;
		selmon = wintomon(root);
	}
	return dirty;
}

Client *
wintoclient(Window w)
{
	Client *c;
	Monitor *m;

	for (m = mons; m; m = m->next)
		for (c = m->clients; c; c = c->next)
			if (c->win == w)
				return c;
	return NULL;
}
			
Monitor *
wintomon(Window w)
{
	int x, y;
	Client *c;
	Monitor *m;

	if (w == root && getrootptr(&x, &y))
		return recttomon(x, y, 1, 1);
	for (m = mons; m; m = m->next)
		if (w == m->barwin)
			return m;
	if ((c = wintoclient(w)))
		return c->mon;
	return selmon;
}

int
xerrordummy(Display *dpy, XErrorEvent *ee)
{
	return 0;
}


int
getrootptr(int *x, int *y)
{
	int di;
	unsigned int dui;
	Window dummy;

	return XQueryPointer(dpy, root, &dummy, &dummy, x, y, &di, &di, &dui);
}

 
Monitor *
createmon(void)
{
	Monitor *m;
	m = ecalloc(1, sizeof(Monitor));
	return m;
}


void
checkotherwm(void)
{
	xerrorxlib = XSetErrorHandler(xerrorstart);
	/* this causes an error if some other window manager is running */
	XSelectInput(dpy, DefaultRootWindow(dpy), SubstructureRedirectMask);
	XSync(dpy, False);
	XSetErrorHandler(xerror);
	XSync(dpy, False);
}


void setup(void)
{
	XSetWindowAttributes wa;
	Atom utf8string;

	/* clean up any zombies immediately */
	sigchld(0);

	/* init screen */
	screen = DefaultScreen(dpy);
	sw = DisplayWidth(dpy, screen);
	sh = DisplayHeight(dpy, screen);
	root = RootWindow(dpy, screen);
	drw = drw_create(dpy, screen, root, sw, sh);
	if (!drw_fontset_create(drw, fonts, LENGTH(fonts)))
		die("no fonts could be loaded.");
	updategeom();
	/* init atoms */
	utf8string = XInternAtom(dpy, "UTF8_STRING", False);
	wmatom[WMProtocols] = XInternAtom(dpy, "WM_PROTOCOLS", False);
	wmatom[WMDelete] = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
	wmatom[WMState] = XInternAtom(dpy, "WM_STATE", False);
	wmatom[WMTakeFocus] = XInternAtom(dpy, "WM_TAKE_FOCUS", False);
	netatom[NetActiveWindow] = XInternAtom(dpy, "_NET_ACTIVE_WINDOW", False);
	netatom[NetSupported] = XInternAtom(dpy, "_NET_SUPPORTED", False);
	netatom[NetWMName] = XInternAtom(dpy, "_NET_WM_NAME", False);
	netatom[NetWMState] = XInternAtom(dpy, "_NET_WM_STATE", False);
	netatom[NetWMCheck] = XInternAtom(dpy, "_NET_SUPPORTING_WM_CHECK", False);
	netatom[NetWMFullscreen] = XInternAtom(dpy, "_NET_WM_STATE_FULLSCREEN", False);
	netatom[NetWMWindowType] = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE", False);
	netatom[NetWMWindowTypeDialog] = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_DIALOG", False);
	netatom[NetClientList] = XInternAtom(dpy, "_NET_CLIENT_LIST", False);
	/* init cursors */
	cursor[CurNormal] = drw_cur_create(drw, XC_left_ptr);
	cursor[CurResize] = drw_cur_create(drw, XC_sizing);
	cursor[CurMove] = drw_cur_create(drw, XC_fleur);
	cursor[CurLeaderKey] = drw_cur_create(drw, XC_icon);
	
	/* supporting window for NetWMCheck */
	wmcheckwin = XCreateSimpleWindow(dpy, root, 0, 0, 1, 1, 0, 0, 0);
	XChangeProperty(dpy, wmcheckwin, netatom[NetWMCheck], XA_WINDOW, 32,
		PropModeReplace, (unsigned char *) &wmcheckwin, 1);
	XChangeProperty(dpy, wmcheckwin, netatom[NetWMName], utf8string, 8,
		PropModeReplace, (unsigned char *) "dwm", 3);
	XChangeProperty(dpy, root, netatom[NetWMCheck], XA_WINDOW, 32,
		PropModeReplace, (unsigned char *) &wmcheckwin, 1);
	/* EWMH support per view */
	XChangeProperty(dpy, root, netatom[NetSupported], XA_ATOM, 32,
		PropModeReplace, (unsigned char *) netatom, NetLast);
	XDeleteProperty(dpy, root, netatom[NetClientList]);
	/* select events */
	wa.cursor = cursor[CurNormal]->cursor;
	wa.event_mask = SubstructureRedirectMask|SubstructureNotifyMask
		|ButtonPressMask|PointerMotionMask|EnterWindowMask
		|LeaveWindowMask|StructureNotifyMask|PropertyChangeMask;
	XChangeWindowAttributes(dpy, root, CWEventMask|CWCursor, &wa);
	XSelectInput(dpy, root, wa.event_mask);
	grabkeys();
	/* focus(NULL); */
}

void
grabkeys(void)
{

	unsigned int i;
	unsigned int modifiers[] = { 0 }; /* For simplicity don't press other modifiers*/
	KeyCode code;

	XUngrabKey(dpy, AnyKey, AnyModifier, root);
	Keys *node = currkey;
	while (node) {
		if ((code = XKeysymToKeycode(dpy, node->key.keysym)))
			for (i = 0; i < LENGTH(modifiers); i++)
				XGrabKey(dpy, code, node->key.mod | modifiers[i], root,
					 True, GrabModeAsync, GrabModeAsync);
		node = node->siblings;
	}
}


void
run(void)
{
	XEvent ev;
	/* main event loop */
	XSync(dpy, False);
	XNextEvent(dpy, &ev);
	while (running && !XNextEvent(dpy, &ev))
		if (handler[ev.type])
			handler[ev.type](&ev);			
}

void
keypress(XEvent *e)
{
	unsigned int i;
	KeySym keysym;
	XKeyEvent *ev;

	ev = &e->xkey;
	keysym = XKeycodeToKeysym(dpy, (KeyCode)ev->keycode, 0);
	Keys *node = currkey;
	while (node) {
		if (keysym == node->key.keysym && CLEANMASK(node->key.mod) == CLEANMASK(ev->state) && node->key.func) {
			node->key.func(&(node->key.arg));
			currkey = node->child;
		}
		node = node->siblings;
	}
	if (currkey == NULL) {
		toggleleader(&(Arg){.i = 0});
		currkey = &keys;
	}
	grabkeys();
}


/* void */
/* cleanup(void) */
/* { */
/* 	Arg a = {.ui = ~0}; */
/* 	/\* Monitor *m; *\/ */
/* 	size_t i; */
/* 	for (m = mons; m; m = m->next) */
/* 		while (Clients) */
/* 			unmanage(Clients, 0); */
/* 	XUngrabKey(dpy, AnyKey, AnyModifier, root); */
/* 	while (mons) */
/* 		cleanupmon(mons); */
/* 	for (i = 0; i < CurLast; i++) */
/* 		drw_cur_free(drw, cursor[i]); */
/* 	for (i = 0; i < LENGTH(colors); i++) */
/* 		free(scheme[i]); */
/* 	XDestroyWindow(dpy, wmcheckwin); */
/* 	drw_free(drw); */
/* 	XSync(dpy, False); */
/* 	XSetInputFocus(dpy, PointerRoot, RevertToPointerRoot, CurrentTime); */
/* 	XDeleteProperty(dpy, root, netatom[NetActiveWindow]); */
/* } */


int
main(int argc, char *argv[])
{
	if (argc == 2 && !strcmp("-v", argv[1]))
		die("kwm-1");
	else if (argc != 1)
		die("usage: kwm [-v]");
	if (!setlocale(LC_CTYPE, "") || !XSupportsLocale())
		fputs("warning: no locale support\n", stderr);
	if (!(dpy = XOpenDisplay(NULL)))
		die("kwm: cannot open display");
	checkotherwm();
	setup();
	run();
	/* cleanup(); */
	XCloseDisplay(dpy);
	return 0;
}
