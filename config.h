static const char *fonts[] = { "monospace:size=10" };

#define NOMODIFIER 0
#define LEADERMOD ControlMask
#define LEADERKEY XK_t

static const char *dmenucmd[] = { "dmenu_run", NULL };
static const char *termcmd[]  = { "st", NULL };

static Key keys[] = {
	/* modifier                     key        function        argument */
	{LEADERMOD,                    LEADERKEY,      toggleleader,   {.i = 1}},
	/* Disables the LEADER combo */
	{LEADERMOD,                    XK_g,           toggleleader,   {.i = 0}},
	{NOMODIFIER,                   XK_p,           spawn,          {.v = dmenucmd}},
	{NOMODIFIER,                   XK_c,           spawn,          {.v = termcmd}},
	{NOMODIFIER,                  XK_b,           banish,         {}},
};
