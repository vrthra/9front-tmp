/*
 * Interface includes:
 *	void plescroll(Panel *p, int top);
 *		move the given character position onto the top line
 *	void plegetsel(Panel *p, int *sel0, int *sel1);
 *		read the selection back
 *	int plelen(Panel *p);
 *		read the length of the text back
 *	Rune *pleget(Panel *p);
 *		get a pointer to the text
 *	void plesel(Panel *p, int sel0, int sel1);
 *		set the selection -- adjusts hiliting
 *	void plepaste(Panel *p, Rune *text, int ntext);
 *		replace the selection with the given text
 */
#include <u.h>
#include <libc.h>
#include <draw.h>
#include <event.h>
#include <panel.h>
#include "pldefs.h"
#include <keyboard.h>

typedef struct Edit Edit;
struct Edit{
	Point minsize;
	void (*hit)(Panel *);
	int sel0, sel1;
	Textwin *t;
	Rune *text;
	int ntext;
};
void pl_drawedit(Panel *p){
	Edit *ep;
	Panel *sb;
	ep=p->data;
	if(ep->t==0){
		ep->t=twnew(p->b, font, ep->text, ep->ntext);
		if(ep->t==0){
			fprint(2, "pl_drawedit: can't allocate\n");
			exits("no mem");
		}
	}
	ep->t->b=p->b;
	twreshape(ep->t, p->r);
	twhilite(ep->t, ep->sel0, ep->sel1, 1);
	sb=p->yscroller;
	if(sb && sb->setscrollbar)
		sb->setscrollbar(sb, ep->t->top, ep->t->bot, ep->t->etext-ep->t->text);
}
void pl_snarfedit(Panel *p, int cut){
	int fd, n, s0, s1;
	char *s;
	Rune *t;

	if((fd=open("/dev/snarf", cut ? OWRITE|OTRUNC : OREAD))<0)
		return;
	if(cut){
		t=pleget(p);
		plegetsel(p, &s0, &s1);
		if(t==0 || s0>=s1){
			close(fd);
			return;
		}
		s = smprint("%.*S", s1-s0, t+s0);
		if((n = strlen(s))>0)
			write(fd, s, n);
		free(s);
		plepaste(p, 0, 0);
	}else{
		if((s=malloc(4096))==0){
			close(fd);
			return;
		}
		if((n=readn(fd, s, 4096))<0)
			n=0;
		t=runesmprint("%.*s", n, s);
		plepaste(p, t, runestrlen(t));
		free(s);
		free(t);
	}
	close(fd);
}
/*
 * Should do double-clicks:
 *	If ep->sel0==ep->sel1 on entry and the
 *	call to twselect returns the same selection, then
 *	expand selections (| marks possible selection points, ... is expanded selection)
 *	<|...|>			<> must nest
 *	(|...|)			() must nest
 *	[|...|]			[] must nest
 *	{|...|}			{} must nest
 *	'|...|'			no ' in ...
 *	"|...|"			no " in ...
 *	\n|...|\n		either newline may be the corresponding end of text
 *				include the trailing newline in the selection
 *	...|I...		I and ... are characters satisfying pl_idchar(I)
 *	...I|
 */
int pl_hitedit(Panel *p, Mouse *m){
	Edit *ep;
	if(m->buttons&7){
		ep=p->data;
		ep->t->b=p->b;
		twhilite(ep->t, ep->sel0, ep->sel1, 0);
		twselect(ep->t, m);
		ep->sel0=ep->t->sel0;
		ep->sel1=ep->t->sel1;
		plgrabkb(p);
		if((m->buttons&7)==3)
			pl_snarfedit(p, 1);
		else if((m->buttons&7)==5)
			pl_snarfedit(p, 0);
		else if(ep->hit)
			(*ep->hit)(p);
	}
	return 0;
}
void pl_scrolledit(Panel *p, int dir, int buttons, int num, int den){
	Edit *ep;
	Textwin *t;
	Panel *sb;
	int index, nline;
	if(dir!=VERT) return;
	ep=p->data;
	t=ep->t;
	t->b=p->b;
	switch(buttons){
	default:
		return;
	case 1:		/* top line moves to mouse position */
		nline=(t->r.max.y-t->r.min.y)/t->hgt*num/den;
		index=t->top;
		while(index!=0 && nline!=0)
			if(t->text[--index]=='\n') --nline;
		break;
	case 2:		/* absolute */
		index=(t->etext-t->text)*num/den;
		break;
	case 4:		/* mouse points at new top line */
		index=twpt2rune(t,
			Pt(t->r.min.x, t->r.min.y+(t->r.max.y-t->r.min.y)*num/den));
		break;
	}
	while(index!=0 && t->text[index-1]!='\n') --index;
	if(index!=t->top){
		twhilite(ep->t, ep->sel0, ep->sel1, 0);
		twscroll(t, index);
		p->scr.pos.y=t->top;
		twhilite(ep->t, ep->sel0, ep->sel1, 1);
		sb=p->yscroller;
		if(sb && sb->setscrollbar)
			sb->setscrollbar(sb, t->top, t->bot, t->etext-t->text);
	}
}
void pl_typeedit(Panel *p, Rune c){
	Edit *ep;
	Textwin *t;
	int bot, scrolled;
	Panel *sb;
	ep=p->data;
	t=ep->t;
	t->b=p->b;
	twhilite(t, ep->sel0, ep->sel1, 0);
	switch(c){
	case Kesc:
		pl_snarfedit(p, 1);
		break;
	case Kbs:	/* ^H: erase character */
		if(ep->sel0!=0) --ep->sel0;
		twreplace(t, ep->sel0, ep->sel1, 0, 0);
		break;
	case Knack:	/* ^U: erase line */
		while(ep->sel0!=0 && t->text[ep->sel0-1]!='\n') --ep->sel0;
		twreplace(t, ep->sel0, ep->sel1, 0, 0);
		break;
	case Ketb:	/* ^W: erase word */
		while(ep->sel0!=0 && !pl_idchar(t->text[ep->sel0-1])) --ep->sel0;
		while(ep->sel0!=0 && pl_idchar(t->text[ep->sel0-1])) --ep->sel0;
		twreplace(t, ep->sel0, ep->sel1, 0, 0);
		break;
	default:
		twreplace(t, ep->sel0, ep->sel1, &c, 1);
		++ep->sel0;
		break;
	}
	ep->sel1=ep->sel0;
	/*
	 * Scroll up until ep->sel0 is above t->bot.
	 */
	scrolled=0;
	do{
		bot=t->bot;
		if(ep->sel0<=bot) break;
		twscroll(t, twpt2rune(t, Pt(t->r.min.x, t->r.min.y+font->height)));
		scrolled++;
	}while(bot!=t->bot);
	if(scrolled){
		sb=p->yscroller;
		if(sb && sb->setscrollbar)
			sb->setscrollbar(sb, t->top, t->bot, t->etext-t->text);
	}
	twhilite(t, ep->sel0, ep->sel1, 1);
}
Point pl_getsizeedit(Panel *p, Point children){
	USED(children);
	return pl_boxsize(((Edit *)p->data)->minsize, p->state);
}
void pl_childspaceedit(Panel *g, Point *ul, Point *size){
	USED(g, ul, size);
}
void plinitedit(Panel *v, int flags, Point minsize, Rune *text, int ntext, void (*hit)(Panel *)){
	Edit *ep;
	ep=v->data;
	v->flags=flags|LEAF;
	v->state=UP;
	v->draw=pl_drawedit;
	v->hit=pl_hitedit;
	v->type=pl_typeedit;
	v->getsize=pl_getsizeedit;
	v->childspace=pl_childspaceedit;
	v->kind="edit";
	ep->hit=hit;
	ep->minsize=minsize;
	ep->text=text;
	ep->ntext=ntext;
	if(ep->t!=0) twfree(ep->t);
	ep->t=0;
	ep->sel0=-1;
	ep->sel1=-1;
	v->scroll=pl_scrolledit;
	v->scr.pos=Pt(0,0);
	v->scr.size=Pt(ntext,0);
}
Panel *pledit(Panel *parent, int flags, Point minsize, Rune *text, int ntext, void (*hit)(Panel *)){
	Panel *v;
	v=pl_newpanel(parent, sizeof(Edit));
	((Edit *)v->data)->t=0;
	plinitedit(v, flags, minsize, text, ntext, hit);
	return v;
}
void plescroll(Panel *p, int top){
	twscroll(((Edit *)p->data)->t, top);
}
void plegetsel(Panel *p, int *sel0, int *sel1){
	Edit *ep;
	ep=p->data;
	*sel0=ep->sel0;
	*sel1=ep->sel1;
}
int plelen(Panel *p){
	Textwin *t;
	t=((Edit *)p->data)->t;
	return t->etext-t->text;
}
Rune *pleget(Panel *p){
	return ((Edit *)p->data)->t->text;
}
void plesel(Panel *p, int sel0, int sel1){
	Edit *ep;
	ep=p->data;
	ep->t->b=p->b;
	twhilite(ep->t, ep->sel0, ep->sel1, 0);
	ep->sel0=sel0;
	ep->sel1=sel1;
	twhilite(ep->t, ep->sel0, ep->sel1, 1);
}
void plepaste(Panel *p, Rune *text, int ntext){
	Edit *ep;
	ep=p->data;
	ep->t->b=p->b;
	twhilite(ep->t, ep->sel0, ep->sel1, 0);
	twreplace(ep->t, ep->sel0, ep->sel1, text, ntext);
	ep->sel1=ep->sel0+ntext;
	twhilite(ep->t, ep->sel0, ep->sel1, 1);
	p->scr.size.y=ep->t->etext-ep->t->text;
	p->scr.pos.y=ep->t->top;
}
void plemove(Panel *p, Point d){
	Edit *ep;
	ep=p->data;
	if(ep->t && !eqpt(d, Pt(0,0))) twmove(ep->t, d);
}
