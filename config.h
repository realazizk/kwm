static const char *fonts[] = { "monospace:size=10" };

#define NOMODIFIER 0
#define LEADERMOD ControlMask
#define LEADERKEY XK_t

static const char *dmenucmd[] = { "dmenu_run", NULL };
static const char *termcmd[]  = { "st", NULL };
/* static const char *chgcmd[]   = {"wmctrl -i -a $(wmctrl -l | wmctrl -l|awk '{$3=""; $2=""; print $0}'|dmenu -l 10|awk '{print $1}')", NULL};	 */
static const char *emacs[] = { "emacs",  NULL, NULL, NULL, "Emacs" };
static const char *browser[] = { "firefox", NULL, NULL, NULL, "Firefox-esr" };
static const int borderpx = 2;

static Keys keys = {
	&(Keys){NULL,	     
		&(Keys){NULL,
			&(Keys){NULL,
				&(Keys){NULL,
					&(Keys){NULL,
						&(Keys){NULL,
							&(Keys) {NULL,
								 &(Keys){NULL,
									 &(Keys){NULL,
										 &(Keys){NULL,
											 &(Keys){NULL,
												 &(Keys){NULL,
													 &(Keys){NULL,
														 NULL,
														 {LEADERMOD, XK_f, runorraise, {.v = browser}}}, /* C-t C-f */
													 {LEADERMOD, XK_e, runorraise, {.v = emacs}}}, /* C-t C-e */
												 {LEADERMOD, XK_t, selclient, {0}}}, /* C-t C-t */
											 ShiftMask, XK_o, prevframe, {0}}, /* C-t O */
										 {NOMODIFIER, XK_o, nextframe, {0}}}, /* C-t o */
									 {NOMODIFIER, XK_p, prevclient, {0}}}, /* C-t p */
								 {NOMODIFIER, XK_n, nextclient, {0}}}, /* C-t n */
							{ShiftMask, XK_k, killclient, {0}}}, /* C-t K */
						{NOMODIFIER, XK_q, quit, {0}}}, /* C-t q */
					{LEADERMOD, XK_g, toggleleader, {.i = 0}}}, /* C-t C-g */
				{NOMODIFIER, XK_b, banish, {0}}}, /* C-t b */
		 {NOMODIFIER, XK_c, spawn, {.v = termcmd}}}, /* C-t c */
		{NOMODIFIER, XK_exclam, spawn, {.v = dmenucmd}}}, /* C-t ! */
	NULL,
	{LEADERMOD, LEADERKEY, toggleleader, {.i = 1}}	    /* C-t */
};
