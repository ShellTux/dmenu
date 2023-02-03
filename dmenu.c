/* See LICENSE file for copyright and license details. */
#include <ctype.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#ifdef XINERAMA
#include <X11/extensions/Xinerama.h>
#endif
#include <X11/Xft/Xft.h>

/* Patch incompatibility overrides */
#undef NON_BLOCKING_STDIN_PATCH
#undef PIPEOUT_PATCH
#undef PRINTINPUTTEXT_PATCH

#include "drw.h"
#include "util.h"
#include <stdbool.h>

/* macros */
#define INTERSECT(x,y,w,h,r)  (MAX(0, MIN((x)+(w),(r).x_org+(r).width)  - MAX((x),(r).x_org)) \
                             * MAX(0, MIN((y)+(h),(r).y_org+(r).height) - MAX((y),(r).y_org)))
#define LENGTH(X)             (sizeof X / sizeof X[0])
#define TEXTW(X)              (drw_fontset_getwidth(drw, (X)) + lrpad)
#define OPAQUE                0xffU
#define OPACITY               "_NET_WM_WINDOW_OPACITY"

/* enums */
enum {
	SchemeNorm,
	SchemeSel,
	SchemeOut,
	SchemeBorder,
	SchemeNormHighlight,
	SchemeSelHighlight,
	SchemeHp,
	SchemeHover,
	SchemeGreen,
	SchemeYellow,
	SchemeBlue,
	SchemePurple,
	SchemeRed,
	SchemeLast,
}; /* color schemes */

struct item {
	char *text;
	char *text_output;
	struct item *left, *right;
	int id; /* for multiselect */
	int hp;
	double distance;
	int index;
};

static char text[BUFSIZ] = "";
static char *embed;
static char separator;
static int separator_greedy;
static int separator_reverse;
static int bh, mw, mh;
static int dmx = 0, dmy = 0; /* put dmenu at these x and y offsets */
static unsigned int dmw = 0; /* make dmenu this wide */
static int inputw = 0, promptw;
static int passwd = 0;
static int lrpad; /* sum of left and right padding */
static int vp; /* vertical padding for bar */
static int sp; /* side padding for bar */
static size_t cursor;
static struct item *items = NULL;
static struct item *matches, *matchend;
static struct item *prev, *curr, *next, *sel;
static int mon = -1, screen;
static int print_index = 0;
static int managed = 0;
static int *selid = NULL;
static unsigned int selidsize = 0;
static unsigned int sortmatches = 1;
static unsigned int preselected = 0;
static int commented = 0;
static int animated = 0;

static Atom clip, utf8;
static Atom type, dock;
static Display *dpy;
static Window root, parentwin, win;
static XIC xic;

static int useargb = 0;
static Visual *visual;
static int depth;
static Colormap cmap;

static Drw *drw;
static Clr *scheme[SchemeLast];

#include "patch/include.h"

#include "config.h"

static char * cistrstr(const char *s, const char *sub);
static int (*fstrncmp)(const char *, const char *, size_t) = strncasecmp;
static char *(*fstrstr)(const char *, const char *) = cistrstr;

static unsigned int
textw_clamp(const char *str, unsigned int n)
{
	unsigned int w = drw_fontset_getwidth_clamp(drw, str, n) + lrpad;
	return MIN(w, n);
}

static void appenditem(struct item *item, struct item **list, struct item **last);
static void calcoffsets(void);
static void cleanup(void);
static char * cistrstr(const char *s, const char *sub);
static int drawitem(struct item *item, int x, int y, int w);
static void drawmenu(void);
static void grabfocus(void);
static void grabkeyboard(void);
static void match(void);
static void insert(const char *str, ssize_t n);
static size_t nextrune(int inc);
static void movewordedge(int dir);
static void keypress(XKeyEvent *ev);
static void paste(void);
static void xinitvisual(void);
static void readstdin(void);
static void run(void);
static void setup(void);
static void usage(void);

#include "patch/include.c"

static void
appenditem(struct item *item, struct item **list, struct item **last)
{
	if (*last)
		(*last)->right = item;
	else
		*list = item;

	item->left = *last;
	item->right = NULL;
	*last = item;
}

static void
calcoffsets(void)
{
	int i, n, rpad = 0;

	if (lines > 0) {
		if (columns)
			n = lines * columns * bh;
		else
			n = lines * bh;
	} else {
		rpad = TEXTW(numbers);
		n = mw - (promptw + inputw + TEXTW("<") + TEXTW(">") + rpad);
	}
	/* calculate which items will begin the next page and previous page */
	for (i = 0, next = curr; next; next = next->right)
		if ((i += (lines > 0) ? bh : textw_clamp(next->text, n)) > n)
			break;
	for (i = 0, prev = curr; prev && prev->left; prev = prev->left)
		if ((i += (lines > 0) ? bh : textw_clamp(prev->left->text, n)) > n)
			break;
}

static void
cleanup(void)
{
	size_t i;

	XUngrabKey(dpy, AnyKey, AnyModifier, root);
	for (i = 0; i < SchemeLast; i++)
		free(scheme[i]);
	for (i = 0; items && items[i].text; ++i)
		free(items[i].text);
	free(items);
	for (i = 0; i < hplength; ++i)
		free(hpitems[i]);
	free(hpitems);
	drw_free(drw);
	XSync(dpy, False);
	XCloseDisplay(dpy);
	free(selid);
}

static char *
cistrstr(const char *s, const char *sub)
{
	size_t len;

	for (len = strlen(sub); *s; s++)
		if (!strncasecmp(s, sub, len))
			return (char *)s;
	return NULL;
}

static int
drawitem(struct item *item, int x, int y, int w)
{
	int r;
	char *text = item->text;

	int iscomment = 0;
	if (text[0] == '>') {
		if (text[1] == '>') {
			iscomment = 3;
			switch (text[2]) {
			case 'r':
				drw_setscheme(drw, scheme[SchemeRed]);
				break;
			case 'g':
				drw_setscheme(drw, scheme[SchemeGreen]);
				break;
			case 'y':
				drw_setscheme(drw, scheme[SchemeYellow]);
				break;
			case 'b':
				drw_setscheme(drw, scheme[SchemeBlue]);
				break;
			case 'p':
				drw_setscheme(drw, scheme[SchemePurple]);
				break;
			case 'h':
				drw_setscheme(drw, scheme[SchemeNormHighlight]);
				break;
			case 's':
				drw_setscheme(drw, scheme[SchemeSel]);
				break;
			default:
				iscomment = 1;
				drw_setscheme(drw, scheme[SchemeNorm]);
			break;
			}
		} else {
			drw_setscheme(drw, scheme[SchemeNorm]);
			iscomment = 1;
		}
	} else if (text[0] == ':') {
		iscomment = 2;
		if (item == sel) {
			switch (text[1]) {
			case 'r':
				drw_setscheme(drw, scheme[SchemeRed]);
				break;
			case 'g':
				drw_setscheme(drw, scheme[SchemeGreen]);
				break;
			case 'y':
				drw_setscheme(drw, scheme[SchemeYellow]);
				break;
			case 'b':
				drw_setscheme(drw, scheme[SchemeBlue]);
				break;
			case 'p':
				drw_setscheme(drw, scheme[SchemePurple]);
				break;
			case 'h':
				drw_setscheme(drw, scheme[SchemeNormHighlight]);
				break;
			case 's':
				drw_setscheme(drw, scheme[SchemeSel]);
				break;
			default:
				drw_setscheme(drw, scheme[SchemeSel]);
				iscomment = 0;
				break;
			}
		} else {
			drw_setscheme(drw, scheme[SchemeNorm]);
		}
	}

	int temppadding = 0;
	if (iscomment == 2) {
		if (text[2] == ' ') {
			temppadding = drw->fonts->h * 3;
			animated = 1;
			char dest[1000];
			strcpy(dest, text);
			dest[6] = '\0';
			drw_text(drw, x, y
				, temppadding
				, MAX(lineheight, bh)
				, temppadding / 2.6
				, dest + 3
				, 0
			);
			iscomment = 6;
			drw_setscheme(drw, sel == item ? scheme[SchemeHover] : scheme[SchemeNorm]);
		}
	}

	char *output;
	if (commented) {
		static char onestr[2];
		onestr[0] = text[0];
		onestr[1] = '\0';
		output = onestr;
	} else {
		output = text;
	}

	if (item == sel)
		drw_setscheme(drw, scheme[SchemeSel]);
	else if (item->hp)
		drw_setscheme(drw, scheme[SchemeHp]);
	else if (issel(item->id))
		drw_setscheme(drw, scheme[SchemeOut]);
	else
		drw_setscheme(drw, scheme[SchemeNorm]);

	r = drw_text(drw
		, x + ((iscomment == 6) ? temppadding : 0)
		, y
		, w
		, bh
		, commented ? (bh - TEXTW(output) - lrpad) / 2 : lrpad / 2
		, output + iscomment
		, 0
		);
	drawhighlights(item, output + iscomment, x + ((iscomment == 6) ? temppadding : 0), y, w);
	return r;
}

static void
drawmenu(void)
{
	static int curpos, oldcurlen;
	int curlen, rcurlen;
	struct item *item;
	int x = 0, y = 0, w, rpad = 0, itw = 0, stw = 0;
	int fh = drw->fonts->h;
	char *censort;

	drw_setscheme(drw, scheme[SchemeNorm]);
	drw_rect(drw, 0, 0, mw, mh, 1, 1);

	if (prompt && *prompt) {
		x = drw_text(drw, x, 0, promptw, bh, lrpad / 2, prompt, 0
		);
	}
	/* draw input field */
	w = (lines > 0 || !matches) ? mw - x : inputw;

	w -= lrpad / 2;
	x += lrpad / 2;
	rcurlen = TEXTW(text + cursor) - lrpad;
	curlen = TEXTW(text) - lrpad - rcurlen;
	curpos += curlen - oldcurlen;
	curpos = MIN(w, MAX(0, curpos));
	curpos = MAX(curpos, w - rcurlen);
	curpos = MIN(curpos, curlen);
	oldcurlen = curlen;

	drw_setscheme(drw, scheme[SchemeNorm]);
	if (passwd) {
		censort = ecalloc(1, sizeof(text));
		memset(censort, '.', strlen(text));
		drw_text_align(drw, x, 0, curpos, bh, censort, cursor, AlignR);
		drw_text_align(drw, x + curpos, 0, w - curpos, bh, censort + cursor, strlen(censort) - cursor, AlignL);
		free(censort);
	} else {
		drw_text_align(drw, x, 0, curpos, bh, text, cursor, AlignR);
		drw_text_align(drw, x + curpos, 0, w - curpos, bh, text + cursor, strlen(text) - cursor, AlignL);
	}
	drw_rect(drw, x + curpos - 1, 2 + (bh-fh)/2, 2, fh - 4, 1, 0);

	recalculatenumbers();
	rpad = TEXTW(numbers);
	rpad += 2 * sp;
	rpad += border_width;
	if (lines > 0) {
		/* draw grid */
		int i = 0;
		for (item = curr; item != next; item = item->right, i++)
			if (columns)
				drawitem(
					item,
					0 + ((i / lines) *  (mw / columns)),
					y + (((i % lines) + 1) * bh),
					mw / columns
				);
			else
				drawitem(item, 0, y += bh, mw);
	} else if (matches) {
		/* draw horizontal list */
		x += inputw;
		w = TEXTW("<");
		if (curr->left) {
			drw_setscheme(drw, scheme[SchemeNorm]);
			drw_text(drw, x, 0, w, bh, lrpad / 2, "<", 0
			);
		}
		x += w;
		for (item = curr; item != next; item = item->right) {
			stw = TEXTW(">");
			itw = textw_clamp(item->text, mw - x - stw - rpad);
			x = drawitem(item, x, 0, itw);
		}
		if (next) {
			w = TEXTW(">");
			drw_setscheme(drw, scheme[SchemeNorm]);
			drw_text(drw, mw - w - rpad, 0, w, bh, lrpad / 2
				, ">"
				, 0
			);
		}
	}
	drw_setscheme(drw, scheme[SchemeNorm]);
	drw_text(drw, mw - rpad, 0, TEXTW(numbers), bh, lrpad / 2, numbers, 0);
	drw_map(drw, win, 0, 0, mw, mh);
}

static void
grabfocus(void)
{
	struct timespec ts = { .tv_sec = 0, .tv_nsec = 10000000  };
	Window focuswin;
	int i, revertwin;

	for (i = 0; i < 100; ++i) {
		XGetInputFocus(dpy, &focuswin, &revertwin);
		if (focuswin == win)
			return;
		nanosleep(&ts, NULL);
	}
	die("cannot grab focus");
}

static void
grabkeyboard(void)
{
	struct timespec ts = { .tv_sec = 0, .tv_nsec = 1000000  };
	int i;

	if (embed || managed)
		return;
	/* try to grab keyboard, we may have to wait for another process to ungrab */
	for (i = 0; i < 1000; i++) {
		if (XGrabKeyboard(dpy, DefaultRootWindow(dpy), True, GrabModeAsync,
		                  GrabModeAsync, CurrentTime) == GrabSuccess)
			return;
		nanosleep(&ts, NULL);
	}
	die("cannot grab keyboard");
}

static void
match(void)
{
	if (dynamic && *dynamic)
		refreshoptions();

	if (fuzzy) {
		fuzzymatch();
		return;
	}
	static char **tokv = NULL;
	static int tokn = 0;

	char buf[sizeof text], *s;
	int i, tokc = 0;
	size_t len, textsize;
	struct item *item, *lprefix, *lsubstr, *prefixend, *substrend;
	struct item *lhpprefix, *hpprefixend;

	strcpy(buf, text);
	/* separate input text into tokens to be matched individually */
	for (s = strtok(buf, " "); s; tokv[tokc - 1] = s, s = strtok(NULL, " "))
		if (++tokc > tokn && !(tokv = realloc(tokv, ++tokn * sizeof *tokv)))
			die("cannot realloc %zu bytes:", tokn * sizeof *tokv);
	len = tokc ? strlen(tokv[0]) : 0;

	if (use_prefix) {
		matches = lprefix = matchend = prefixend = NULL;
		textsize = strlen(text);
	} else {
		matches = lprefix = lsubstr = matchend = prefixend = substrend = NULL;
		textsize = strlen(text) + 1;
	}
	lhpprefix = hpprefixend = NULL;
	for (item = items; item && item->text; item++)
	{
		for (i = 0; i < tokc; i++)
			if (!fstrstr(item->text, tokv[i]))
				break;
		if (i != tokc && !(dynamic && *dynamic)) /* not all tokens match */
			continue;
		/* exact matches go first, then prefixes with high priority, then prefixes, then substrings */
		if (!sortmatches)
 			appenditem(item, &matches, &matchend);
 		else
		if (!tokc || !fstrncmp(text, item->text, textsize))
			appenditem(item, &matches, &matchend);
		else if (item->hp && !fstrncmp(tokv[0], item->text, len))
			appenditem(item, &lhpprefix, &hpprefixend);
		else if (!fstrncmp(tokv[0], item->text, len))
			appenditem(item, &lprefix, &prefixend);
		else if (!use_prefix)
			appenditem(item, &lsubstr, &substrend);
	}
	if (lhpprefix) {
		if (matches) {
			matchend->right = lhpprefix;
			lhpprefix->left = matchend;
		} else
			matches = lhpprefix;
		matchend = hpprefixend;
	}
	if (lprefix) {
		if (matches) {
			matchend->right = lprefix;
			lprefix->left = matchend;
		} else
			matches = lprefix;
		matchend = prefixend;
	}
	if (!use_prefix && lsubstr)
	{
		if (matches) {
			matchend->right = lsubstr;
			lsubstr->left = matchend;
		} else
			matches = lsubstr;
		matchend = substrend;
	}
	curr = sel = matches;

	if (instant && matches && matches==matchend && !lsubstr) {
		puts(matches->text);
		cleanup();
		exit(0);
	}

	calcoffsets();
}

static void
insert(const char *str, ssize_t n)
{
	if (strlen(text) + n > sizeof text - 1)
		return;


	/* move existing text out of the way, insert new text, and update cursor */
	memmove(&text[cursor + n], &text[cursor], sizeof text - cursor - MAX(n, 0));
	if (n > 0)
		memcpy(&text[cursor], str, n);
	cursor += n;
	match();

}

static size_t
nextrune(int inc)
{
	ssize_t n;

	/* return location of next utf8 rune in the given direction (+1 or -1) */
	for (n = cursor + inc; n + inc >= 0 && (text[n] & 0xc0) == 0x80; n += inc)
		;
	return n;
}

static void
movewordedge(int dir)
{
	if (dir < 0) { /* move cursor to the start of the word*/
		while (cursor > 0 && strchr(worddelimiters, text[nextrune(-1)]))
			cursor = nextrune(-1);
		while (cursor > 0 && !strchr(worddelimiters, text[nextrune(-1)]))
			cursor = nextrune(-1);
	} else { /* move cursor to the end of the word */
		while (text[cursor] && strchr(worddelimiters, text[cursor]))
			cursor = nextrune(+1);
		while (text[cursor] && !strchr(worddelimiters, text[cursor]))
			cursor = nextrune(+1);
	}
}

static void
keypress(XKeyEvent *ev)
{
	char buf[64];
	int len;
	struct item * item;
	KeySym ksym = NoSymbol;
	Status status;
	int i;
	struct item *tmpsel;
	bool offscreen = false;

	len = XmbLookupString(xic, ev, buf, sizeof buf, &ksym, &status);
	switch (status) {
	default: /* XLookupNone, XBufferOverflow */
		return;
	case XLookupChars: /* composed string from input method */
		goto insert;
	case XLookupKeySym:
	case XLookupBoth: /* a KeySym and a string are returned: use keysym */
		break;
	}

	if (ev->state & ControlMask) {
		switch(ksym) {
		case XK_a: expect("ctrl-a", ev); ksym = XK_Home;      break;
		case XK_b: expect("ctrl-b", ev); ksym = XK_Left;      break;
		case XK_c: expect("ctrl-c", ev); ksym = XK_Escape;    break;
		case XK_d: expect("ctrl-d", ev); ksym = XK_Delete;    break;
		case XK_e: expect("ctrl-e", ev); ksym = XK_End;       break;
		case XK_f: expect("ctrl-f", ev); ksym = XK_Right;     break;
		case XK_g: expect("ctrl-g", ev); ksym = XK_Escape;    break;
		case XK_h: expect("ctrl-h", ev); ksym = XK_BackSpace; break;
		case XK_i: expect("ctrl-i", ev); ksym = XK_Tab;       break;
		case XK_j: expect("ctrl-j", ev); ksym = XK_Down;      break;
		case XK_J:/* fallthrough */
		case XK_l: expect("ctrl-l", ev); break;
		case XK_m: expect("ctrl-m", ev); /* fallthrough */
		case XK_M: ksym = XK_Return; ev->state &= ~ControlMask; break;
		case XK_n: expect("ctrl-n", ev); ksym = XK_Down; break;
		case XK_p: expect("ctrl-p", ev); ksym = XK_Up;   break;
		case XK_o: expect("ctrl-o", ev); break;
		case XK_q: expect("ctrl-q", ev); break;
		case XK_r: expect("ctrl-r", ev); break;
		case XK_s: expect("ctrl-s", ev); break;
		case XK_t: expect("ctrl-t", ev); break;
		case XK_k: expect("ctrl-k", ev); ksym = XK_Up; break;
		case XK_u: expect("ctrl-u", ev); /* delete left */
			insert(NULL, 0 - cursor);
			break;
		case XK_w: expect("ctrl-w", ev); /* delete word */
			while (cursor > 0 && strchr(worddelimiters, text[nextrune(-1)]))
				insert(NULL, nextrune(-1) - cursor);
			while (cursor > 0 && !strchr(worddelimiters, text[nextrune(-1)]))
				insert(NULL, nextrune(-1) - cursor);
			break;
		case XK_v:
			expect("ctrl-v", ev);
		case XK_V:
			XConvertSelection(dpy, (ev->state & ShiftMask) ? clip : XA_PRIMARY,
			                  utf8, utf8, win, CurrentTime);
			return;
		case XK_y: expect("ctrl-y", ev); /* paste selection */
		case XK_Y:
			XConvertSelection(dpy, (ev->state & ShiftMask) ? clip : XA_PRIMARY,
			                  utf8, utf8, win, CurrentTime);
			return;
		case XK_x: expect("ctrl-x", ev); break;
		case XK_z: expect("ctrl-z", ev); break;
		case XK_Left:
		case XK_KP_Left:
			movewordedge(-1);
			goto draw;
		case XK_Right:
		case XK_KP_Right:
			movewordedge(+1);
			goto draw;
		case XK_Return:
		case XK_KP_Enter:
			selsel();
			break;
		case XK_bracketleft:
			cleanup();
			exit(1);
		default:
			return;
		}
	} else if (ev->state & Mod1Mask) {
		switch(ksym) {
		case XK_b:
			movewordedge(-1);
			goto draw;
		case XK_f:
			movewordedge(+1);
			goto draw;
		case XK_g: ksym = XK_Home;  break;
		case XK_G: ksym = XK_End;   break;
		case XK_h: ksym = XK_Up;    break;
		case XK_j: ksym = XK_Next;  break;
		case XK_k: ksym = XK_Prior; break;
		case XK_l: ksym = XK_Down;  break;
		case XK_p:
			navhistory(-1);
			buf[0]=0;
			break;
		case XK_n:
			navhistory(1);
			buf[0]=0;
			break;
		default:
			return;
		}
	}

	switch(ksym) {
	default:
insert:
		if (!iscntrl((unsigned char)*buf))
			insert(buf, len);
		break;
	case XK_Delete:
	case XK_KP_Delete:
		if (text[cursor] == '\0')
			return;
		cursor = nextrune(+1);
		/* fallthrough */
	case XK_BackSpace:
		if (cursor == 0)
			return;
		insert(NULL, nextrune(-1) - cursor);
		break;
	case XK_End:
	case XK_KP_End:
		if (text[cursor] != '\0') {
			cursor = strlen(text);
			break;
		}
		if (next) {
			/* jump to end of list and position items in reverse */
			curr = matchend;
			calcoffsets();
			curr = prev;
			calcoffsets();
			while (next && (curr = curr->right))
				calcoffsets();
		}
		sel = matchend;
		break;
	case XK_Escape:
		cleanup();
		exit(1);
	case XK_Home:
	case XK_KP_Home:
		if (sel == matches) {
			cursor = 0;
			break;
		}
		sel = curr = matches;
		calcoffsets();
		break;
	case XK_Left:
	case XK_KP_Left:
		if (columns > 1) {
			if (!sel)
				return;
			tmpsel = sel;
			for (i = 0; i < lines; i++) {
				if (!tmpsel->left ||  tmpsel->left->right != tmpsel)
					return;
				if (tmpsel == curr)
					offscreen = true;
				tmpsel = tmpsel->left;
			}
			sel = tmpsel;
			if (offscreen) {
				curr = prev;
				calcoffsets();
			}
			break;
		}
		if (cursor > 0 && (!sel || !sel->left || lines > 0)) {
			cursor = nextrune(-1);
			break;
		}
		if (lines > 0)
			return;
		/* fallthrough */
	case XK_Up:
	case XK_KP_Up:
		if (sel && sel->left && (sel = sel->left)->right == curr) {
			curr = prev;
			calcoffsets();
		}
		break;
	case XK_Next:
	case XK_KP_Next:
		if (!next)
			return;
		sel = curr = next;
		calcoffsets();
		break;
	case XK_Prior:
	case XK_KP_Prior:
		if (!prev)
			return;
		sel = curr = prev;
		calcoffsets();
		break;
	case XK_Return:
	case XK_KP_Enter:
		if (!(ev->state & ControlMask)) {
			savehistory((sel && !(ev->state & ShiftMask))
				    ? sel->text : text);
			printsel(ev->state);
			cleanup();
			exit(0);
		}
		break;
	case XK_Right:
	case XK_KP_Right:
		if (columns > 1) {
			if (!sel)
				return;
			tmpsel = sel;
			for (i = 0; i < lines; i++) {
				if (!tmpsel->right ||  tmpsel->right->left != tmpsel)
					return;
				tmpsel = tmpsel->right;
				if (tmpsel == next)
					offscreen = true;
			}
			sel = tmpsel;
			if (offscreen) {
				curr = next;
				calcoffsets();
			}
			break;
		}
		if (text[cursor] != '\0') {
			cursor = nextrune(+1);
			break;
		}
		if (lines > 0)
			return;
		/* fallthrough */
	case XK_Down:
	case XK_KP_Down:
		if (sel && sel->right && (sel = sel->right) == next) {
			curr = next;
			calcoffsets();
		}
		break;
	case XK_Tab:
		if (!matches)
			break; /* cannot complete no matches */
		/* only do tab completion if all matches start with prefix */
		for (item = matches; item && item->text; item = item->right)
			if (item->text[0] != text[0])
				goto draw;
		strncpy(text, matches->text, sizeof text - 1);
		text[sizeof text - 1] = '\0';
		len = cursor = strlen(text); /* length of longest common prefix */
		for (item = matches; item && item->text; item = item->right) {
			cursor = 0;
			while (cursor < len && text[cursor] == item->text[cursor])
				cursor++;
			len = cursor;
		}
		memset(text + len, '\0', strlen(text) - len);
		break;
	}

draw:
	drawmenu();
}

static void
paste(void)
{
	char *p, *q;
	int di;
	unsigned long dl;
	Atom da;

	/* we have been given the current selection, now insert it into input */
	if (XGetWindowProperty(dpy, win, utf8, 0, (sizeof text / 4) + 1, False,
	                   utf8, &da, &di, &dl, &dl, (unsigned char **)&p)
	    == Success && p) {
		insert(p, (q = strchr(p, '\n')) ? q - p : (ssize_t)strlen(p));
		XFree(p);
	}
	drawmenu();
}

static void
xinitvisual()
{
	XVisualInfo *infos;
	XRenderPictFormat *fmt;
	int nitems;
	int i;

	XVisualInfo tpl = {
		.screen = screen,
		.depth = 32,
		.class = TrueColor
	};
	long masks = VisualScreenMask | VisualDepthMask | VisualClassMask;

	infos = XGetVisualInfo(dpy, masks, &tpl, &nitems);
	visual = NULL;
	for(i = 0; i < nitems; i ++) {
		fmt = XRenderFindVisualFormat(dpy, infos[i].visual);
		if (fmt->type == PictTypeDirect && fmt->direct.alphaMask) {
			visual = infos[i].visual;
			depth = infos[i].depth;
			cmap = XCreateColormap(dpy, root, visual, AllocNone);
			useargb = 1;
			break;
		}
	}

	XFree(infos);

	if (!visual || !opacity) {
		visual = DefaultVisual(dpy, screen);
		depth = DefaultDepth(dpy, screen);
		cmap = DefaultColormap(dpy, screen);
	}
}

static void
readstdin(void)
{
	char *line = NULL;
	char *p;

	size_t size = 0;
	size_t i, junk;
	ssize_t len;

	if (passwd) {
		inputw = lines = 0;
		return;
	}

	/* read each line from stdin and add it to the item list */
	for (i = 0; (len = getline(&line, &junk, stdin)) != -1; i++, line = NULL) {
		if (i + 1 >= size / sizeof *items)
			if (!(items = realloc(items, (size += BUFSIZ))))
				die("cannot realloc %zu bytes:", size);
		if (line[len - 1] == '\n')
			line[len - 1] = '\0';

		items[i].text = line;
		if (separator && (p = separator_greedy ?
			strrchr(items[i].text, separator) : strchr(items[i].text, separator))) {
			*p = '\0';
			items[i].text_output = ++p;
		} else {
			items[i].text_output = items[i].text;
		}
		if (separator_reverse) {
			p = items[i].text;
			items[i].text = items[i].text_output;
			items[i].text_output = p;
		}
		items[i].id = i; /* for multiselect */
		items[i].index = i;

		items[i].hp = arrayhas(hpitems, hplength, items[i].text);
	}
	if (items)
		items[i].text = NULL;
	lines = MIN(lines, i);
}

static void
run(void)
{
	XEvent ev;
	int i;

	while (!XNextEvent(dpy, &ev)) {
		if (preselected) {
			for (i = 0; i < preselected; i++) {
				if (sel && sel->right && (sel = sel->right) == next) {
					curr = next;
					calcoffsets();
				}
			}
			drawmenu();
			preselected = 0;
		}
		if (XFilterEvent(&ev, win))
			continue;
		switch(ev.type) {
		case ButtonPress:
			buttonpress(&ev);
			break;
		case DestroyNotify:
			if (ev.xdestroywindow.window != win)
				break;
			cleanup();
			exit(1);
		case Expose:
			if (ev.xexpose.count == 0)
				drw_map(drw, win, 0, 0, mw, mh);
			break;
		case FocusIn:
			/* regrab focus from parent window */
			if (ev.xfocus.window != win)
				grabfocus();
			break;
		case KeyPress:
			keypress(&ev.xkey);
			break;
		case SelectionNotify:
			if (ev.xselection.property == utf8)
				paste();
			break;
		case VisibilityNotify:
			if (ev.xvisibility.state != VisibilityUnobscured)
				XRaiseWindow(dpy, win);
			break;
		}
	}
}

static void
setup(void)
{
	int x, y, i, j;
	unsigned int du;
	XSetWindowAttributes swa;
	XIM xim;
	Window w, dw, *dws;
	XWindowAttributes wa;
	XClassHint ch = {"dmenu", "dmenu"};
#ifdef XINERAMA
	XineramaScreenInfo *info;
	Window pw;
	int a, di, n, area = 0;
#endif
	/* init appearance */
	for (j = 0; j < SchemeLast; j++)
		scheme[j] = drw_scm_create(drw, (const char**)colors[j], alphas[j], 2);

	clip = XInternAtom(dpy, "CLIPBOARD",   False);
	utf8 = XInternAtom(dpy, "UTF8_STRING", False);
	type = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE", False);
	dock = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_DOCK", False);

	/* calculate menu geometry */
	bh = drw->fonts->h + 2;
	bh = MAX(bh,lineheight);	/* make a menu line AT LEAST 'lineheight' tall */
	lines = MAX(lines, 0);
	mh = (lines + 1) * bh;
	promptw = (prompt && *prompt) ? TEXTW(prompt) - lrpad / 4 : 0;
#ifdef XINERAMA
	i = 0;
	if (parentwin == root && (info = XineramaQueryScreens(dpy, &n))) {
		XGetInputFocus(dpy, &w, &di);
		if (mon >= 0 && mon < n)
			i = mon;
		else if (w != root && w != PointerRoot && w != None) {
			/* find top-level window containing current input focus */
			do {
				if (XQueryTree(dpy, (pw = w), &dw, &w, &dws, &du) && dws)
					XFree(dws);
			} while (w != root && w != pw);
			/* find xinerama screen with which the window intersects most */
			if (XGetWindowAttributes(dpy, pw, &wa))
				for (j = 0; j < n; j++)
					if ((a = INTERSECT(wa.x, wa.y, wa.width, wa.height, info[j])) > area) {
						area = a;
						i = j;
					}
		}
		/* no focused window is on screen, so use pointer location instead */
		if (mon < 0 && !area && XQueryPointer(dpy, root, &dw, &dw, &x, &y, &di, &di, &du))
			for (i = 0; i < n; i++)
				if (INTERSECT(x, y, 1, 1, info[i]) != 0)
					break;

		if (center) {
			mw = MIN(MAX(max_textw() + promptw, min_width), info[i].width);
			x = info[i].x_org + ((info[i].width  - mw) / 2);
			y = info[i].y_org + ((info[i].height - mh) / 2);
		} else {
			x = info[i].x_org + dmx;
			y = info[i].y_org + (topbar ? dmy : info[i].height - mh - dmy);
			mw = (dmw>0 ? dmw : info[i].width);
		}
		XFree(info);
	} else
#endif
	{
		if (!XGetWindowAttributes(dpy, parentwin, &wa))
			die("could not get embedding window attributes: 0x%lx",
			    parentwin);
		if (center) {
			mw = MIN(MAX(max_textw() + promptw, min_width), wa.width);
			x = (wa.width  - mw) / 2;
			y = (wa.height - mh) / 2;
		} else {
			x = dmx;
			y = topbar ? dmy : wa.height - mh - dmy;
			mw = (dmw>0 ? dmw : wa.width);
		}
	}
	inputw = mw / 3; /* input width: ~33.33% of monitor width */
	match();

	/* create menu window */
	swa.override_redirect = managed ? False : True;
	swa.background_pixel = 0;
	swa.colormap = cmap;
	swa.event_mask = ExposureMask | KeyPressMask | VisibilityChangeMask
		| ButtonPressMask
	;
	win = XCreateWindow(
		dpy, parentwin,
		x + sp, y + vp - (topbar ? 0 : border_width * 2), mw - 2 * sp - border_width * 2, mh, border_width,
		depth, InputOutput, visual,
		CWOverrideRedirect|CWBackPixel|CWBorderPixel|CWColormap|CWEventMask, &swa
	);
	if (border_width)
		XSetWindowBorder(dpy, win, scheme[SchemeBorder][ColBg].pixel);
	XSetClassHint(dpy, win, &ch);
	XChangeProperty(dpy, win, type, XA_ATOM, 32, PropModeReplace,
			(unsigned char *) &dock, 1);


	/* input methods */
	if ((xim = XOpenIM(dpy, NULL, NULL, NULL)) == NULL)
		die("XOpenIM failed: could not open input device");

	xic = XCreateIC(xim, XNInputStyle, XIMPreeditNothing | XIMStatusNothing,
	                XNClientWindow, win, XNFocusWindow, win, NULL);

	if (managed) {
		XTextProperty prop;
		char *windowtitle = prompt != NULL ? prompt : "dmenu";
		Xutf8TextListToTextProperty(dpy, &windowtitle, 1, XUTF8StringStyle, &prop);
		XSetWMName(dpy, win, &prop);
		XSetTextProperty(dpy, win, &prop, XInternAtom(dpy, "_NET_WM_NAME", False));
		XFree(prop.value);
	}

	XMapRaised(dpy, win);
	if (embed) {
		XSelectInput(dpy, parentwin, FocusChangeMask | SubstructureNotifyMask);
		if (XQueryTree(dpy, parentwin, &dw, &w, &dws, &du) && dws) {
			for (i = 0; i < du && dws[i] != win; ++i)
				XSelectInput(dpy, dws[i], FocusChangeMask);
			XFree(dws);
		}
		grabfocus();
	}
	drw_resize(drw, mw, mh);
	drawmenu();
}

static void
usage(void)
{
	die("usage: dmenu [-bv"
		"c"
		"f"
		"s"
		"n"
		"x"
		"F"
		"P"
		"S"
		"] "
		"[-wm] "
		"[-g columns] "
		"[-l lines] [-p prompt] [-fn font] [-m monitor]"
		"\n             [-nb color] [-nf color] [-sb color] [-sf color] [-w windowid]"
		"\n            "
		" [-dy command]"
		" [-ex expectkey]"
		" [-o opacity]"
		" [-bw width]"
		" [-hb color] [-hf color] [-hp items]"
		"\n            "
		" [-it text]"
		" [-h height]"
		" [-ps index]"
		" [-H histfile]"
		" [-X xoffset] [-Y yoffset] [-W width]" // (arguments made upper case due to conflicts)
		"\n             [-nhb color] [-nhf color] [-shb color] [-shf color]" // highlight colors
		"\n             [-d separator] [-D separator]"
		"\n");
}

int
main(int argc, char *argv[])
{
	XWindowAttributes wa;
	int i;
	int fast = 0;

	if (!setlocale(LC_CTYPE, "") || !XSupportsLocale())
		fputs("warning: no locale support\n", stderr);
	if (!(dpy = XOpenDisplay(NULL)))
		die("cannot open display");
	screen = DefaultScreen(dpy);
	root = RootWindow(dpy, screen);
	if (!embed || !(parentwin = strtol(embed, NULL, 0)))
		parentwin = root;
	if (!XGetWindowAttributes(dpy, parentwin, &wa))
		die("could not get embedding window attributes: 0x%lx",
		    parentwin);

	xinitvisual();
	drw = drw_create(dpy, screen, root, wa.width, wa.height, visual, depth, cmap);
	readxresources();

	for (i = 1; i < argc; i++)
		/* these options take no arguments */
		if (!strcmp(argv[i], "-v")) {      /* prints version information */
			puts("dmenu-"VERSION);
			exit(0);
		} else if (!strcmp(argv[i], "-b")) { /* appears at the bottom of the screen */
			topbar = 0;
		} else if (!strcmp(argv[i], "-c")) { /* toggles centering of dmenu window on screen */
			center = !center;
		} else if (!strcmp(argv[i], "-f")) { /* grabs keyboard before reading stdin */
			fast = 1;
		} else if (!strcmp(argv[i], "-s")) { /* case-sensitive item matching */
			fstrncmp = strncmp;
			fstrstr = strstr;
		} else if (!strcmp(argv[i], "-wm")) { /* display as managed wm window */
			managed = 1;
		} else if (!strcmp(argv[i], "-n")) { /* instant select only match */
			instant = !instant;
		} else if (!strcmp(argv[i], "-x")) { /* invert use_prefix */
			use_prefix = !use_prefix;
		} else if (!strcmp(argv[i], "-F")) { /* disable/enable fuzzy matching, depends on default */
			fuzzy = !fuzzy;
		} else if (!strcmp(argv[i], "-P")) { /* is the input a password */
			passwd = 1;
		} else if (!strcmp(argv[i], "-ex")) { /* expect key */
			expected = argv[++i];
		} else if (!strcmp(argv[i], "-S")) { /* do not sort matches */
			sortmatches = 0;
		} else if (!strcmp(argv[i], "-ix")) { /* adds ability to return index in list */
			print_index = 1;
		} else if (i + 1 == argc)
			usage();
		/* these options take one argument */
		else if (!strcmp(argv[i], "-H"))
			histfile = argv[++i];
		else if (!strcmp(argv[i], "-g")) {   /* number of columns in grid */
			columns = atoi(argv[++i]);
			if (columns && lines == 0)
				lines = 1;
		}
		else if (!strcmp(argv[i], "-l"))   /* number of lines in vertical list */
			lines = atoi(argv[++i]);
		else if (!strcmp(argv[i], "-X"))   /* window x offset */
			dmx = atoi(argv[++i]);
		else if (!strcmp(argv[i], "-Y"))   /* window y offset (from bottom up if -b) */
			dmy = atoi(argv[++i]);
		else if (!strcmp(argv[i], "-W"))   /* make dmenu this wide */
			dmw = atoi(argv[++i]);
		else if (!strcmp(argv[i], "-m"))
			mon = atoi(argv[++i]);
		else if (!strcmp(argv[i], "-o"))  /* opacity, pass -o 0 to disable alpha */
			opacity = atoi(argv[++i]);
		else if (!strcmp(argv[i], "-p"))   /* adds prompt to left of input field */
			prompt = argv[++i];
		else if (!strcmp(argv[i], "-fn"))  /* font or font set */
			fonts[0] = argv[++i];
		else if(!strcmp(argv[i], "-h")) { /* minimum height of one menu line */
			lineheight = atoi(argv[++i]);
			lineheight = MAX(lineheight, min_lineheight); /* reasonable default in case of value too small/negative */
		}
		else if (!strcmp(argv[i], "-nb"))  /* normal background color */
			colors[SchemeNorm][ColBg] = argv[++i];
		else if (!strcmp(argv[i], "-nf"))  /* normal foreground color */
			colors[SchemeNorm][ColFg] = argv[++i];
		else if (!strcmp(argv[i], "-sb"))  /* selected background color */
			colors[SchemeSel][ColBg] = argv[++i];
		else if (!strcmp(argv[i], "-sf"))  /* selected foreground color */
			colors[SchemeSel][ColFg] = argv[++i];
		else if (!strcmp(argv[i], "-hb"))  /* high priority background color */
			colors[SchemeHp][ColBg] = argv[++i];
		else if (!strcmp(argv[i], "-hf")) /* low priority background color */
			colors[SchemeHp][ColFg] = argv[++i];
		else if (!strcmp(argv[i], "-hp"))
			hpitems = tokenize(argv[++i], ",", &hplength);
		else if (!strcmp(argv[i], "-nhb")) /* normal hi background color */
			colors[SchemeNormHighlight][ColBg] = argv[++i];
		else if (!strcmp(argv[i], "-nhf")) /* normal hi foreground color */
			colors[SchemeNormHighlight][ColFg] = argv[++i];
		else if (!strcmp(argv[i], "-shb")) /* selected hi background color */
			colors[SchemeSelHighlight][ColBg] = argv[++i];
		else if (!strcmp(argv[i], "-shf")) /* selected hi foreground color */
			colors[SchemeSelHighlight][ColFg] = argv[++i];
		else if (!strcmp(argv[i], "-w"))   /* embedding window id */
			embed = argv[++i];
		else if (!strcmp(argv[i], "-d") || /* field separator */
				(separator_greedy = !strcmp(argv[i], "-D"))) {
			separator = argv[++i][0];
			separator_reverse = argv[i][1] == '|';
		}
		else if (!strcmp(argv[i], "-ps"))   /* preselected item */
			preselected = atoi(argv[++i]);
		else if (!strcmp(argv[i], "-dy"))  /* dynamic command to run */
			dynamic = argv[++i];
		else if (!strcmp(argv[i], "-bw"))  /* border width around dmenu */
			border_width = atoi(argv[++i]);
		else if (!strcmp(argv[i], "-it")) {   /* adds initial text */
			const char * text = argv[++i];
			insert(text, strlen(text));
		}
		else
			usage();

	if (!drw_fontset_create(drw, (const char**)fonts, LENGTH(fonts)))
		die("no fonts could be loaded.");

	lrpad = drw->fonts->h;

	sp = sidepad;
	vp = (topbar ? vertpad : - vertpad);

	if (lineheight == -1)
		lineheight = drw->fonts->h * 2.5;

#ifdef __OpenBSD__
	if (pledge("stdio rpath", NULL) == -1)
		die("pledge");
#endif
	loadhistory();

	if (fast && !isatty(0)) {
		grabkeyboard();
		if (!(dynamic && *dynamic))
			readstdin();
	} else {
		if (!(dynamic && *dynamic))
			readstdin();
		grabkeyboard();
	}
	setup();
	run();

	return 1; /* unreachable */
}
