static const char *fonts[] = { "monospace:size=10" };

#define NOMODIFIER 0
#define LEADERMOD ControlMask
#define LEADERKEY XK_t

static const char *dmenucmd[] = { "dmenu_run", NULL };
static const char *termcmd[]  = { "st", NULL };

static Keys keys = {
	&(Keys){NULL,	     
		&(Keys){NULL,
			&(Keys){NULL,
				&(Keys){NULL,
					&(Keys){NULL,
						NULL,
						{NOMODIFIER, XK_q, quit, {0}}}, /* C-t q */
					{LEADERMOD, XK_g, toggleleader, {.i = 0}}}, /* C-t C-g */
				{NOMODIFIER, XK_b, banish, {0}}}, /* C-t b */
		 {NOMODIFIER, XK_c, spawn, {.v = termcmd}}}, /* C-t c */
		{NOMODIFIER, XK_p, spawn, {.v = dmenucmd}}}, /* C-t p */
	&(Keys){NULL,
		NULL, 	  
		{LEADERMOD, XK_g, toggleleader, {.i = 0}}}, /* C-g */
	{LEADERMOD, LEADERKEY, toggleleader, {.i = 1}}	    /* C-t */
};
