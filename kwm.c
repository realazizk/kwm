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
#define WIDTH(X)                ((X)->w + 2 * (X)->bw)
#define HEIGHT(X)               ((X)->h + 2 * (X)->bw)
#define ColBorder               2

/* Enums */
enum { NetSupported, NetWMName, NetWMState, NetWMCheck,
       NetWMFullscreen, NetActiveWindow, NetWMWindowType,
       NetWMWindowTypeDialog, NetClientList, NetLast }; /* EWMH atoms */
enum { WMProtocols, WMDelete, WMState, WMTakeFocus, WMLast }; /* default atoms */
enum { CurNormal, CurLeaderKey, CurResize, CurMove, CurLast }; /* cursor */
enum { SchemeNorm, SchemeSel }; /* color schemes */

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
	Monitor *mon;
	Window win;
};

struct Monitor {
	int num;
	int mx, my, mw, mh;   /* screen size */
	int wx, wy, ww, wh;   /* window area  */
	Client *clients;
	Client *sel;
	Monitor *next;
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

static void unfocus(Client *, int);
static void clientmessage(XEvent *);
static void setfullscreen(Client *, int);
static void updatewindowtype(Client *);
static void unmapnotify(XEvent *);
static void updatewmhints(Client *);
static void attach(Client *);
static void updatetitle(Client *);
static void maprequest(XEvent *);
static void unmanage(Client *, int);
static void updateclientlist();
static void setclientstate(Client *, long);
static void checkotherwm();
static int xerrorstart(Display *, XErrorEvent *);
static void focusin(XEvent *e);
static int xerror(Display *, XErrorEvent *);
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
static void banish(const Arg *);
static void quit(const Arg *);
static void cleanup(void);
static int xerrordummy(Display *, XErrorEvent *);
static void manage(Window, XWindowAttributes *);
static void resize(Client *, int, int, int, int, int);
static void resizeclient(Client *c, int x, int y, int w, int h);
static int applysizehints(Client *, int *, int *, int *, int *, int);
static void configure(Client *);
static int sendevent(Client *, Atom);
static void killclient(const Arg *);
static void stopclient(const Arg *);
static int gettextprop(Window, Atom, char *, unsigned int);
static void focus(Client *);
static void destroynotify(XEvent *);
static void nextclient(const Arg *);
static void prevclient(const Arg *);
static void configurenotify(XEvent *);
static void prevframe(const Arg *);
static void nextframe(const Arg *);
static void selclient(const Arg *);
static void runorraise(const Arg *);
static Atom getatomprop(Client *, Atom);
static void configurerequest(XEvent *e);

/* Variables */

static Clr **scheme;
static Display *dpy;
static int (*xerrorxlib)(Display *, XErrorEvent *);
static void (*handler[LASTEvent]) (XEvent *) = {
	[KeyPress] = keypress,
	[MapRequest] = maprequest,
	[UnmapNotify] = unmapnotify,
	[FocusIn] = focusin,
	[DestroyNotify] = destroynotify,
	[ConfigureNotify] = configurenotify,
	[ClientMessage] = clientmessage,
	[ConfigureRequest] = configurerequest,
};

static const char broken[] = "broken";
static int running = 1;
static int screen;
static int sw, sh;
static Window root, wmcheckwin;
static Drw *drw;
static Atom wmatom[WMLast], netatom[NetLast];
static Cur *cursor[CurLast];
static Monitor *mons, *selmon;
static Client *lastclient;
static int pointergrabbed;


/* Configuration file */
#include "config.h"

static Keys *currkey = &keys;


void
quit(const Arg *arg)
{
	running = 0;
}

Atom
getatomprop(Client *c, Atom prop)
{
	int di;
	unsigned long dl;
	unsigned char *p = NULL;
	Atom da, atom = None;

	if (XGetWindowProperty(dpy, c->win, prop, 0L, sizeof atom, False, XA_ATOM,
		&da, &di, &dl, &dl, &p) == Success && p) {
		atom = *(Atom *)p;
		XFree(p);
	}
	return atom;
}


void
focusin(XEvent *e)
{
	XFocusChangeEvent *ev = &e->xfocus;

	if (selmon->sel && ev->window != selmon->sel->win) {
		if (!selmon->sel->neverfocus) {
			XSetInputFocus(dpy, selmon->sel->win, RevertToPointerRoot, CurrentTime);
			XChangeProperty(dpy, root, netatom[NetActiveWindow],
					XA_WINDOW, 32, PropModeReplace,
					(unsigned char *) &(selmon->sel->win), 1);
		}
		sendevent(selmon->sel, wmatom[WMTakeFocus]);
	}
		
}


void
configurenotify(XEvent *e)
{
	Monitor *m;
	Client *c;
	XConfigureEvent *ev = &e->xconfigure;
	int dirty;
	
	/* TODO: updategeom handling sucks, needs to be simplified */
	if (ev->window == root) {
		dirty = (sw != ev->width || sh != ev->height);
		sw = ev->width;
		sh = ev->height;
		if (updategeom() || dirty) {
			drw_resize(drw, sw, sh);
			for (m = mons; m; m = m->next) {
				for (c = m->clients; c; c = c->next)
					/* FIXME: fix this when add frames */
					resizeclient(c, m->mx, m->my, m->mw, m->mh);
			}
			focus(NULL);
		}
	}
}


void
toggleleader(const Arg *arg)
{
	pointergrabbed = arg->i;
	if (arg->i) XGrabPointer(dpy, root, True, 0,
				  GrabModeAsync, GrabModeAsync,
				  None, cursor[CurLeaderKey]->cursor, CurrentTime);
	else XUngrabPointer(dpy, CurrentTime);
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
					c->mon = mons;
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

	if (w == root && getrootptr(&x, &y))
		return recttomon(x, y, 1, 1);
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
	int i;
	
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

	/* Init colors */
	scheme = ecalloc(LENGTH(colors), sizeof(Clr *));
	for (i = 0; i < LENGTH(colors); i++)
		scheme[i] = drw_scm_create(drw, colors[i], 3);
	
	/* supporting window for NetWMCheck */
	wmcheckwin = XCreateSimpleWindow(dpy, root, 0, 0, 1, 1, 0, 0, 0);
	XChangeProperty(dpy, wmcheckwin, netatom[NetWMCheck], XA_WINDOW, 32,
		PropModeReplace, (unsigned char *) &wmcheckwin, 1);
	XChangeProperty(dpy, wmcheckwin, netatom[NetWMName], utf8string, 8,
		PropModeReplace, (unsigned char *) "kwm", 3);
	XChangeProperty(dpy, root, netatom[NetWMCheck], XA_WINDOW, 32,
		PropModeReplace, (unsigned char *) &wmcheckwin, 1);
	/* EWMH support per view */
	XChangeProperty(dpy, root, netatom[NetSupported], XA_ATOM, 32,
		PropModeReplace, (unsigned char *) netatom, NetLast);
	XDeleteProperty(dpy, root, netatom[NetClientList]);
	/* select events */
	wa.cursor = cursor[CurNormal]->cursor;
	wa.event_mask = SubstructureRedirectMask|SubstructureNotifyMask
		|ButtonPressMask|PointerMotionMask
		|StructureNotifyMask|PropertyChangeMask;
	XChangeWindowAttributes(dpy, root, CWEventMask|CWCursor, &wa);
	XSelectInput(dpy, root, wa.event_mask);
	grabkeys();
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

void
detach(Client *c)
{
	Client **tc;

	for (tc = &c->mon->clients; *tc && *tc != c; tc = &(*tc)->next);
	*tc = c->next;
}


void
unmanage(Client *c, int destroyed)
{
	XWindowChanges wc;

	detach(c);
	if (!destroyed) {
		wc.border_width = c->oldbw;
		XGrabServer(dpy); /* avoid race conditions */
		XSetErrorHandler(xerrordummy);
		XConfigureWindow(dpy, c->win, CWBorderWidth, &wc); /* restore border */
		XUngrabButton(dpy, AnyButton, AnyModifier, c->win);
		setclientstate(c, WithdrawnState);
		XSync(dpy, False);
		XSetErrorHandler(xerror);
		XUngrabServer(dpy);
	}
	c->mon->sel = c->next;
	if (lastclient && lastclient->mon == c->mon)
		focus(lastclient);
	else
		focus(NULL);
	free(c);
	updateclientlist();
}

void
updateclientlist()
{
	Client *c;
	Monitor *m;

	XDeleteProperty(dpy, root, netatom[NetClientList]);
	for (m = mons; m; m = m->next)
		for (c = m->clients; c; c = c->next)
			XChangeProperty(dpy, root, netatom[NetClientList],
				XA_WINDOW, 32, PropModeAppend,
				(unsigned char *) &(c->win), 1);
}


void
runorraise(const Arg *arg)
{
	char *app = ((char **)arg->v)[4];
	Arg a = { .ui = ~0 };
	Monitor *mon;
	Client *c;
	XClassHint hint = { NULL, NULL };
	for (mon = mons; mon; mon = mon->next) {
		for (c = mon->clients; c; c = c->next) {
			XGetClassHint(dpy, c->win, &hint);
			if (hint.res_class && strcmp(app, hint.res_class) == 0) {
				focus(c);
				return;
			}
		}
	}
	spawn(arg);
}


void
unmapnotify(XEvent *e)
{
	Client *c;
	XUnmapEvent *ev = &e->xunmap;
	if ((c = wintoclient(ev->window))) {
		if (ev->send_event) 
			setclientstate(c, WithdrawnState);
		else
			unmanage(c, 0);		
	}
}

void
setclientstate(Client *c, long state)
{
	long data[] = { state, None };

	XChangeProperty(dpy, c->win, wmatom[WMState], wmatom[WMState], 32,
		PropModeReplace, (unsigned char *)data, 2);
}

void
updatetitle(Client *c)
{
	if (!gettextprop(c->win, netatom[NetWMName], c->name, sizeof c->name))
		gettextprop(c->win, XA_WM_NAME, c->name, sizeof c->name);
	if (c->name[0] == '\0') /* hack to mark broken clients */
		strcpy(c->name, broken);
}

int
gettextprop(Window w, Atom atom, char *text, unsigned int size)
{
	char **list = NULL;
	int n;
	XTextProperty name;

	if (!text || size == 0)
		return 0;
	text[0] = '\0';
	if (!XGetTextProperty(dpy, w, &name, atom) || !name.nitems)
		return 0;
	if (name.encoding == XA_STRING)
		strncpy(text, (char *)name.value, size - 1);
	else {
		if (XmbTextPropertyToTextList(dpy, &name, &list, &n) >= Success && n > 0 && *list) {
			strncpy(text, *list, size - 1);
			XFreeStringList(list);
		}
	}
	text[size - 1] = '\0';
	XFree(name.value);
	return 1;
}


int
applysizehints(Client *c, int *x, int *y, int *w, int *h, int interact)
{
	Monitor *m = c->mon;

	/* set minimum possible */
	*w = MAX(1, *w);
	*h = MAX(1, *h);
	if (interact) {
		if (*x > sw)
			*x = sw - WIDTH(c);
		if (*y > sh)
			*y = sh - HEIGHT(c);
		if (*x + *w + 2 * c->bw < 0)
			*x = 0;
		if (*y + *h + 2 * c->bw < 0)
			*y = 0;
	} else {
		if (*x >= m->wx + m->ww)
			*x = m->wx + m->ww - WIDTH(c);
		if (*y >= m->wy + m->wh)
			*y = m->wy + m->wh - HEIGHT(c);
		if (*x + *w + 2 * c->bw <= m->wx)
			*x = m->wx;
		if (*y + *h + 2 * c->bw <= m->wy)
			*y = m->wy;
	}

	return *x != c->x || *y != c->y || *w != c->w || *h != c->h;
}

int
sendevent(Client *c, Atom proto)
{
	int n;
	Atom *protocols;
	int exists = 0;
	XEvent ev;

	if (XGetWMProtocols(dpy, c->win, &protocols, &n)) {
		while (!exists && n--)
			exists = protocols[n] == proto;
		XFree(protocols);
	}
	if (exists) {
		ev.type = ClientMessage;
		ev.xclient.window = c->win;
		ev.xclient.message_type = wmatom[WMProtocols];
		ev.xclient.format = 32;
		ev.xclient.data.l[0] = proto;
		ev.xclient.data.l[1] = CurrentTime;
		XSendEvent(dpy, c->win, False, NoEventMask, &ev);
	}
	return exists;
}

void
stopclient(const Arg *arg)
{
	if (!selmon->sel)
		return;
	sendevent(selmon->sel, wmatom[WMDelete]);
}

void
killclient(const Arg *arg)
{
	XGrabServer(dpy);
	XSetErrorHandler(xerrordummy);
	XSetCloseDownMode(dpy, DestroyAll);
	XKillClient(dpy, selmon->sel->win);
	XSync(dpy, False);
	XSetErrorHandler(xerror);
	XUngrabServer(dpy);
}


void
configure(Client *c)
{
	XConfigureEvent ce;

	ce.type = ConfigureNotify;
	ce.display = dpy;
	ce.event = c->win;
	ce.window = c->win;
	ce.x = c->x;
	ce.y = c->y;
	ce.width = c->w;
	ce.height = c->h;
	ce.border_width = c->bw;
	ce.above = None;
	ce.override_redirect = False;
	XSendEvent(dpy, c->win, False, StructureNotifyMask, (XEvent *)&ce);
}

void
configurerequest(XEvent *e)
{
	Client *c;
	Monitor *m;
	XConfigureRequestEvent *ev = &e->xconfigurerequest;
	XWindowChanges wc;

	if ((c = wintoclient(ev->window))) {
		if (ev->value_mask & CWBorderWidth)
			c->bw = ev->border_width;
		else if (c->isfloating) {
			m = c->mon;
			if (ev->value_mask & CWX) {
				c->oldx = c->x;
				c->x = m->mx + ev->x;
			}
			if (ev->value_mask & CWY) {
				c->oldy = c->y;
				c->y = m->my + ev->y;
			}
			if (ev->value_mask & CWWidth) {
				c->oldw = c->w;
				c->w = ev->width;
			}
			if (ev->value_mask & CWHeight) {
				c->oldh = c->h;
				c->h = ev->height;
			}
			if ((c->x + c->w) > m->mx + m->mw && c->isfloating)
				c->x = m->mx + (m->mw / 2 - WIDTH(c) / 2); /* center in x direction */
			if ((c->y + c->h) > m->my + m->mh && c->isfloating)
				c->y = m->my + (m->mh / 2 - HEIGHT(c) / 2); /* center in y direction */
			if ((ev->value_mask & (CWX|CWY)) && !(ev->value_mask & (CWWidth|CWHeight)))
				configure(c);
			XMoveResizeWindow(dpy, c->win, c->x, c->y, c->w, c->h);
		} else
			configure(c);
	} else {
		wc.x = ev->x;
		wc.y = ev->y;
		wc.width = ev->width;
		wc.height = ev->height;
		wc.border_width = ev->border_width;
		wc.sibling = ev->above;
		wc.stack_mode = ev->detail;
		XConfigureWindow(dpy, ev->window, ev->value_mask, &wc);
	}
	XSync(dpy, False);
}


void
nextclient(const Arg *arg)
{
	Client *c;
	if (!selmon->clients) return;
	if (selmon->sel == selmon->clients)
		for (c = selmon->clients; c->next; c = c->next);
	else
		for (c = selmon->clients; c->next != selmon->sel ; c = c->next);
	if (c) focus(c);
}

void
prevclient(const Arg *arg)
{
	if (!selmon->sel) return;
	if (selmon->sel->next)
		focus(selmon->sel->next);
	else
		focus(selmon->clients);
}

void
selclient(const Arg *arg)
{
	focus(lastclient);
}

void
nextframe(const Arg *arg)
{
	/* FIXME: Operate on frames not on monitors */

	Monitor *m;

	if (selmon->next) m = selmon->next;
	else m = mons;
	unfocus(selmon->sel, 0);
	selmon = m;
	focus(selmon->sel);
}

void
prevframe(const Arg *arg)
{
	/* FIXME: Operate on frames not on monitors */

	Monitor *m;
	
	if (selmon->next) m = selmon->next;
	else m = mons;
	unfocus(selmon->sel, 0);
	selmon = m;
	focus(selmon->sel);
}

void
manage(Window w, XWindowAttributes *wa)
{
	Client *c, *t = NULL;
	Window trans = None;
	XWindowChanges wc;
	
	c = ecalloc(1, sizeof(Client));
	c->win = w;
	/* geometry */
	c->x = c->oldx = wa->x;
	c->y = c->oldy = wa->y;
	c->w = c->oldw = wa->width;
	c->h = c->oldh = wa->height;
	
	c->oldbw = wa->border_width;

	updatetitle(c);
	if (XGetTransientForHint(dpy, w, &trans) && (t = wintoclient(trans)))
		c->mon = t->mon;
        else {
		c->mon = selmon;
	}

	if (c->x + WIDTH(c) > c->mon->mx + c->mon->mw)
		c->x = c->mon->mx + c->mon->mw - WIDTH(c);
	if (c->y + HEIGHT(c) > c->mon->my + c->mon->mh)
		c->y = c->mon->my + c->mon->mh - HEIGHT(c);
	c->x = MAX(c->x, c->mon->mx);
	c->y = MAX(c->y, c->mon->my);
	
	c->bw = borderpx;

	wc.border_width = c->bw;
	XConfigureWindow(dpy, w, CWBorderWidth, &wc);
	XSetWindowBorder(dpy, w, scheme[SchemeNorm][ColBorder].pixel);
	configure(c); /* propagates border_width, if size doesn't change */
	updatewindowtype(c);
	updatewmhints(c);
	XSelectInput(dpy, w, EnterWindowMask|FocusChangeMask|PropertyChangeMask|StructureNotifyMask);
	
	if (!c->isfloating) {
		resize(c, c->mon->wx, c->mon->wy, c->mon->ww - 2 * c->bw, c->mon->wh - 2 * c->bw, 0);
		c->isfloating = c->oldstate = trans != None || c->isfixed;
	}
	
	if (c->isfloating) {
		XRaiseWindow(dpy, c->win);
		resize(c, c->x, c->y, c->w, c->h, 0);
	}
		
	attach(c);
	XChangeProperty(dpy, root, netatom[NetClientList], XA_WINDOW, 32, PropModeAppend,
		(unsigned char *) &(c->win), 1);

	
	setclientstate(c, NormalState);
	XMapWindow(dpy, c->win);
	c->mon->sel = c;
	focus(NULL);
}

void
setfullscreen(Client *c, int fullscreen)
{
	if (fullscreen && !c->isfullscreen) {
		XChangeProperty(dpy, c->win, netatom[NetWMState], XA_ATOM, 32,
			PropModeReplace, (unsigned char*)&netatom[NetWMFullscreen], 1);
		c->isfullscreen = 1;
		c->oldstate = c->isfloating;
		c->oldbw = c->bw;
		c->bw = 0;
		c->isfloating = 1;
		resizeclient(c, c->mon->mx, c->mon->my, c->mon->mw, c->mon->mh);
		XRaiseWindow(dpy, c->win);
	} else if (!fullscreen && c->isfullscreen){
		XChangeProperty(dpy, c->win, netatom[NetWMState], XA_ATOM, 32,
			PropModeReplace, (unsigned char*)0, 0);
		c->isfullscreen = 0;
		c->isfloating = c->oldstate;
		c->bw = c->oldbw;
		c->x = c->oldx;
		c->y = c->oldy;
		c->w = c->oldw;
		c->h = c->oldh;
		resizeclient(c, c->x, c->y, c->w, c->h);
	}
}

void
clientmessage(XEvent *e)
{
	XClientMessageEvent *cme = &e->xclient;
	Client *c = wintoclient(cme->window);

	if (!c)
		return;
	if (cme->message_type == netatom[NetWMState]) {
		if (cme->data.l[1] == netatom[NetWMFullscreen]
		|| cme->data.l[2] == netatom[NetWMFullscreen])
			setfullscreen(c, (cme->data.l[0] == 1 /* _NET_WM_STATE_ADD    */
				|| (cme->data.l[0] == 2 /* _NET_WM_STATE_TOGGLE */ && !c->isfullscreen)));
	}
}


void
updatewindowtype(Client *c)
{
	Atom state = getatomprop(c, netatom[NetWMState]);
	Atom wtype = getatomprop(c, netatom[NetWMWindowType]);

	if (state == netatom[NetWMFullscreen])
		setfullscreen(c, 1);
	if (wtype == netatom[NetWMWindowTypeDialog])
		c->isfloating = 1;
}


void
destroynotify(XEvent *e)
{
	Client *c;

	XDestroyWindowEvent *ev = &e->xdestroywindow;
	if ((c = wintoclient(ev->window)))
		unmanage(c, 1);
}

void
updatewmhints(Client *c)
{
	XWMHints *wmh;

	if ((wmh = XGetWMHints(dpy, c->win))) {
		if (c == selmon->sel && wmh->flags & XUrgencyHint) {
			wmh->flags &= ~XUrgencyHint;
			XSetWMHints(dpy, c->win, wmh);
		} else
			c->isurgent = (wmh->flags & XUrgencyHint) ? 1 : 0;
		if (wmh->flags & InputHint)
			c->neverfocus = !wmh->input;
		else
			c->neverfocus = 0;
		XFree(wmh);
	}
}

void
attach(Client *c)
{
	/* FIXME: Use an LL instead of stack */
	c->next = c->mon->clients;
	c->mon->clients = c;
}

void
resize(Client *c, int x, int y, int w, int h, int interact)
{
	if (applysizehints(c, &x, &y, &w, &h, interact))
		resizeclient(c, x, y, w, h);
}

void
unfocus(Client *c, int setfocus)
{
	if (!c)
		return;
	XSetWindowBorder(dpy, c->win, scheme[SchemeNorm][ColBorder].pixel);
	if (setfocus) {
		XSetInputFocus(dpy, root, RevertToPointerRoot, CurrentTime);
		XDeleteProperty(dpy, root, netatom[NetActiveWindow]);
	}
}

void
focus(Client *c)
{
	if (!c)
		c = selmon->clients;
	if (selmon->sel && selmon->sel != c)
		unfocus(selmon->sel, 0);
	if (c) {
		if (c->mon != selmon)
			selmon = c->mon;
		lastclient = selmon->sel;
		selmon->sel = c;
		XSetWindowBorder(dpy, c->win, scheme[SchemeSel][ColBorder].pixel);
		if (!c->neverfocus) {
			XSetInputFocus(dpy, c->win, RevertToPointerRoot, CurrentTime);
			XChangeProperty(dpy, root, netatom[NetActiveWindow],
					XA_WINDOW, 32, PropModeReplace,
					(unsigned char *) &(c->win), 1);
		}
		sendevent(c, wmatom[WMTakeFocus]);
		XRaiseWindow(dpy, c->win);
	} else {
		XSetInputFocus(dpy, root, RevertToPointerRoot, CurrentTime);
		XDeleteProperty(dpy, root, netatom[NetActiveWindow]);
	}
}

void
resizeclient(Client *c, int x, int y, int w, int h)
{
	XWindowChanges wc;

	c->oldx = c->x; c->x = wc.x = x;
	c->oldy = c->y; c->y = wc.y = y;
	c->oldw = c->w; c->w = wc.width = w;
	c->oldh = c->h; c->h = wc.height = h;
	wc.border_width = c->bw;
	XConfigureWindow(dpy, c->win, CWX|CWY|CWWidth|CWHeight|CWBorderWidth, &wc);
	configure(c);
	XSync(dpy, False);
}


void
cleanup(void)
{
	Monitor *m;
	size_t i;

	for (m = mons; m; m = m->next)
		while (m->clients)
			unmanage(m->clients, 0);
	XUngrabKey(dpy, AnyKey, AnyModifier, root);
	for (i = 0; i < CurLast; i++)
		drw_cur_free(drw, cursor[i]);
	for (i = 0; i < LENGTH(colors); i++)
		free(scheme[i]);
	XDestroyWindow(dpy, wmcheckwin);
	drw_free(drw);
	XSync(dpy, False);
	XSetInputFocus(dpy, PointerRoot, RevertToPointerRoot, CurrentTime);
	XDeleteProperty(dpy, root, netatom[NetActiveWindow]);
}


void
maprequest(XEvent *e)
{
	static XWindowAttributes wa;
	XMapRequestEvent *ev = &e->xmaprequest;
	
	if (!XGetWindowAttributes(dpy, ev->window, &wa))
		return;
	if (wa.override_redirect)
		return;
	if (!wintoclient(ev->window))
		manage(ev->window, &wa);
}


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
	cleanup();
	XCloseDisplay(dpy);
	return 0;
}
