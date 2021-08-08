#include <u.h>
#include <libc.h>
#include <draw.h>
#include <thread.h>
#include <mouse.h>
#include <keyboard.h>
#include <plumb.h>

typedef struct Dirtree Dirtree;

enum
{
	PATHMAX	= 8191,

	Indent	= 32,
	Padx	= 8,
	Pady	= 3,
	Lyoff	= -3,
#define Lxoff	(stringwidth(font, "⊞")/2-1)
};

enum
{
	Refresh,
	Exit,
};

enum
{
	Reshapec,
	Mousec,
	Keyboardc,
};

#pragma varargck type "本" Dirtree*
struct Dirtree
{
	char*		name;
	int			unfold;
	int			isdir;
	Dirtree*	children;
	Dirtree*	parent;
	Dirtree*	next;
};

Image*			treeback;
Image*			textcol;
Image*			linecol;
Image*			selcol;
Image*			seltextcol;
Mousectl*		mousectl;
Keyboardctl*	keyboardctl;
Dirtree*		dirtree;
char			rootpath[PATHMAX+1];
int				plumbfd;
int				unfold;
int				selitem = -1;

char *menu3str[] = {
	[Refresh]	= "Refresh",
	[Exit]		= "Exit",
	[Exit+1]	= nil,
};

#define Itemh		(2*Pady+font->height)
#define Itemy(i)	(Pady+(i)*Itemh)

int
dirtreefmt(Fmt *f)
{
	Dirtree *t;

	t = va_arg(f->args, Dirtree*);
	return t->parent != nil
		? fmtprint(f, "%本/%s", t->parent, t->name)
		: fmtprint(f, "%s", t->name);
}

int
itemat(Point xy)
{
	return (xy.y-screen->r.min.y)/Itemh;
}

int
_drawdirtree(Dirtree *T, int level, int *item)
{
	static char buf[PATHMAX+1];
	Point p, q, p₀, p₁;
	Image *col;
	int i, last;

	i = *item;

Start:
	if(T == nil)
		return i;
	p = addpt(Pt(level*Indent + Padx, Itemy(*item)), screen->r.min);
	q = p;
	if(*item == selitem){
		p₀ = Pt(screen->r.min.x, p.y-Pady);
		p₁ = Pt(screen->r.max.x, p.y+font->height+Pady);
		draw(screen, Rpt(p₀, p₁), selcol, nil, ZP);
		col = seltextcol;
	}else
		col = textcol;
	if(T->isdir){
		string(screen, p, col, ZP, font, T->unfold ? "⊟" : "⊞");
		q.x += stringwidth(font, "⊞ ");
	}
	snprint(buf, PATHMAX+1, "%s", T->name);
	string(screen, q, col, ZP, font, buf);
	if(level > 0){
		p₀ = addpt(p, Pt(-Indent+Lxoff, Itemh/2+Lyoff));
		p₁ = addpt(p₀, Pt(Indent-Padx, 0));
		line(screen, p₀, p₁, 0, 0, 0, linecol, ZP);
	}
	*item = *item+1;
	i = *item;
	if(T->unfold){
		last = _drawdirtree(T->children, level+1, item);
		if(last-i > 0){
			p₀ = addpt(p, Pt(Lxoff, Itemh+Lyoff));
			p₁ = addpt(p₀, Pt(0, Itemh*(last-i) - Itemh/2));
			line(screen, p₀, p₁, 0, 0, 0, linecol, ZP);
		}
	}
	T = T->next;
	goto Start;
}

int
drawdirtree(Dirtree *T)
{
	int item;

	item = 0;
	_drawdirtree(T, 0, &item);
	return item;
}

Dirtree*
_gendirtree(char path[], Dirtree *parent)
{
	Dirtree *T, *t, *newt;
	Dir *dirs, *f, *fe;
	char *buf;
	int fd;
	long n;

	T = nil;
	t = nil;
	buf = nil;
	fd = open(path, OREAD);
	if(fd < 0)
		sysfatal("cannot open %s: %r\n", path);
	while((n = dirread(fd, &dirs)) > 0){
		fe = dirs+n;
		for(f = dirs; f < fe; f++){
			newt = mallocz(sizeof(Dirtree), 1);
			newt->name = strdup(f->name);
			newt->parent = parent;
			if(t == nil)
				T = newt;
			else
				t->next = newt;
			t = newt;
			if(f->qid.type & QTDIR){
				if(buf == nil){
					buf = malloc(PATHMAX+1);
					buf[PATHMAX] = '\0';
				}
				if(snprint(buf, PATHMAX, "%s/%s", path, f->name) < 0)
					sysfatal("cannot generate path: %r");
				newt->isdir = 1;
				newt->unfold = unfold;
				newt->children = _gendirtree(buf, newt);
			}
		}
		free(dirs);
	}
	free(buf);
	close(fd);
	return T;
}

void
gendirtree(char path[])
{
	dirtree = _gendirtree(path, nil);
}

void
redraw(void)
{
	draw(screen, screen->r, treeback, nil, ZP);
	drawdirtree(dirtree);
	flushimage(display, 1);
}

void
menu3(void)
{
	static Menu menu = {menu3str};

	switch(menuhit(3, mousectl, &menu, nil)){
	case Refresh:
		gendirtree(rootpath);
		redraw();
		break;
	case Exit:
		threadexitsall(nil);
	}
}

void
resized(void)
{
	if(getwindow(display, Refnone) < 0)
		sysfatal("cannot get display");
	redraw();
}

Dirtree*
_lookupitem(Dirtree *T, int item, int *i)
{
	Dirtree *t;

	if(T == nil)
		return nil;
	if(*i == item)
		return T;
	*i = *i+1;
	t = nil;
	if(T->unfold)
		t = _lookupitem(T->children, item, i);
	if(t == nil)
		t = _lookupitem(T->next, item, i);
	return t;
}

Dirtree*
lookupitem(int item)
{
	int i;

	if(item == -1)
		return nil;
	i = 0;
	return _lookupitem(dirtree, item, &i);
}

Dirtree*
clickitem(int button)
{
	int i, mask;

	mask = 1<<button-1;
	do{
		i = itemat(mousectl->xy);
		if(i != selitem){
			selitem = i;
			redraw();
		}
		readmouse(mousectl);
	}while(mousectl->buttons == mask);
	selitem = -1;
	redraw();
	if(mousectl->buttons != 0)
		return nil;
	return lookupitem(i);
}

void
toggleitem(void)
{
	Dirtree *t;

	t = clickitem(1);
	if(t == nil)
		return;
	t->unfold ^= 1;
	redraw();
}

void
plumbitem(void)
{
	static char buf[PATHMAX+1];
	Dirtree *t;

	t = clickitem(2);
	if(t == nil)
		return;
	snprint(buf, PATHMAX+1, "%s/%本", rootpath, t);
	plumbsendtext(plumbfd, "tree", nil, nil, buf);
}

void
loop(void)
{
	Rune r;
	Alt alts[] = {
		{mousectl->resizec, nil, CHANRCV},
		{mousectl->c, &mousectl->Mouse, CHANRCV},
		{keyboardctl->c, &r, CHANRCV},
		{nil, nil, CHANEND},
	};

	for(;;) switch(alt(alts)){
	case Reshapec:
		resized();
		break;
	case Mousec:
		if(mousectl->buttons & 1)
			toggleitem();
		if(mousectl->buttons & 2)
			plumbitem();
		if(mousectl->buttons & 4)
			menu3();
		break;
	case Keyboardc:
		if(r == Kdel)
			threadexitsall(nil);
	}
}

void
initstyle(void)
{
	Rectangle r₁;

	r₁ = Rect(0, 0, 1, 1);
	treeback = display->white;
	textcol = display->black;
	linecol = allocimage(display, r₁, CMAP8, 1, 0x777777FF);
	selcol = allocimage(display, r₁, CMAP8, 1, 0xDC143CFF);
	seltextcol = display->white;
}

void
usage(void)
{
	fprint(2, "usage: %s [-e] [path]\n", argv0);
	exits("usage");
}

void
threadmain(int argc, char *argv[])
{
	ARGBEGIN{
	case 'u':
		unfold = 1;
		break;
	default:
		usage();
	}ARGEND;

	if(argc == 0)
		getwd(rootpath, sizeof(rootpath));
	else if(argc == 1)
		strncpy(rootpath, argv[0], PATHMAX+1);
	else
		usage();
	fmtinstall(L'本', dirtreefmt);
	if(initdraw(nil, nil, argv0) < 0)
		sysfatal("cannot init draw: %r");
	mousectl = initmouse(nil, screen);
	if(mousectl == nil)
		sysfatal("cannot init mouse: %r");
	keyboardctl = initkeyboard(nil);
	if(keyboardctl == nil)
		sysfatal("cannot init keyboard: %r");
	plumbfd = plumbopen("send", OWRITE);
	if(plumbfd < 0)
		sysfatal("cannot open plumber: %r");
	initstyle();
	gendirtree(rootpath);
	redraw();
	loop();
}
