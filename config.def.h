/* See LICENSE file for copyright and license details. */
/* Default settings; can be overriden by command line. */

static int topbar  = 1; /* -b  option; if 0, dmenu appears at bottom */
static int opacity = 0; /* -o  option; if 0, then alpha is disabled */
static int fuzzy   = 1; /* -F  option; if 0, dmenu doesn't use fuzzy matching */
static int instant = 0; /* -n  option; if 1, selects matching item without the
			   need to press enter */
static int center
    = 1; /* -c  option; if 0, dmenu won't be centered on the screen */
static int min_width     = 500; /* minimum width when centered */
static const int vertpad = 10;  /* vertical padding of bar */
static const int sidepad = 10;  /* horizontal padding of bar */
/* -fn option overrides fonts[0]; default X11 font or font set */
static char *fonts[]     = {
    "FiraCode Nerd Font:style=Regular:pixelsize=16:antialias=true:hinting=true",
    "monospace:pixelsize=16",
    "JoyPixels:style=Regular:pixelsize=16:antialias=true:hinting=true",
    "Font Awesome 6 Free:style=Solid:pixelsize=16:antialias=true:hinting=true",
    "Font Awesome 6 "
        "Brands:style=Regular:pixelsize=16:antialias=true:hinting=true",
    "Font Awesome 6 "
        "Free:style=Regular:pixelsize=16:antialias=true:hinting=true"};
static char *prompt = NULL; /* -p  option; prompt to the left of input field */
static const char *dynamic
    = NULL; /* -dy option; dynamic command to run on input change */

static const unsigned int baralpha    = 0xd0;
static const unsigned int borderalpha = OPAQUE;
static const unsigned int alphas[][3] = {
  /*               fg      bg        border     */
    [SchemeNorm]          = {OPAQUE, baralpha, borderalpha},
    [SchemeSel]           = {OPAQUE, baralpha, borderalpha},
    [SchemeBorder]        = {OPAQUE,   OPAQUE,      OPAQUE},
    [SchemeSelHighlight]  = {OPAQUE, baralpha, borderalpha},
    [SchemeNormHighlight] = {OPAQUE, baralpha, borderalpha},
    [SchemeHp]            = {OPAQUE, baralpha, borderalpha},
    [SchemeHover]         = {OPAQUE, baralpha, borderalpha},
    [SchemeGreen]         = {OPAQUE, baralpha, borderalpha},
    [SchemeRed]           = {OPAQUE, baralpha, borderalpha},
    [SchemeYellow]        = {OPAQUE, baralpha, borderalpha},
    [SchemeBlue]          = {OPAQUE, baralpha, borderalpha},
    [SchemePurple]        = {OPAQUE, baralpha, borderalpha},
};

static char *colors[][2] = {
  /*               fg         bg       */
    [SchemeNorm]          = {"#bbbbbb", "#222222"},
    [SchemeSel]           = {"#eeeeee", "#005577"},
    [SchemeOut]           = {"#000000", "#00ffff"},
    [SchemeBorder]        = {"#000000", "#005577"},
    [SchemeSelHighlight]  = {"#ffc978", "#005577"},
    [SchemeNormHighlight] = {"#ffc978", "#222222"},
    [SchemeHp]            = {"#bbbbbb", "#333333"},
    [SchemeHover]         = {"#ffffff", "#353D4B"},
    [SchemeGreen]         = {"#ffffff", "#52E067"},
    [SchemeRed]           = {"#ffffff", "#e05252"},
    [SchemeYellow]        = {"#ffffff", "#e0c452"},
    [SchemeBlue]          = {"#ffffff", "#5280e0"},
    [SchemePurple]        = {"#ffffff", "#9952e0"},
};
/* -l option; if nonzero, dmenu uses vertical list with given number of lines */
static unsigned int lines   = 15;
/* -g option; if nonzero, dmenu uses a grid comprised of columns and lines */
static unsigned int columns = 0;
static unsigned int lineheight
    = 0; /* -h option; minimum height of a menu line     */
static unsigned int min_lineheight = 8;
static unsigned int maxhist        = 50;
static int histnodup               = 1; /* if 0, record repeated histories */

/*
 * Characters not considered part of a word while deleting words
 * for example: " /?\"&[]"
 */
static const char worddelimiters[] = " ";

/* Size of the window border */
static unsigned int border_width = 3;

/*
 * Use prefix matching by default; can be inverted with the -x flag.
 */
static int use_prefix = 1;
