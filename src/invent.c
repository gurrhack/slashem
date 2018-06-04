/*	SCCS Id: @(#)invent.c	3.4	2003/12/02	*/
/* Copyright (c) Stichting Mathematisch Centrum, Amsterdam, 1985. */
/* NetHack may be freely redistributed.  See license for details. */

#include "hack.h"
#include "qtext.h"

#define NOINVSYM	'#'
#define CONTAINED_SYM	'>'	/* designator for inside a container */

#ifdef OVL1
STATIC_DCL void reorder_invent(void);
STATIC_DCL boolean mergable(struct obj *,struct obj *);
STATIC_DCL void invdisp_nothing(const char *,const char *);
STATIC_DCL boolean worn_wield_only(struct obj *);
STATIC_DCL boolean only_here(struct obj *);
#endif /* OVL1 */
STATIC_DCL void compactify(char *);
STATIC_DCL boolean taking_off(const char *);
STATIC_DCL boolean putting_on(const char *);
STATIC_PTR int ckunpaid(struct obj *);
STATIC_PTR int ckunided(struct obj *);
STATIC_PTR int ckvalidcat(struct obj *);
#ifdef DUMP_LOG
static char display_pickinv(const char *,BOOLEAN_P, long *, BOOLEAN_P);
#else
static char display_pickinv(const char *,BOOLEAN_P, long *);
#endif /* DUMP_LOG */
#ifdef OVLB
STATIC_DCL boolean this_type_only(struct obj *);
STATIC_DCL void dounpaid(void);
STATIC_DCL struct obj *find_unpaid(struct obj *,struct obj **);
STATIC_DCL void menu_identify(int);
STATIC_DCL boolean tool_in_use(struct obj *);
#endif /* OVLB */
STATIC_DCL char obj_to_let(struct obj *);
STATIC_DCL int itemactions(struct obj *);

/* define for getobj() */
#define FOLLOW(curr, flags) \
    (((flags) & BY_NEXTHERE) ? (curr)->nexthere : (curr)->nobj)
static const char all_count[] = { ALLOW_COUNT, ALL_CLASSES, 0 };

#ifdef OVLB

static int lastinvnr = 51;	/* 0 ... 51 (never saved&restored) */

#ifdef WIZARD
/* wizards can wish for venom, which will become an invisible inventory
 * item without this.  putting it in inv_order would mean venom would
 * suddenly become a choice for all the inventory-class commands, which
 * would probably cause mass confusion.  the test for inventory venom
 * is only WIZARD and not wizard because the wizard can leave venom lying
 * around on a bones level for normal players to find.
 */
static char venom_inv[] = { VENOM_CLASS, 0 };	/* (constant) */
#endif

void
assigninvlet(otmp)
register struct obj *otmp;
{
	boolean inuse[52];
	register int i;
	register struct obj *obj;

#ifdef GOLDOBJ
        /* There is only one of these in inventory... */        
        if (otmp->oclass == COIN_CLASS) {
	    otmp->invlet = GOLD_SYM;
	    return;
	}
#endif

	for(i = 0; i < 52; i++) inuse[i] = FALSE;
	for(obj = invent; obj; obj = obj->nobj) if(obj != otmp) {
		i = obj->invlet;
		if('a' <= i && i <= 'z') inuse[i - 'a'] = TRUE; else
		if('A' <= i && i <= 'Z') inuse[i - 'A' + 26] = TRUE;
		if(i == otmp->invlet) otmp->invlet = 0;
	}
	if((i = otmp->invlet) &&
	    (('a' <= i && i <= 'z') || ('A' <= i && i <= 'Z')))
		return;
	for(i = lastinvnr+1; i != lastinvnr; i++) {
		if(i == 52) { i = -1; continue; }
		if(!inuse[i]) break;
	}
	otmp->invlet = (inuse[i] ? NOINVSYM :
			(i < 26) ? ('a'+i) : ('A'+i-26));
	lastinvnr = i;
}

#endif /* OVLB */
#ifdef OVL1

/* note: assumes ASCII; toggling a bit puts lowercase in front of uppercase */
#define inv_rank(o) ((o)->invlet ^ 040)

/* sort the inventory; used by addinv() and doorganize() */
STATIC_OVL void
reorder_invent()
{
	struct obj *otmp, *prev, *next;
	boolean need_more_sorting;

	do {
	    /*
	     * We expect at most one item to be out of order, so this
	     * isn't nearly as inefficient as it may first appear.
	     */
	    need_more_sorting = FALSE;
	    for (otmp = invent, prev = 0; otmp; ) {
		next = otmp->nobj;
		if (next && inv_rank(next) < inv_rank(otmp)) {
		    need_more_sorting = TRUE;
		    if (prev) prev->nobj = next;
		    else      invent = next;
		    otmp->nobj = next->nobj;
		    next->nobj = otmp;
		    prev = next;
		} else {
		    prev = otmp;
		    otmp = next;
		}
	    }
	} while (need_more_sorting);
}

#undef inv_rank

/* KMH, balance patch -- Idea by Wolfgang von Hansen <wvh@geodesy.inka.de>.
 * Harmless to character, yet deliciously evil.
 * Somewhat expensive, so don't use it often.
 *
 * Some players who depend upon fixinv complained.  They take damage
 * instead.
 */
int jumble_pack ()
{
	register struct obj *obj, *nobj, *otmp;
	register char let;
	register int dmg = 0;


	for (obj = invent; obj; obj = nobj)
	{
		nobj = obj->nobj;
		if (rn2(10))
			/* Skip it */;
		else if (flags.invlet_constant)
			dmg += 2;
		else {
			/* Remove it from the inventory list (but don't touch the obj) */
			extract_nobj(obj, &invent);

			/* Determine the new letter */
			let = rnd(52) + 'A';
			if (let > 'Z')
				let = let - 'Z' + 'a' - 1;

			/* Does another object share this letter? */
			for (otmp = invent; otmp; otmp = otmp->nobj)
				if (otmp->invlet == let)
					otmp->invlet = obj->invlet;

			/* Add the item back into the inventory */
			obj->invlet = let;
			obj->nobj = invent; /* insert at beginning */
			obj->where = OBJ_INVENT;
			invent = obj;
		}
	}

	/* Clean up */
	reorder_invent();
	return (dmg);
}


/* scan a list of objects to see whether another object will merge with
   one of them; used in pickup.c when all 52 inventory slots are in use,
   to figure out whether another object could still be picked up */
struct obj *
merge_choice(objlist, obj)
struct obj *objlist, *obj;
{
	struct monst *shkp;
	int save_nocharge;

	if (obj->otyp == SCR_SCARE_MONSTER || obj->otyp == SCR_INSTANT_AMNESIA || (obj->oclass == POTION_CLASS && (Monsterfingers || u.uprops[MONSTERFINGERS_EFFECT].extrinsic || have_butterfingerstone() ) ) || (obj->oclass == SCROLL_CLASS && (DustbinBug || u.uprops[DUSTBIN_BUG].extrinsic || have_dustbinstone()) ) )	/* punt on these */
	    return (struct obj *)0;
	/* if this is an item on the shop floor, the attributes it will
	   have when carried are different from what they are now; prevent
	   that from eliciting an incorrect result from mergable() */
	save_nocharge = obj->no_charge;
	if (objlist == invent && obj->where == OBJ_FLOOR &&
		(shkp = shop_keeper(inside_shop(obj->ox, obj->oy))) != 0) {
	    if (obj->no_charge) obj->no_charge = 0;
	    /* A billable object won't have its `unpaid' bit set, so would
	       erroneously seem to be a candidate to merge with a similar
	       ordinary object.  That's no good, because once it's really
	       picked up, it won't merge after all.  It might merge with
	       another unpaid object, but we can't check that here (depends
	       too much upon shk's bill) and if it doesn't merge it would
	       end up in the '#' overflow inventory slot, so reject it now. */
	    else if (inhishop(shkp)) return (struct obj *)0;
	}
	while (objlist) {
	    if (mergable(objlist, obj)) break;
	    objlist = objlist->nobj;
	}
	obj->no_charge = save_nocharge;
	return objlist;
}

/* merge obj with otmp and delete obj if types agree */
int
merged(potmp, pobj)
struct obj **potmp, **pobj;
{
	register struct obj *otmp = *potmp, *obj = *pobj;

	if(mergable(otmp, obj)) {
		/* Approximate age: we do it this way because if we were to
		 * do it "accurately" (merge only when ages are identical)
		 * we'd wind up never merging any corpses.
		 * otmp->age = otmp->age*(1-proportion) + obj->age*proportion;
		 *
		 * Don't do the age manipulation if lit.  We would need
		 * to stop the burn on both items, then merge the age,
		 * then restart the burn.
		 */
		if (!obj->lamplit)
		    otmp->age = ((otmp->age*otmp->quan) + (obj->age*obj->quan))
			    / (otmp->quan + obj->quan);

		otmp->quan += obj->quan;
#ifdef GOLDOBJ
                /* temporary special case for gold objects!!!! */
#endif
		if (otmp->oclass == COIN_CLASS) otmp->owt = weight(otmp);
		else otmp->owt += obj->owt;
		if(!otmp->onamelth && obj->onamelth)
			otmp = *potmp = oname(otmp, ONAME(obj));
		obj_extract_self(obj);

		/* really should merge the timeouts */
		if (obj->lamplit) obj_merge_light_sources(obj, otmp);
		if (obj->timed) obj_stop_timers(obj);	/* follows lights */

		/* fixup for `#adjust' merging wielded darts, daggers, &c */
		if (obj->owornmask && carried(otmp)) {
		    long wmask = otmp->owornmask | obj->owornmask;

		    /* Both the items might be worn in competing slots;
		       merger preference (regardless of which is which):
			 primary weapon + alternate weapon -> primary weapon;
			 primary weapon + quiver -> primary weapon;
			 alternate weapon + quiver -> alternate weapon.
		       (Prior to 3.3.0, it was not possible for the two
		       stacks to be worn in different slots and `obj'
		       didn't need to be unworn when merging.) */
		    if (wmask & W_WEP) wmask = W_WEP;
		    else if (wmask & W_SWAPWEP) wmask = W_SWAPWEP;
		    else if (wmask & W_QUIVER) wmask = W_QUIVER;
		    else {
			impossible("merging strangely worn items (%lx)", wmask);
			wmask = otmp->owornmask;
		    }
		    if ((otmp->owornmask & ~wmask) != 0L) setnotworn(otmp);
		    setworn(otmp, wmask);
		    setnotworn(obj);
		}
#if 0
		/* (this should not be necessary, since items
		    already in a monster's inventory don't ever get
		    merged into other objects [only vice versa]) */
		else if (obj->owornmask && mcarried(otmp)) {
		    if (obj == MON_WEP(otmp->ocarry)) {
			MON_WEP(otmp->ocarry) = otmp;
			otmp->owornmask = W_WEP;
		    }
		}
#endif /*0*/

		obfree(obj,otmp);	/* free(obj), bill->otmp */
		return(1);
	}
	return 0;
}

/*
Adjust hero intrinsics as if this object was being added to the hero's
inventory.  Called _before_ the object has been added to the hero's
inventory.

This is called when adding objects to the hero's inventory normally (via
addinv) or when an object in the hero's inventory has been polymorphed
in-place.

It may be valid to merge this code with with addinv_core2().
*/
void
addinv_core1(obj)
struct obj *obj;
{
	if (obj->oclass == COIN_CLASS) {
#ifndef GOLDOBJ
		u.ugold += obj->quan;
#else
		flags.botl = 1;
#endif
	} else if (obj->otyp == AMULET_OF_YENDOR) {
		if (u.uhave.amulet) impossible("already have amulet?");
		u.uhave.amulet = 1;
#ifdef RECORD_ACHIEVE

		if (!achieve.get_amulet) {
			achieve.get_amulet = 1; /* filthy hangup cheater bastard!!! --Amy */

			if (uarmc && OBJ_DESCR(objects[uarmc->otyp]) && (!strcmp(OBJ_DESCR(objects[uarmc->otyp]), "team splat cloak") || !strcmp(OBJ_DESCR(objects[uarmc->otyp]), "vosklitsatel'nyy znak plashch komanda") || !strcmp(OBJ_DESCR(objects[uarmc->otyp]), "jamoasi xavfsizlik plash") )) pline("TROPHY GET!");
			if (RngeTeamSplat) pline("TROPHY GET!");

			if (uarmc && uarmc->oartifact == ART_JUNETHACK______WINNER) {
				u.uhpmax += 10;
				u.uenmax += 10;
				if (Upolyd) u.mhmax += 10;
				pline("Well done! Your maximum health and mana were increased to make sure you'll get even more trophies! Go for it!");
			}

			qt_pager(QT_PICKAMULET);
			if (u.umortality < 1) {
				u.extralives++;
				pline("Thanks to your flawless performance so far, you gain an extra life (1-UP)!");
			}
			com_pager(196);

		}

                achieve.get_amulet = 1;

#ifdef LIVELOGFILE
		livelog_achieve_update();
		livelog_report_trophy("obtained the Amulet of Yendor");
#endif
#endif
	} else if (obj->otyp == CANDELABRUM_OF_INVOCATION) {
		if (u.uhave.menorah) impossible("already have candelabrum?");
		u.uhave.menorah = 1;
		if (!u.menoraget) {
			u.menoraget = 1;
			u.uhpmax += rnd(3);
			u.uenmax += rnd(3);
			if (Upolyd) u.mhmax += rnd(3);
		}
#ifdef RECORD_ACHIEVE

		if (!achieve.get_candelabrum) {

			if (uarmc && OBJ_DESCR(objects[uarmc->otyp]) && (!strcmp(OBJ_DESCR(objects[uarmc->otyp]), "team splat cloak") || !strcmp(OBJ_DESCR(objects[uarmc->otyp]), "vosklitsatel'nyy znak plashch komanda") || !strcmp(OBJ_DESCR(objects[uarmc->otyp]), "jamoasi xavfsizlik plash") )) pline("TROPHY GET!");
			if (RngeTeamSplat) pline("TROPHY GET!");

			if (uarmc && uarmc->oartifact == ART_JUNETHACK______WINNER) {
				u.uhpmax += 10;
				u.uenmax += 10;
				if (Upolyd) u.mhmax += 10;
				pline("Well done! Your maximum health and mana were increased to make sure you'll get even more trophies! Go for it!");
			}

		}

                achieve.get_candelabrum = 1;
			qt_pager(QT_VLAD);
#ifdef LIVELOGFILE
		livelog_achieve_update();
		livelog_report_trophy("obtained the Candelabrum of Invocation");
#endif
#endif
	} else if (obj->otyp == BELL_OF_OPENING) {
		if (u.uhave.bell) impossible("already have silver bell?");
		u.uhave.bell = 1;
		if (!u.silverbellget) {
			com_pager(195);
			u.silverbellget = 1;
			u.uhpmax += rnd(3);
			u.uenmax += rnd(3);
			if (Upolyd) u.mhmax += rnd(3);
		}
#ifdef RECORD_ACHIEVE

		if (!achieve.get_bell) {

			if (uarmc && OBJ_DESCR(objects[uarmc->otyp]) && (!strcmp(OBJ_DESCR(objects[uarmc->otyp]), "team splat cloak") || !strcmp(OBJ_DESCR(objects[uarmc->otyp]), "vosklitsatel'nyy znak plashch komanda") || !strcmp(OBJ_DESCR(objects[uarmc->otyp]), "jamoasi xavfsizlik plash") )) pline("TROPHY GET!");
			if (RngeTeamSplat) pline("TROPHY GET!");

			if (uarmc && uarmc->oartifact == ART_JUNETHACK______WINNER) {
				u.uhpmax += 10;
				u.uenmax += 10;
				if (Upolyd) u.mhmax += 10;
				pline("Well done! Your maximum health and mana were increased to make sure you'll get even more trophies! Go for it!");
			}

		}

                achieve.get_bell = 1;
#ifdef LIVELOGFILE
		livelog_achieve_update();
		livelog_report_trophy("obtained the Bell of Opening");
#endif
#endif
	} else if (obj->otyp == SPE_BOOK_OF_THE_DEAD) {
		if (u.uhave.book) impossible("already have the book?");
		u.uhave.book = 1;
		if (!u.bookofthedeadget) {
			u.bookofthedeadget = 1;
			u.uhpmax += rnd(3);
			u.uenmax += rnd(3);
			if (Upolyd) u.mhmax += rnd(3);
		}
#ifdef RECORD_ACHIEVE

		if (!achieve.get_book) {

			if (uarmc && OBJ_DESCR(objects[uarmc->otyp]) && (!strcmp(OBJ_DESCR(objects[uarmc->otyp]), "team splat cloak") || !strcmp(OBJ_DESCR(objects[uarmc->otyp]), "vosklitsatel'nyy znak plashch komanda") || !strcmp(OBJ_DESCR(objects[uarmc->otyp]), "jamoasi xavfsizlik plash") )) pline("TROPHY GET!");
			if (RngeTeamSplat) pline("TROPHY GET!");

			if (uarmc && uarmc->oartifact == ART_JUNETHACK______WINNER) {
				u.uhpmax += 10;
				u.uenmax += 10;
				if (Upolyd) u.mhmax += 10;
				pline("Well done! Your maximum health and mana were increased to make sure you'll get even more trophies! Go for it!");
			}

		}

                achieve.get_book = 1;
			qt_pager(QT_RODNEY);
#ifdef LIVELOGFILE
		livelog_achieve_update();
		livelog_report_trophy("obtained the Book of the Dead");
#endif
#endif
	} else if (obj->oartifact) {
		if (is_quest_artifact(obj)) {
		    if (u.uhave.questart)
			impossible("already have quest artifact?");
		    u.uhave.questart = 1;
		    artitouch();
		}
		if(obj->oartifact == ART_TREASURY_OF_PROTEUS){
			u.ukinghill = TRUE;
		}
		if((obj->oartifact == ART_KEY_OF_CHAOS) && !u.chaoskeyget) {
			u.chaoskeyget = 1;
			u.uhpmax += rnd(2);
			u.uenmax += rnd(2);
			if (Upolyd) u.mhmax += rnd(2);
		}
		if((obj->oartifact == ART_KEY_OF_NEUTRALITY) && !u.neutralkeyget) {
			u.neutralkeyget = 1;
			u.uhpmax += rnd(2);
			u.uenmax += rnd(2);
			if (Upolyd) u.mhmax += rnd(2);
		}
		if((obj->oartifact == ART_KEY_OF_LAW) && !u.lawfulkeyget) {
			u.lawfulkeyget = 1;
			u.uhpmax += rnd(2);
			u.uenmax += rnd(2);
			if (Upolyd) u.mhmax += rnd(2);
		}

		set_artifact_intrinsic(obj, 1, W_ART);
	
	}

#ifdef RECORD_ACHIEVE
        if(obj->otyp == LUCKSTONE && obj->record_achieve_special) {

		if (!achieve.get_luckstone) {

			if (uarmc && OBJ_DESCR(objects[uarmc->otyp]) && (!strcmp(OBJ_DESCR(objects[uarmc->otyp]), "team splat cloak") || !strcmp(OBJ_DESCR(objects[uarmc->otyp]), "vosklitsatel'nyy znak plashch komanda") || !strcmp(OBJ_DESCR(objects[uarmc->otyp]), "jamoasi xavfsizlik plash") )) pline("TROPHY GET!");
			if (RngeTeamSplat) pline("TROPHY GET!");

			if (uarmc && uarmc->oartifact == ART_JUNETHACK______WINNER) {
				u.uhpmax += 10;
				u.uenmax += 10;
				if (Upolyd) u.mhmax += 10;
				pline("Well done! Your maximum health and mana were increased to make sure you'll get even more trophies! Go for it!");
			}

		}

                achieve.get_luckstone = 1;
			qt_pager(QT_LUCKSTONE);
                obj->record_achieve_special = 0;
		    if (!u.luckstoneget) {
			u.luckstoneget = 1;
			u.uhpmax += rnd(2);
			u.uenmax += rnd(2);
			if (Upolyd) u.mhmax += rnd(2);
		    }
#ifdef LIVELOGFILE
		livelog_achieve_update();
		livelog_report_trophy("obtained the luckstone from Mines End");
#endif
        } else if((obj->otyp == AMULET_OF_REFLECTION || obj->otyp == GAUNTLETS_OF_REFLECTION || obj->otyp == RIN_POLYMORPH_CONTROL || obj->otyp == RIN_TELEPORT_CONTROL || obj->otyp == SHIELD_OF_MOBILITY || obj->otyp == HELM_OF_DRAIN_RESISTANCE || obj->otyp == CYAN_DRAGON_SCALE_MAIL || obj->otyp == FLYING_BOOTS ||
                   obj->otyp == BAG_OF_HOLDING) &&
                  obj->record_achieve_special) {

		if (!achieve.finish_sokoban) {

			if (uarmc && OBJ_DESCR(objects[uarmc->otyp]) && (!strcmp(OBJ_DESCR(objects[uarmc->otyp]), "team splat cloak") || !strcmp(OBJ_DESCR(objects[uarmc->otyp]), "vosklitsatel'nyy znak plashch komanda") || !strcmp(OBJ_DESCR(objects[uarmc->otyp]), "jamoasi xavfsizlik plash") )) pline("TROPHY GET!");
			if (RngeTeamSplat) pline("TROPHY GET!");

			if (uarmc && uarmc->oartifact == ART_JUNETHACK______WINNER) {
				u.uhpmax += 10;
				u.uenmax += 10;
				if (Upolyd) u.mhmax += 10;
				pline("Well done! Your maximum health and mana were increased to make sure you'll get even more trophies! Go for it!");
			}

		}

                achieve.finish_sokoban = 1;
			qt_pager(QT_SOKOBAN);
                obj->record_achieve_special = 0;
		    if (!u.sokobanfinished) {
			u.sokobanfinished = 1;
			u.uhpmax += rnd(2);
			u.uenmax += rnd(2);
			if (Upolyd) u.mhmax += rnd(2);
		    }
#ifdef LIVELOGFILE
		livelog_achieve_update();
		livelog_report_trophy("obtained the Sokoban prize");
#endif
        } else if((obj->otyp == STONE_OF_MAGIC_RESISTANCE) && obj->record_achieve_special) {

		if (!achieveX.get_magresstone) {

			if (uarmc && OBJ_DESCR(objects[uarmc->otyp]) && (!strcmp(OBJ_DESCR(objects[uarmc->otyp]), "team splat cloak") || !strcmp(OBJ_DESCR(objects[uarmc->otyp]), "vosklitsatel'nyy znak plashch komanda") || !strcmp(OBJ_DESCR(objects[uarmc->otyp]), "jamoasi xavfsizlik plash") )) pline("TROPHY GET!");
			if (RngeTeamSplat) pline("TROPHY GET!");

			if (uarmc && uarmc->oartifact == ART_JUNETHACK______WINNER) {
				u.uhpmax += 10;
				u.uenmax += 10;
				if (Upolyd) u.mhmax += 10;
				pline("Well done! Your maximum health and mana were increased to make sure you'll get even more trophies! Go for it!");
			}

		}

            achieveX.get_magresstone = 1;
		/*qt_pager(QT_DEEPMINES);*/
		obj->record_achieve_special = 0;
		if (!u.deepminefinished) {
			u.deepminefinished = 1;
			u.uhpmax += rnd(2);
			u.uenmax += rnd(2);
			if (Upolyd) u.mhmax += rnd(2);
		}
#ifdef LIVELOGFILE
		livelog_achieve_update();
		livelog_report_trophy("obtained the stone of magic resistance from the Deep Mines");
#endif
        }
#endif /* RECORD_ACHIEVE */

}

/*
Adjust hero intrinsics as if this object was being added to the hero's
inventory.  Called _after_ the object has been added to the hero's
inventory.

This is called when adding objects to the hero's inventory normally (via
addinv) or when an object in the hero's inventory has been polymorphed
in-place.
*/
void
addinv_core2(obj)
struct obj *obj;
{
	if (confers_luck(obj)) {
		/* new luckstone must be in inventory by this point
		 * for correct calculation */
		set_moreluck();
	}

	/* KMH, balance patch -- recalculate health if you've gained healthstones */
	if (obj->otyp == HEALTHSTONE)
		recalc_health();

}

/*
Add obj to the hero's inventory.  Make sure the object is "free".
Adjust hero attributes as necessary.
*/
struct obj *
addinv(obj)
struct obj *obj;
{
	struct obj *otmp, *prev;

	if (obj->where != OBJ_FREE)
	    panic("addinv: obj not free");
	obj->no_charge = 0;	/* not meaningful for invent */

	addinv_core1(obj);
#ifndef GOLDOBJ
	/* if handed gold, we're done */
	if (obj->oclass == COIN_CLASS)
	    return obj;
#endif

	/* merge if possible; find end of chain in the process */
	for (prev = 0, otmp = invent; otmp; prev = otmp, otmp = otmp->nobj)
	    if (merged(&otmp, &obj)) {
		obj = otmp;
		goto added;
	    }
	/* didn't merge, so insert into chain */
	if (flags.invlet_constant || !prev) {
	    if (flags.invlet_constant) assigninvlet(obj);
	    obj->nobj = invent;		/* insert at beginning */
	    invent = obj;
	    if (flags.invlet_constant) reorder_invent();
	} else {
	    prev->nobj = obj;		/* insert at end */
	    obj->nobj = 0;
	}
	obj->where = OBJ_INVENT;

added:
	addinv_core2(obj);
	carry_obj_effects(&youmonst, obj); /* carrying affects the obj */
	update_inventory();

	if (obj && obj->oartifact == ART_KHOR_S_CURSE) {
		curse(obj);
		if (obj->spe > -5) obj->spe = -5;
	}

	if (obj && (BlesscurseEffect || u.uprops[BLESSCURSE_EFFECT].extrinsic || have_blesscursestone()) && obj->blessed) {
		curse(obj);
		if ((uleft && uleft->oartifact == ART_EVIL_DETECTOR) || (uright && uright->oartifact == ART_EVIL_DETECTOR)) obj->bknown = TRUE;
	}

	return(obj);
}
/*
 * Some objects are affected by being carried.
 * Make those adjustments here. Called _after_ the object
 * has been added to the hero's or monster's inventory,
 * and after hero's intrinsics have been updated.
 */
void
carry_obj_effects(mon, obj)
struct monst *mon;
struct obj *obj;
{
	/* Cursed figurines can spontaneously transform
	   when carried. */
	if (obj->otyp == FIGURINE) {
		if (obj->cursed
	    	    && obj->corpsenm != NON_PM
	    	    && !dead_species(obj->corpsenm,TRUE)) {
			attach_fig_transform_timeout(obj);
		    }
	}
	else if (obj->otyp == TORCH && obj->lamplit) {
	  /* MRKR: extinguish torches before putting them */
	  /*       away. Should monsters do the same?  */

	  if (mon == &youmonst) {
	    You("extinguish %s before putting it away.", 
		yname(obj));
	    end_burn(obj, TRUE);
	  }
	}	
}

#endif /* OVL1 */
#ifdef OVLB

/* Add an item to the inventory unless we're fumbling or it refuses to be
 * held (via touch_artifact), and give a message.
 * If there aren't any free inventory slots, we'll drop it instead.
 * If both success and failure messages are NULL, then we're just doing the
 * fumbling/slot-limit checking for a silent grab.  In any case,
 * touch_artifact will print its own messages if they are warranted.
 */
struct obj *
hold_another_object(obj, drop_fmt, drop_arg, hold_msg)
struct obj *obj;
const char *drop_fmt, *drop_arg, *hold_msg;
{
	char buf[BUFSZ];

	if (!Blind && ((!obj->oinvis || See_invisible) && !obj->oinvisreal) ) obj->dknown = 1;
	if (obj->oartifact) {
	    /* place_object may change these */
	    boolean crysknife = (obj->otyp == CRYSKNIFE);
	    int oerode = obj->oerodeproof;
	    boolean wasUpolyd = Upolyd;

	    /* in case touching this object turns out to be fatal */
	    place_object(obj, u.ux, u.uy);

	    if (!touch_artifact(obj, &youmonst)) {
		obj_extract_self(obj);	/* remove it from the floor */
		dropy(obj);		/* now put it back again :-) */
		return obj;
	    } else if (wasUpolyd && !Upolyd) {
		/* loose your grip if you revert your form */
		if (drop_fmt) pline(drop_fmt, drop_arg);
		obj_extract_self(obj);
		dropy(obj);
		return obj;
	    }
	    obj_extract_self(obj);
	    if (crysknife) {
		obj->otyp = CRYSKNIFE;
		obj->oerodeproof = oerode;
	    }
	}
	if (Fumbling) {
	    if (drop_fmt) pline(drop_fmt, drop_arg);
	    dropy(obj);
	} else {
	    long oquan = obj->quan;
	    int prev_encumbr = near_capacity();	/* before addinv() */

	    /* encumbrance only matters if it would now become worse
	       than max( current_value, stressed ) */
	    if (prev_encumbr < MOD_ENCUMBER) prev_encumbr = MOD_ENCUMBER;
	    /* addinv() may redraw the entire inventory, overwriting
	       drop_arg when it comes from something like doname() */
	    if (drop_arg) drop_arg = strcpy(buf, drop_arg);

	    obj = addinv(obj);
	    if (inv_cnt() > 52
		    || (( (obj->otyp != LOADSTONE && obj->otyp != HEALTHSTONE && obj->otyp != LUCKSTONE && obj->otyp != MANASTONE && obj->otyp != SLEEPSTONE && obj->otyp != LOADBOULDER && obj->otyp != STARLIGHTSTONE && obj->otyp != STONE_OF_MAGIC_RESISTANCE && !is_nastygraystone(obj) ) || (!obj->cursed && !is_nastygraystone(obj)) )
			&& near_capacity() > prev_encumbr)) {
		if (drop_fmt) pline(drop_fmt, drop_arg);
		/* undo any merge which took place */
		if (obj->quan > oquan) obj = splitobj(obj, oquan);
		dropx(obj);
	    } else {
		if (flags.autoquiver && !uquiver && !obj->owornmask &&
			(is_missile(obj) ||
			    ammo_and_launcher(obj, uwep) ||
			    ammo_and_launcher(obj, uswapwep)))
		    setuqwep(obj);
		if (hold_msg || drop_fmt) prinv(hold_msg, obj, oquan);
	    }
	}
	return obj;
}

struct obj *
hold_another_objectX(obj, drop_fmt, drop_arg, hold_msg)
struct obj *obj;
const char *drop_fmt, *drop_arg, *hold_msg;
{
	char buf[BUFSZ];
	char qbuf[QBUFSZ];

	if (!Blind && ((!obj->oinvis || See_invisible) && !obj->oinvisreal) ) obj->dknown = 1;
	if (obj->oartifact) {
	    /* place_object may change these */
	    boolean crysknife = (obj->otyp == CRYSKNIFE);
	    int oerode = obj->oerodeproof;
	    boolean wasUpolyd = Upolyd;

	    /* in case touching this object turns out to be fatal */
	    place_object(obj, u.ux, u.uy);

	    if (!touch_artifact(obj, &youmonst)) {
		obj_extract_self(obj);	/* remove it from the floor */
		dropy(obj);		/* now put it back again :-) */
		return obj;
	    } else if (wasUpolyd && !Upolyd) {
		/* loose your grip if you revert your form */
		if (drop_fmt) pline(drop_fmt, drop_arg);
		obj_extract_self(obj);
		dropy(obj);
		return obj;
	    }
	    obj_extract_self(obj);
	    if (crysknife) {
		obj->otyp = CRYSKNIFE;
		obj->oerodeproof = oerode;
	    }
	}
	if (Fumbling) {
	    if (drop_fmt) pline(drop_fmt, drop_arg);
	    dropy(obj);
	} else {
	    long oquan = obj->quan;
	    int prev_encumbr = near_capacity();	/* before addinv() */

	    /* encumbrance only matters if it would now become worse
	       than max( current_value, stressed ) */
	    if (prev_encumbr < MOD_ENCUMBER) prev_encumbr = MOD_ENCUMBER;
	    /* addinv() may redraw the entire inventory, overwriting
	       drop_arg when it comes from something like doname() */
	    if (drop_arg) drop_arg = strcpy(buf, drop_arg);

	    obj = addinv(obj);

		/* Players were getting annoyed by having their inventory cluttered with garbage from attacking
		 * low-level monsters as a high-level nymph. Let's allow them to drop items if they don't want them. --Amy */

		obj->mstartinvent = 0;
		obj->mstartinventB = 0;
		sprintf(qbuf, "Got %s! Drop it?", doname(obj) );

		if (yn_function(qbuf, ynchars, 'n') == 'y' ) {

		if (drop_fmt) pline(drop_fmt, drop_arg);
		/* undo any merge which took place */
		if (obj->quan > oquan) obj = splitobj(obj, oquan);

		dropx(obj); /* just drop the crap on the ground. Nobody needs tons of chain mails from just attacking orcs. */
		return obj;
		}

	    if ( inv_cnt() > 52
		    || (( (obj->otyp != LOADSTONE && obj->otyp != HEALTHSTONE && obj->otyp != LUCKSTONE && obj->otyp != MANASTONE && obj->otyp != SLEEPSTONE && obj->otyp != LOADBOULDER && obj->otyp != STARLIGHTSTONE && obj->otyp != STONE_OF_MAGIC_RESISTANCE && !is_nastygraystone(obj) ) || (!obj->cursed && !is_nastygraystone(obj)) )
			&& near_capacity() > prev_encumbr)) {
		if (drop_fmt) pline(drop_fmt, drop_arg);
		/* undo any merge which took place */
		if (obj->quan > oquan) obj = splitobj(obj, oquan);
		dropx(obj);
	    } else {
		if (flags.autoquiver && !uquiver && !obj->owornmask &&
			(is_missile(obj) ||
			    ammo_and_launcher(obj, uwep) ||
			    ammo_and_launcher(obj, uswapwep)))
		    setuqwep(obj);
		if (hold_msg || drop_fmt) prinv(hold_msg, obj, oquan);
	    }
	}
	return obj;
}

/* useup() all of an item regardless of its quantity */
void
useupall(obj)
struct obj *obj;
{

	if (evades_destruction(obj)) return; /* fail safe */

	if (Has_contents(obj)) delete_contents(obj);
	setnotworn(obj);
	freeinv(obj);
	obfree(obj, (struct obj *)0);
}

void
useup(obj)
register struct obj *obj;
{

	if (evades_destruction(obj)) return; /* fail safe */

	/*  Note:  This works correctly for containers because they */
	/*	   (containers) don't merge.			    */
	if(obj->quan > 1L){
		obj->in_use = FALSE;	/* no longer in use */
		obj->quan--;
		obj->owt = weight(obj);
		update_inventory();
	} else {
		useupall(obj);
	}
}

/* use one charge from an item and possibly incur shop debt for it */
void
consume_obj_charge(obj, maybe_unpaid)
struct obj *obj;
boolean maybe_unpaid;	/* false if caller handles shop billing */
{
	if (maybe_unpaid) check_unpaid(obj);
	obj->spe -= 1;
	if (DischargeBug || u.uprops[DISCHARGE_BUG].extrinsic || have_dischargestone()) obj->spe -= 1;
	if (obj->known) update_inventory();
}

#endif /* OVLB */
#ifdef OVL3

/*
Adjust hero's attributes as if this object was being removed from the
hero's inventory.  This should only be called from freeinv() and
where we are polymorphing an object already in the hero's inventory.

Should think of a better name...
*/
void
freeinv_core(obj)
struct obj *obj;
{
	if (obj->oclass == COIN_CLASS) {
#ifndef GOLDOBJ
		u.ugold -= obj->quan;
		obj->in_use = FALSE;
#endif
		flags.botl = 1;
		return;
	} else if (obj->otyp == AMULET_OF_YENDOR) {
		if (!u.uhave.amulet) impossible("don't have amulet?");
		u.uhave.amulet = 0;
	} else if (obj->otyp == CANDELABRUM_OF_INVOCATION) {
		if (!u.uhave.menorah) impossible("don't have candelabrum?");
		u.uhave.menorah = 0;
	} else if (obj->otyp == BELL_OF_OPENING) {
		if (!u.uhave.bell) impossible("don't have silver bell?");
		u.uhave.bell = 0;
	} else if (obj->otyp == SPE_BOOK_OF_THE_DEAD) {
		if (!u.uhave.book) impossible("don't have the book?");
		u.uhave.book = 0;
	} else if (obj->oartifact) {
		if (is_quest_artifact(obj)) {
		    if (!u.uhave.questart)
			impossible("don't have quest artifact?");
		    u.uhave.questart = 0;
		}
		if(obj->oartifact == ART_TREASURY_OF_PROTEUS){
			u.ukinghill = FALSE;
		}
		set_artifact_intrinsic(obj, 0, W_ART);
	}

	if (u.uprops[DROPCURSES_EFFECT].extrinsic || Dropcurses || have_dropcursestone() || (uleft && uleft->oartifact == ART_ARABELLA_S_RADAR) || (uright && uright->oartifact == ART_ARABELLA_S_RADAR) ) {
		curse(obj);
	}

	if (obj->otyp == LOADSTONE || obj->otyp == SLEEPSTONE || obj->otyp == LOADBOULDER || obj->otyp == STARLIGHTSTONE || is_nastygraystone(obj) ) {
		curse(obj);
	} else if (confers_luck(obj)) {
		set_moreluck();
		flags.botl = 1;
	} else if (obj->otyp == HEALTHSTONE) {
	/* KMH, balance patch -- recalculate health if you've lost healthstones */
		recalc_health();
	} else if (obj->otyp == FIGURINE && obj->timed) {
		(void) stop_timer(FIG_TRANSFORM, (void *) obj);
	}

	if (obj && obj->oartifact == ART_KHOR_S_CURSE) {
		curse(obj);
		if (obj->spe > -5) obj->spe = -5;
	}

	if (obj && (BlesscurseEffect || u.uprops[BLESSCURSE_EFFECT].extrinsic || have_blesscursestone()) && obj->blessed) {
		curse(obj);
		if ((uleft && uleft->oartifact == ART_EVIL_DETECTOR) || (uright && uright->oartifact == ART_EVIL_DETECTOR)) obj->bknown = TRUE;
	}

}

/* remove an object from the hero's inventory */
void
freeinv(obj)
register struct obj *obj;
{
	extract_nobj(obj, &invent);
	freeinv_core(obj);
	update_inventory();
}

void
delallobj(x, y)
int x, y;
{
	struct obj *otmp, *otmp2;

	for (otmp = level.objects[x][y]; otmp; otmp = otmp2) {
		if (otmp == uball)
			unpunish();
		/* after unpunish(), or might get deallocated chain */
		otmp2 = otmp->nexthere;
		if (otmp == uchain)
			continue;
		delobj(otmp);
	}
}

#endif /* OVL3 */
#ifdef OVL2

/* destroy object in fobj chain (if unpaid, it remains on the bill) */
void
delobj(obj)
register struct obj *obj;
{
	boolean update_map;

	if (evades_destruction(obj)) {
		/* player might be doing something stupid, but we
		 * can't guarantee that.  assume special artifacts
		 * are indestructible via drawbridges, and exploding
		 * chests, and golem creation, and ...
		 */
		return;
	}
	if (uwep && uwep == obj) setnotworn(obj); /* this hopefully fixes cream pie bugs and similar things --Amy */

	/* to be on the safe side, let's include this check for all the other inventory slots too... */
	if (uswapwep && uswapwep == obj) uswapwepgone();
	if (uquiver && uquiver == obj) uqwepgone();
	if (uarm && uarm == obj) remove_worn_item(obj, TRUE);
	if (uarmc && uarmc == obj) remove_worn_item(obj, TRUE);
	if (uarmh && uarmh == obj) remove_worn_item(obj, TRUE);
	if (uarms && uarms == obj) remove_worn_item(obj, TRUE);
	if (uarmg && uarmg == obj) remove_worn_item(obj, TRUE);
	if (uarmf && uarmf == obj) remove_worn_item(obj, TRUE);
	if (uarmu && uarmu == obj) remove_worn_item(obj, TRUE);
	if (uamul && uamul == obj) remove_worn_item(obj, TRUE);
	if (uimplant && uimplant == obj) remove_worn_item(obj, TRUE);
	if (uleft && uleft == obj) remove_worn_item(obj, TRUE);
	if (uright && uright == obj) remove_worn_item(obj, TRUE);
	if (ublindf && ublindf == obj) remove_worn_item(obj, TRUE);
	if (uball && uball == obj) unpunish();
	if (uchain && uchain == obj) unpunish();

	update_map = (obj->where == OBJ_FLOOR || Has_contents(obj) &&
		(obj->where == OBJ_INVENT || obj->where == OBJ_MINVENT));
	if (Has_contents(obj)) delete_contents(obj);
	obj_extract_self(obj);
	if (update_map) newsym(obj->ox, obj->oy);
	obfree(obj, (struct obj *) 0);
}

#endif /* OVL2 */
#ifdef OVL0

struct obj *
sobj_at(n,x,y)
register int n, x, y;
{
	register struct obj *otmp;

	for(otmp = level.objects[x][y]; otmp; otmp = otmp->nexthere)
		if(otmp->otyp == n)
		    return(otmp);
	return((struct obj *)0);
}

#endif /* OVL0 */
#ifdef OVLB

struct obj *
carrying(type)
register int type;
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj)
		if(otmp->otyp == type)
			return(otmp);
	return((struct obj *) 0);
}

const char *
currency(amount)
long amount;
{
	if (amount == 1L) return "zorkmid";
	else return "zorkmids";
}

boolean
have_lizard()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == CORPSE && otmp->corpsenm == PM_LIZARD)
			return(TRUE);
		if(otmp->otyp == CORPSE && otmp->corpsenm == PM_CAVE_LIZARD)
			return(TRUE);
		if(otmp->otyp == CORPSE && otmp->corpsenm == PM_PREHISTORIC_CAVE_LIZARD)
			return(TRUE);
		if(otmp->otyp == CORPSE && otmp->corpsenm == PM_CHAOS_LIZARD)
			return(TRUE);
		if(otmp->otyp == CORPSE && otmp->corpsenm == PM_CHAOTIC_LIZARD)
			return(TRUE);
		if(otmp->otyp == CORPSE && otmp->corpsenm == PM_LIZARD_OF_YENDOR)
			return(TRUE);
		if(otmp->otyp == CORPSE && otmp->corpsenm == PM_GRASS_LIZARD)
			return(TRUE);
		if(otmp->otyp == CORPSE && otmp->corpsenm == PM_BLUE_LIZARD)
			return(TRUE);
		if(otmp->otyp == CORPSE && otmp->corpsenm == PM_SWAMP_LIZARD)
			return(TRUE);
		if(otmp->otyp == CORPSE && otmp->corpsenm == PM_SPITTING_LIZARD)
			return(TRUE);
		if(otmp->otyp == CORPSE && otmp->corpsenm == PM_LIZARD_EEL)
			return(TRUE);
		if(otmp->otyp == CORPSE && otmp->corpsenm == PM_LIZARD_MAN)
			return(TRUE);
		if(otmp->otyp == CORPSE && otmp->corpsenm == PM_LIZARD_KING)
			return(TRUE);
		if(otmp->otyp == CORPSE && otmp->corpsenm == PM_EEL_LIZARD)
			return(TRUE);
		if(otmp->otyp == CORPSE && otmp->corpsenm == PM_CLINGING_LIZARD)
			return(TRUE);
		if(otmp->otyp == CORPSE && otmp->corpsenm == PM_DEFORMED_LIZARD)
			return(TRUE);
		if(otmp->otyp == CORPSE && otmp->corpsenm == PM_MIMIC_LIZARD)
			return(TRUE);
		if(otmp->otyp == CORPSE && otmp->corpsenm == PM_HIDDEN_LIZARD)
			return(TRUE);
		if(otmp->otyp == CORPSE && otmp->corpsenm == PM_ANTI_STONE_LIZARD)
			return(TRUE);
		if(otmp->otyp == CORPSE && otmp->corpsenm == PM_HUGE_LIZARD)
			return(TRUE);
		if(otmp->otyp == CORPSE && otmp->corpsenm == PM_ROCK_LIZARD)
			return(TRUE);
		if(otmp->otyp == CORPSE && otmp->corpsenm == PM_BABY_CAVE_LIZARD)
			return(TRUE);
		if(otmp->otyp == CORPSE && otmp->corpsenm == PM_NIGHT_LIZARD)
			return(TRUE);
		if(otmp->otyp == CORPSE && otmp->corpsenm == PM_SAND_TIDE)
			return(TRUE);
		if(otmp->otyp == CORPSE && otmp->corpsenm == PM_FBI_AGENT)
			return(TRUE);
		if(otmp->otyp == CORPSE && otmp->corpsenm == PM_OWN_SMOKE)
			return(TRUE);
		if(otmp->otyp == CORPSE && otmp->corpsenm == PM_GRANDPA)
			return(TRUE);
		if(otmp->otyp == CORPSE && otmp->corpsenm == PM_KARMIC_LIZARD)
			return(TRUE);
		if(otmp->otyp == CORPSE && otmp->corpsenm == PM_GREEN_LIZARD)
			return(TRUE);
		if(otmp->otyp == CORPSE && otmp->corpsenm == PM_BLACK_LIZARD)
			return(TRUE);
		if(otmp->otyp == CORPSE && otmp->corpsenm == PM_MONSTER_LIZARD)
			return(TRUE);
		if(otmp->otyp == CORPSE && otmp->corpsenm == PM_FIRE_LIZARD)
			return(TRUE);
		if(otmp->otyp == CORPSE && otmp->corpsenm == PM_LIGHTNING_LIZARD)
			return(TRUE);
		if(otmp->otyp == CORPSE && otmp->corpsenm == PM_ICE_LIZARD)
			return(TRUE);
		if(otmp->otyp == CORPSE && otmp->corpsenm == PM_GIANT_LIZARD)
			return(TRUE);
		}
	return(FALSE);
}

boolean
have_loadstone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == LOADSTONE && otmp->cursed)
			return(TRUE);
		}
	return(FALSE);
}

int
numberofetheritems()
{
	int number = 0;
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if (is_etheritem(otmp)) number++;
	}
	return number;
}

int
numberofwornetheritems()
{
	int number = 0;
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if (is_etheritem(otmp) && otmp->owornmask) number++;
	}
	return number;

}

boolean
have_pokeloadstone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == LOADSTONE && otmp->oartifact == ART_AUTOMATIC_POKE_BALL)
			return(TRUE);
		}
	return(FALSE);
}

boolean
have_invisoloadstone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == LOADSTONE && otmp->oartifact == ART_JONADAB_S_HEAVYLOAD)
			return(TRUE);
		}
	return(FALSE);
}

boolean
have_primecurse()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->prmcurse)
			return(TRUE);
		}
	return(FALSE);
}

boolean
have_morgothiancurse()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->morgcurse)
			return(TRUE);
		}
	return(FALSE);
}

boolean
have_topiylinencurse()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->evilcurse)
			return(TRUE);
		}
	return(FALSE);
}

boolean
have_blackbreathcurse()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->bbrcurse)
			return(TRUE);
		}
	return(FALSE);
}

boolean
have_mothrelay()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if( (otmp->otyp == RELAY) && otmp->oartifact )
			return(TRUE);
		}
	return(FALSE);
}

boolean
have_sleepstone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == SLEEPSTONE)
			return(TRUE);
		}
	return(FALSE);
}

boolean
have_magicresstone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == STONE_OF_MAGIC_RESISTANCE)
			return(TRUE);
		}
	return(FALSE);
}

boolean
have_cursedmagicresstone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == STONE_OF_MAGIC_RESISTANCE  && otmp->cursed)
			return(TRUE);
		}
	return(FALSE);
}

boolean
have_loadboulder()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == LOADBOULDER && otmp->cursed)
			return(TRUE);
		}
	return(FALSE);
}

boolean
have_starlightstone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == STARLIGHTSTONE && otmp->cursed)
			return(TRUE);
		}
	return(FALSE);
}

boolean
have_rmbstone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == RIGHT_MOUSE_BUTTON_STONE)
			return(TRUE);
		}

	if (u.nastinator01) return TRUE;
	if (Role_if(PM_FEMINIST) && u.urmaxlvlUP >= 6 && u.femauspices6 == 1) return TRUE;
	return(FALSE);
}

boolean
have_displaystone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == DISPLAY_LOSS_STONE)
			return(TRUE);
		}
	if (u.nastinator02) return TRUE;
	if (Role_if(PM_FEMINIST) && u.urmaxlvlUP >= 6 && u.femauspices6 == 2) return TRUE;
	return(FALSE);
}

boolean
have_yellowspellstone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == YELLOW_SPELL_STONE)
			return(TRUE);
		}
	if (u.nastinator03) return TRUE;
	if (Role_if(PM_FEMINIST) && u.urmaxlvlUP >= 11 && u.femauspices11 == 1) return TRUE;
	return(FALSE);
}

boolean
have_spelllossstone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == SPELL_LOSS_STONE)
			return(TRUE);
		}
	if (u.nastinator04) return TRUE;
	return(FALSE);
}

boolean
have_autodestructstone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == AUTO_DESTRUCT_STONE)
			return(TRUE);
		}
	if (u.nastinator05) return TRUE;
	if (Role_if(PM_FEMINIST) && u.urmaxlvlUP >= 15 && u.femauspices15 == 2) return TRUE;
	return(FALSE);
}

boolean
have_memorylossstone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == MEMORY_LOSS_STONE)
			return(TRUE);
		}
	if (u.nastinator06) return TRUE;
	if (Role_if(PM_FEMINIST) && u.urmaxlvlUP >= 6 && u.femauspices6 == 3) return TRUE;
	return(FALSE);
}

boolean
have_inventorylossstone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == INVENTORY_LOSS_STONE)
			return(TRUE);
		}
	if (u.nastinator07) return TRUE;
	if (Role_if(PM_FEMINIST) && u.urmaxlvlUP >= 30 && u.femauspices30 == 1) return TRUE;
	return(FALSE);
}

boolean
have_blackystone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == BLACKY_STONE)
			return(TRUE);
		}
	if (u.nastinator08) return TRUE;
	return(FALSE);
}

boolean
have_menubugstone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == MENU_BUG_STONE)
			return(TRUE);
		}
	if (u.nastinator09) return TRUE;
	return(FALSE);
}

boolean
have_speedbugstone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == SPEEDBUG_STONE)
			return(TRUE);
		}
	if (u.nastinator10) return TRUE;
	if (Role_if(PM_FEMINIST) && u.urmaxlvlUP >= 15 && u.femauspices15 == 1) return TRUE;
	return(FALSE);
}

boolean
have_superscrollerstone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == SUPERSCROLLER_STONE)
			return(TRUE);
		}
	if (u.nastinator11) return TRUE;
	return(FALSE);
}

boolean
have_freehandbugstone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == FREE_HAND_BUG_STONE)
			return(TRUE);
		}
	if (u.nastinator12) return TRUE;
	return(FALSE);
}

boolean
have_unidentifystone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == UNIDENTIFY_STONE)
			return(TRUE);
		}
	if (u.nastinator13) return TRUE;
	if (Role_if(PM_FEMINIST) && u.urmaxlvlUP >= 20 && u.femauspices20 == 1) return TRUE;
	return(FALSE);
}

boolean
have_thirststone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == STONE_OF_THIRST)
			return(TRUE);
		}
	if (u.nastinator14) return TRUE;
	return(FALSE);
}

boolean
have_unluckystone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == UNLUCKY_STONE)
			return(TRUE);
		}
	if (u.nastinator15) return TRUE;
	return(FALSE);
}

boolean
have_shadesofgreystone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == SHADES_OF_GREY_STONE)
			return(TRUE);
		}
	if (u.nastinator16) return TRUE;
	if (Role_if(PM_FEMINIST) && u.urmaxlvlUP >= 8 && u.femauspices8 == 1) return TRUE;
	return(FALSE);
}

boolean
have_faintingstone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == STONE_OF_FAINTING)
			return(TRUE);
		}
	if (u.nastinator17) return TRUE;
	return(FALSE);
}

boolean
have_cursingstone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == STONE_OF_CURSING)
			return(TRUE);
		}
	if (u.nastinator18) return TRUE;
	return(FALSE);
}

boolean
have_difficultystone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == STONE_OF_DIFFICULTY)
			return(TRUE);
		}
	if (u.nastinator19) return TRUE;
	if (Role_if(PM_FEMINIST) && u.urmaxlvlUP >= 17 && u.femauspices17 == 1) return TRUE;
	return(FALSE);
}

boolean
have_deafnessstone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == DEAFNESS_STONE)
			return(TRUE);
		}
	if (u.nastinator20) return TRUE;
	return(FALSE);
}

boolean
have_weaknessstone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == WEAKNESS_STONE)
			return(TRUE);
		}
	if (u.nastinator21) return TRUE;
	return(FALSE);
}

boolean
have_antimagicstone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == ANTIMAGIC_STONE)
			return(TRUE);
		}
	if (u.nastinator22) return TRUE;
	return(FALSE);
}

boolean
have_rotthirteenstone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == ROT_THIRTEEN_STONE)
			return(TRUE);
		}
	if (u.nastinator23) return TRUE;
	if (Role_if(PM_FEMINIST) && u.urmaxlvlUP >= 8 && u.femauspices8 == 2) return TRUE;
	return(FALSE);
}

boolean
have_bishopstone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == BISHOP_STONE)
			return(TRUE);
		}
	if (u.nastinator24) return TRUE;
	return(FALSE);
}

boolean
have_confusionstone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == CONFUSION_STONE)
			return(TRUE);
		}
	if (u.nastinator25) return TRUE;
	return(FALSE);
}

boolean
have_dropbugstone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == DROPBUG_STONE)
			return(TRUE);
		}
	if (u.nastinator26) return TRUE;
	return(FALSE);
}

boolean
have_dstwstone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == DSTW_STONE)
			return(TRUE);
		}
	if (u.nastinator27) return TRUE;

	if (Role_if(PM_FEMINIST) && u.urmaxlvlUP >= 4 && u.femauspices4 == 1) return TRUE;

	return(FALSE);
}

boolean
have_amnesiastone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == AMNESIA_STONE)
			return(TRUE);
		}
	if (u.nastinator28) return TRUE;
	if (Role_if(PM_FEMINIST) && u.urmaxlvlUP >= 28 && u.femauspices28 == 2) return TRUE;
	return(FALSE);
}

boolean
have_bigscriptstone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == BIGSCRIPT_STONE)
			return(TRUE);
		}
	if (u.nastinator29) return TRUE;
	if (Role_if(PM_FEMINIST) && u.urmaxlvlUP >= 6 && u.femauspices6 == 4) return TRUE;
	return(FALSE);
}

boolean
have_bankstone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == BANK_STONE)
			return(TRUE);
		}
	if (u.nastinator30) return TRUE;
	return(FALSE);
}

boolean
have_mapstone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == MAP_STONE)
			return(TRUE);
		}
	if (u.nastinator31) return TRUE;
	if (Role_if(PM_FEMINIST) && u.urmaxlvlUP >= 20 && u.femauspices20 == 3) return TRUE;
	return(FALSE);
}

boolean
have_techniquestone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == TECHNIQUE_STONE)
			return(TRUE);
		}
	if (u.nastinator32) return TRUE;
	if (Role_if(PM_FEMINIST) && u.urmaxlvlUP >= 4 && u.femauspices4 == 2) return TRUE;
	return(FALSE);
}

boolean
have_disenchantmentstone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == DISENCHANTMENT_STONE)
			return(TRUE);
		}
	if (u.nastinator33) return TRUE;
	if (Role_if(PM_FEMINIST) && u.urmaxlvlUP >= 28 && u.femauspices28 == 3) return TRUE;
	return(FALSE);
}

boolean
have_verisiertstone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == VERISIERT_STONE)
			return(TRUE);
		}
	if (u.nastinator34) return TRUE;
	if (Role_if(PM_FEMINIST) && u.urmaxlvlUP >= 17 && u.femauspices17 == 2) return TRUE;
	return(FALSE);
}

boolean
have_chaosterrainstone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == CHAOS_TERRAIN_STONE)
			return(TRUE);
		}
	if (u.nastinator35) return TRUE;
	if (Role_if(PM_FEMINIST) && u.urmaxlvlUP >= 22 && u.femauspices22 == 2) return TRUE;
	return(FALSE);
}

boolean
have_mutenessstone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == MUTENESS_STONE)
			return(TRUE);
		}
	if (u.nastinator36) return TRUE;
	return(FALSE);
}

boolean
have_engravingstone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == ENGRAVING_STONE)
			return(TRUE);
		}
	if (u.nastinator37) return TRUE;
	if (Role_if(PM_FEMINIST) && u.urmaxlvlUP >= 4 && u.femauspices4 == 3) return TRUE;
	return(FALSE);
}

boolean
have_magicdevicestone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == MAGIC_DEVICE_STONE)
			return(TRUE);
		}
	if (u.nastinator38) return TRUE;
	if (Role_if(PM_FEMINIST) && u.urmaxlvlUP >= 4 && u.femauspices4 == 4) return TRUE;
	return(FALSE);
}

boolean
have_bookstone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == BOOK_STONE)
			return(TRUE);
		}
	if (u.nastinator39) return TRUE;
	if (Role_if(PM_FEMINIST) && u.urmaxlvlUP >= 4 && u.femauspices4 == 5) return TRUE;
	return(FALSE);
}

boolean
have_levelstone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == LEVEL_STONE)
			return(TRUE);
		}
	if (u.nastinator40) return TRUE;
	if (Role_if(PM_FEMINIST) && u.urmaxlvlUP >= 20 && u.femauspices20 == 4) return TRUE;
	return(FALSE);
}

boolean
have_quizstone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == QUIZ_STONE)
			return(TRUE);
		}
	if (u.nastinator41) return TRUE;
	if (Role_if(PM_FEMINIST) && u.urmaxlvlUP >= 8 && u.femauspices8 == 4) return TRUE;
	return(FALSE);
}

boolean
have_statusstone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == STATUS_STONE)
			return(TRUE);
		}
	if (u.nastinator42) return TRUE;
	if (Role_if(PM_FEMINIST) && u.urmaxlvlUP >= 22 && u.femauspices22 == 1) return TRUE;
	return(FALSE);
}

boolean
have_alignmentstone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == ALIGNMENT_STONE)
			return(TRUE);
		}
	if (u.nastinator43) return TRUE;
	if (Role_if(PM_FEMINIST) && u.urmaxlvlUP >= 28 && u.femauspices28 == 1) return TRUE;
	return(FALSE);
}

boolean
have_stairstrapstone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == STAIRSTRAP_STONE)
			return(TRUE);
		}
	if (u.nastinator44) return TRUE;
	return(FALSE);
}

boolean
have_uninformationstone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == UNINFORMATION_STONE)
			return(TRUE);
		}
	if (u.nastinator45) return TRUE;
	if (Role_if(PM_FEMINIST) && u.urmaxlvlUP >= 20 && u.femauspices20 == 2) return TRUE;
	return(FALSE);
}

boolean
have_captchastone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == CAPTCHA_STONE)
			return(TRUE);
		}
	if (u.nastinator46) return TRUE;
	if (Role_if(PM_FEMINIST) && u.urmaxlvlUP >= 8 && u.femauspices8 == 3) return TRUE;
	return(FALSE);
}

boolean
have_farlookstone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == FARLOOK_STONE)
			return(TRUE);
		}
	if (u.nastinator47) return TRUE;
	return(FALSE);
}

boolean
have_respawnstone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == RESPAWN_STONE)
			return(TRUE);
		}
	if (u.nastinator48) return TRUE;
	return(FALSE);
}

boolean
have_intrinsiclossstone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == STONE_OF_INTRINSIC_LOSS)
			return(TRUE);
		}
	if (u.nastinator49) return TRUE;
	if (Role_if(PM_FEMINIST) && u.urmaxlvlUP >= 13 && u.femauspices13 == 1) return TRUE;
	return(FALSE);
}

boolean
have_bloodlossstone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == BLOOD_LOSS_STONE)
			return(TRUE);
		}
	if (u.nastinator50) return TRUE;
	if (Role_if(PM_FEMINIST) && u.urmaxlvlUP >= 13 && u.femauspices13 == 3) return TRUE;
	return(FALSE);
}

boolean
have_badeffectstone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == BAD_EFFECT_STONE)
			return(TRUE);
		}
	if (u.nastinator51) return TRUE;
	return(FALSE);
}

boolean
have_trapcreationstone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == TRAP_CREATION_STONE)
			return(TRUE);
		}
	if (u.nastinator52) return TRUE;
	return(FALSE);
}

boolean
have_vulnerabilitystone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == STONE_OF_VULNERABILITY)
			return(TRUE);
		}
	if (u.nastinator53) return TRUE;
	if (Role_if(PM_FEMINIST) && u.urmaxlvlUP >= 13 && u.femauspices13 == 2) return TRUE;
	return(FALSE);
}

boolean
have_itemportstone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == ITEM_TELEPORTING_STONE)
			return(TRUE);
		}
	if (u.nastinator54) return TRUE;
	return(FALSE);
}

boolean
have_nastystone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == NASTY_STONE)
			return(TRUE);
		}
	if (u.nastinator55) return TRUE;
	return(FALSE);
}

boolean
have_metabolicstone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == METABOLIC_STONE)
			return(TRUE);
		}
	if (u.nastinator56) return TRUE;
	return(FALSE);
}

boolean
have_noreturnstone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == STONE_OF_NO_RETURN)
			return(TRUE);
		}
	if (u.nastinator57) return TRUE;
	if (Role_if(PM_FEMINIST) && u.urmaxlvlUP >= 15 && u.femauspices15 == 3) return TRUE;
	return(FALSE);
}

boolean
have_egostone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == EGOSTONE)
			return(TRUE);
		}
	if (u.nastinator58) return TRUE;
	return(FALSE);
}

boolean
have_fastforwardstone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == FAST_FORWARD_STONE)
			return(TRUE);
		}
	if (u.nastinator59) return TRUE;
	return(FALSE);
}

boolean
have_rottenstone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == ROTTEN_STONE)
			return(TRUE);
		}
	if (u.nastinator60) return TRUE;
	return(FALSE);
}

boolean
have_unskilledstone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == UNSKILLED_STONE)
			return(TRUE);
		}
	if (u.nastinator61) return TRUE;
	return(FALSE);
}

boolean
have_lowstatstone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == LOW_STAT_STONE)
			return(TRUE);
		}
	if (u.nastinator62) return TRUE;
	return(FALSE);
}

boolean
have_trainingstone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == TRAINING_STONE)
			return(TRUE);
		}
	if (u.nastinator63) return TRUE;
	return(FALSE);
}

boolean
have_exercisestone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == EXERCISE_STONE)
			return(TRUE);
		}
	if (u.nastinator64) return TRUE;
	return(FALSE);
}

boolean
have_limitationstone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == TURN_LIMIT_STONE)
			return(TRUE);
		}
	if (u.nastinator65) return TRUE;
	return(FALSE);
}

boolean
have_weaksightstone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == WEAK_SIGHT_STONE)
			return(TRUE);
		}
	if (u.nastinator66) return TRUE;
	if (Role_if(PM_FEMINIST) && u.urmaxlvlUP >= 6 && u.femauspices6 == 5) return TRUE;
	return(FALSE);
}

boolean
have_messagestone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == CHATTER_STONE)
			return(TRUE);
		}
	if (u.nastinator67) return TRUE;
	return(FALSE);
}

boolean
have_nonsacredstone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == NONSACRED_STONE)
			return(TRUE);
		}
	if (u.nastinator68) return TRUE;
	if (Role_if(PM_FEMINIST) && u.urmaxlvlUP >= 22 && u.femauspices22 == 3) return TRUE;
	return(FALSE);
}

boolean
have_starvationstone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == STARVATION_STONE)
			return(TRUE);
		}
	if (u.nastinator69) return TRUE;
	if (Role_if(PM_FEMINIST) && u.urmaxlvlUP >= 30 && u.femauspices30 == 2) return TRUE;
	return(FALSE);
}

boolean
have_droplessstone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == DROPLESS_STONE)
			return(TRUE);
		}
	if (u.nastinator70) return TRUE;
	return(FALSE);
}

boolean
have_loweffectstone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == LOW_EFFECT_STONE)
			return(TRUE);
		}
	if (u.nastinator71) return TRUE;
	if (Role_if(PM_FEMINIST) && u.urmaxlvlUP >= 4 && u.femauspices4 == 6) return TRUE;
	return(FALSE);
}

boolean
have_invisostone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == INVISO_STONE)
			return(TRUE);
		}
	if (u.nastinator72) return TRUE;
	if (Role_if(PM_FEMINIST) && u.urmaxlvlUP >= 22 && u.femauspices22 == 5) return TRUE;
	return(FALSE);
}

boolean
have_ghostlystone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == GHOSTLY_STONE)
			return(TRUE);
		}
	if (u.nastinator73) return TRUE;
	if (Role_if(PM_FEMINIST) && u.urmaxlvlUP >= 22 && u.femauspices22 == 4) return TRUE;
	return(FALSE);
}

boolean
have_dehydratingstone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == DEHYDRATING_STONE)
			return(TRUE);
		}
	if (u.nastinator74) return TRUE;
	return(FALSE);
}

boolean
have_hatestone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == STONE_OF_HATE)
			return(TRUE);
		}
	if (u.nastinator75) return TRUE;
	return(FALSE);
}

boolean
have_directionswapstone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == DIRECTIONAL_SWAP_STONE)
			return(TRUE);
		}
	if (u.nastinator76) return TRUE;
	if (Role_if(PM_FEMINIST) && u.urmaxlvlUP >= 30 && u.femauspices30 == 3) return TRUE;
	return(FALSE);
}

boolean
have_nonintrinsicstone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == NONINTRINSICAL_STONE)
			return(TRUE);
		}
	if (u.nastinator77) return TRUE;
	return(FALSE);
}

boolean
have_dropcursestone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == DROPCURSE_STONE)
			return(TRUE);
		}
	if (u.nastinator78) return TRUE;
	if (Role_if(PM_FEMINIST) && u.urmaxlvlUP >= 15 && u.femauspices15 == 4) return TRUE;
	return(FALSE);
}

boolean
have_nakedstone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == STONE_OF_NAKED_STRIPPING)
			return(TRUE);
		}
	if (u.nastinator79) return TRUE;
	return(FALSE);
}

boolean
have_antilevelstone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == ANTILEVEL_STONE)
			return(TRUE);
		}
	if (u.nastinator80) return TRUE;
	return(FALSE);
}

boolean
have_stealerstone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == STEALER_STONE)
			return(TRUE);
		}
	if (u.nastinator81) return TRUE;
	return(FALSE);
}

boolean
have_rebelstone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == REBEL_STONE)
			return(TRUE);
		}
	if (u.nastinator82) return TRUE;
	return(FALSE);
}

boolean
have_shitstone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == SHIT_STONE)
			return(TRUE);
		}
	if (u.nastinator83) return TRUE;
	return(FALSE);
}

boolean
have_misfirestone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == STONE_OF_MISFIRING)
			return(TRUE);
		}
	if (u.nastinator84) return TRUE;
	return(FALSE);
}

boolean
have_wallstone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == STONE_OF_PERMANENCE)
			return(TRUE);
		}
	if (u.nastinator85) return TRUE;
	if (Role_if(PM_FEMINIST) && u.urmaxlvlUP >= 22 && u.femauspices22 == 6) return TRUE;
	return(FALSE);
}

boolean
have_disconnectstone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == DISCONNECT_STONE)
			return(TRUE);
		}
	if (u.nastinator86) return TRUE;
	return(FALSE);
}

boolean
have_interfacescrewstone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == SCREW_STONE)
			return(TRUE);
		}
	if (u.nastinator87) return TRUE;
	if (Role_if(PM_FEMINIST) && u.urmaxlvlUP >= 30 && u.femauspices30 == 4) return TRUE;
	return(FALSE);
}

boolean
have_bossfightstone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == BOSSFIGHT_STONE)
			return(TRUE);
		}
	if (u.nastinator88) return TRUE;
	if (Role_if(PM_FEMINIST) && u.urmaxlvlUP >= 17 && u.femauspices17 == 3) return TRUE;
	return(FALSE);
}

boolean
have_entirelevelstone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == ENTIRE_LEVEL_STONE)
			return(TRUE);
		}
	if (u.nastinator89) return TRUE;
	if (Role_if(PM_FEMINIST) && u.urmaxlvlUP >= 17 && u.femauspices17 == 4) return TRUE;
	return(FALSE);
}

boolean
have_bonestone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == BONE_STONE)
			return(TRUE);
		}
	if (u.nastinator90) return TRUE;
	return(FALSE);
}

boolean
have_autocursestone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == AUTOCURSE_STONE)
			return(TRUE);
		}
	if (u.nastinator91) return TRUE;
	if (Role_if(PM_FEMINIST) && u.urmaxlvlUP >= 28 && u.femauspices28 == 4) return TRUE;
	return(FALSE);
}

boolean
have_highlevelstone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == HIGHLEVEL_STONE)
			return(TRUE);
		}
	if (u.nastinator92) return TRUE;
	if (Role_if(PM_FEMINIST) && u.urmaxlvlUP >= 17 && u.femauspices17 == 5) return TRUE;
	return(FALSE);
}

boolean
have_spellforgettingstone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == SPELL_MEMORY_STONE)
			return(TRUE);
		}
	if (u.nastinator93) return TRUE;
	return(FALSE);
}

boolean
have_soundeffectstone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == SOUND_EFFECT_STONE)
			return(TRUE);
		}
	if (u.nastinator94) return TRUE;
	if (Role_if(PM_FEMINIST) && u.urmaxlvlUP >= 8 && u.femauspices8 == 5) return TRUE;
	return(FALSE);
}

boolean
have_timerunstone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == TIME_USE_STONE)
			return(TRUE);
		}
	if (u.nastinator95) return TRUE;
	return(FALSE);
}

boolean
have_lootcutstone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == LOOTCUT_STONE)
			return(TRUE);
		}
	if (u.nastinator96) return TRUE;
	return(FALSE);
}

boolean
have_monsterspeedstone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == MONSTER_SPEED_STONE)
			return(TRUE);
		}
	if (u.nastinator97) return TRUE;
	if (Role_if(PM_FEMINIST) && u.urmaxlvlUP >= 15 && u.femauspices15 == 5) return TRUE;
	return(FALSE);
}

boolean
have_scalingstone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == SCALING_STONE)
			return(TRUE);
		}
	if (u.nastinator98) return TRUE;
	if (Role_if(PM_FEMINIST) && u.urmaxlvlUP >= 17 && u.femauspices17 == 6) return TRUE;
	return(FALSE);
}

boolean
have_inimicalstone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == INIMICAL_STONE)
			return(TRUE);
		}
	if (u.nastinator99) return TRUE;
	if (Role_if(PM_FEMINIST) && u.urmaxlvlUP >= 13 && u.femauspices13 == 4) return TRUE;
	return(FALSE);
}

boolean
have_whitespellstone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == WHITE_SPELL_STONE)
			return(TRUE);
		}
	if (u.nastinator100) return TRUE;
	if (Role_if(PM_FEMINIST) && u.urmaxlvlUP >= 11 && u.femauspices11 == 2) return TRUE;
	return(FALSE);
}

boolean
have_greyoutstone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == GREYOUT_STONE)
			return(TRUE);
		}
	if (u.nastinator101) return TRUE;
	if (Role_if(PM_FEMINIST) && u.urmaxlvlUP >= 11 && u.femauspices11 == 3) return TRUE;
	return(FALSE);
}

boolean
have_quasarstone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == QUASAR_STONE)
			return(TRUE);
		}
	if (u.nastinator102) return TRUE;
	if (Role_if(PM_FEMINIST) && u.urmaxlvlUP >= 30 && u.femauspices30 == 5) return TRUE;
	return(FALSE);
}

boolean
have_mommystone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == MOMMY_STONE)
			return(TRUE);
		}
	if (u.nastinator103) return TRUE;
	return(FALSE);
}

boolean
have_horrorstone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == HORROR_STONE)
			return(TRUE);
		}
	if (u.nastinator104) return TRUE;
	return(FALSE);
}

boolean
have_artificialstone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == ARTIFICIAL_STONE)
			return(TRUE);
		}
	if (u.nastinator105) return TRUE;
	return(FALSE);
}

boolean
have_wereformstone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == WEREFORM_STONE)
			return(TRUE);
		}
	if (u.nastinator106) return TRUE;
	return(FALSE);
}

boolean
have_antiprayerstone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == ANTIPRAYER_STONE)
			return(TRUE);
		}
	if (u.nastinator107) return TRUE;
	return(FALSE);
}

boolean
have_evilpatchstone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == EVIL_PATCH_STONE)
			return(TRUE);
		}
	if (u.nastinator108) return TRUE;
	return(FALSE);
}

boolean
have_hardmodestone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == HARD_MODE_STONE)
			return(TRUE);
		}
	if (u.nastinator109) return TRUE;
	return(FALSE);
}

boolean
have_secretattackstone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == SECRET_ATTACK_STONE)
			return(TRUE);
		}
	if (u.nastinator110) return TRUE;
	if (Role_if(PM_FEMINIST) && u.urmaxlvlUP >= 28 && u.femauspices28 == 5) return TRUE;
	return(FALSE);
}

boolean
have_eaterstone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == EATER_STONE)
			return(TRUE);
		}
	if (u.nastinator111) return TRUE;
	return(FALSE);
}

boolean
have_covetousstone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == COVETOUS_STONE)
			return(TRUE);
		}
	if (u.nastinator112) return TRUE;
	if (Role_if(PM_FEMINIST) && u.urmaxlvlUP >= 13 && u.femauspices13 == 5) return TRUE;
	return(FALSE);
}

boolean
have_nonseeingstone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == NON_SEEING_STONE)
			return(TRUE);
		}
	if (u.nastinator113) return TRUE;
	if (Role_if(PM_FEMINIST) && u.urmaxlvlUP >= 20 && u.femauspices20 == 5) return TRUE;
	return(FALSE);
}

boolean
have_darkmodestone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == DARKMODE_STONE)
			return(TRUE);
		}
	if (u.nastinator114) return TRUE;
	return(FALSE);
}

boolean
have_unfindablestone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == UNFINDABLE_STONE)
			return(TRUE);
		}
	if (u.nastinator115) return TRUE;
	return(FALSE);
}

boolean
have_homicidestone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == HOMICIDE_STONE)
			return(TRUE);
		}
	if (u.nastinator116) return TRUE;
	return(FALSE);
}

boolean
have_multitrappingstone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == MULTITRAPPING_STONE)
			return(TRUE);
		}
	if (u.nastinator117) return TRUE;
	return(FALSE);
}

boolean
have_wakeupcallstone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == WAKEUP_CALL_STONE)
			return(TRUE);
		}
	if (u.nastinator118) return TRUE;
	return(FALSE);
}

boolean
have_grayoutstone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == GRAYOUT_STONE)
			return(TRUE);
		}
	if (u.nastinator119) return TRUE;
	if (Role_if(PM_FEMINIST) && u.urmaxlvlUP >= 25 && u.femauspices25 == 1) return TRUE;
	return(FALSE);
}

boolean
have_graycenterstone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == GRAY_CENTER_STONE)
			return(TRUE);
		}
	if (u.nastinator120) return TRUE;
	if (Role_if(PM_FEMINIST) && u.urmaxlvlUP >= 25 && u.femauspices25 == 2) return TRUE;
	return(FALSE);
}

boolean
have_checkerboardstone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == CHECKERBOARD_STONE)
			return(TRUE);
		}
	if (u.nastinator121) return TRUE;
	if (Role_if(PM_FEMINIST) && u.urmaxlvlUP >= 25 && u.femauspices25 == 3) return TRUE;
	return(FALSE);
}

boolean
have_clockwisestone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == CLOCKWISE_STONE)
			return(TRUE);
		}
	if (u.nastinator122) return TRUE;
	if (Role_if(PM_FEMINIST) && u.urmaxlvlUP >= 30 && u.femauspices30 == 6) return TRUE;
	return(FALSE);
}

boolean
have_counterclockwisestone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == COUNTERCLOCKWISE_STONE)
			return(TRUE);
		}
	if (u.nastinator123) return TRUE;
	if (Role_if(PM_FEMINIST) && u.urmaxlvlUP >= 30 && u.femauspices30 == 7) return TRUE;
	return(FALSE);
}

boolean
have_lagstone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == LAG_STONE)
			return(TRUE);
		}
	if (u.nastinator124) return TRUE;
	return(FALSE);
}

boolean
have_blesscursestone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == BLESSCURSE_STONE)
			return(TRUE);
		}
	if (Role_if(PM_FEMINIST) && u.urmaxlvlUP >= 15 && u.femauspices15 == 6) return TRUE;
	if (u.nastinator125) return TRUE;
	return(FALSE);
}

boolean
have_delightstone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == DELIGHT_STONE)
			return(TRUE);
		}
	if (u.nastinator126) return TRUE;
	return(FALSE);
}

boolean
have_dischargestone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == DISCHARGE_STONE)
			return(TRUE);
		}
	if (u.nastinator127) return TRUE;
	return(FALSE);
}

boolean
have_trashstone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == TRASH_STONE)
			return(TRUE);
		}
	if (u.nastinator128) return TRUE;
	if (Role_if(PM_FEMINIST) && u.urmaxlvlUP >= 28 && u.femauspices28 == 6) return TRUE;
	return(FALSE);
}

boolean
have_filteringstone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == FILTERING_STONE)
			return(TRUE);
		}
	if (u.nastinator129) return TRUE;
	return(FALSE);
}

boolean
have_deformattingstone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == DEFORMATTING_STONE)
			return(TRUE);
		}
	if (u.nastinator130) return TRUE;
	return(FALSE);
}

boolean
have_flickerstripstone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == FLICKER_STRIP_STONE)
			return(TRUE);
		}
	if (u.nastinator131) return TRUE;
	if (Role_if(PM_FEMINIST) && u.urmaxlvlUP >= 6 && u.femauspices6 == 6) return TRUE;
	return(FALSE);
}

boolean
have_undressingstone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == UNDRESSING_STONE)
			return(TRUE);
		}
	if (u.nastinator132) return TRUE;
	return(FALSE);
}

boolean
have_hyperbluestone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == HYPER_BLUE_STONE)
			return(TRUE);
		}
	if (u.nastinator133) return TRUE;
	if (Role_if(PM_FEMINIST) && u.urmaxlvlUP >= 8 && u.femauspices8 == 6) return TRUE;
	return(FALSE);
}

boolean
have_nolightstone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == NO_LIGHT_STONE)
			return(TRUE);
		}
	if (u.nastinator134) return TRUE;
	return(FALSE);
}

boolean
have_paranoiastone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == PARANOIA_STONE)
			return(TRUE);
		}
	if (u.nastinator135) return TRUE;
	return(FALSE);
}

boolean
have_fleecestone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == FLEECE_STONE)
			return(TRUE);
		}
	if (u.nastinator136) return TRUE;
	if (Role_if(PM_FEMINIST) && u.urmaxlvlUP >= 6 && u.femauspices6 == 7) return TRUE;
	return(FALSE);
}

boolean
have_interruptionstone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == INTERRUPTION_STONE)
			return(TRUE);
		}
	if (u.nastinator137) return TRUE;
	return(FALSE);
}

boolean
have_dustbinstone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == DUSTBIN_STONE)
			return(TRUE);
		}
	if (u.nastinator138) return TRUE;
	return(FALSE);
}

boolean
have_batterystone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == BATTERY_STONE)
			return(TRUE);
		}
	if (u.nastinator139) return TRUE;
	return(FALSE);
}

boolean
have_butterfingerstone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == BUTTERFINGER_STONE)
			return(TRUE);
		}
	if (u.nastinator140) return TRUE;
	return(FALSE);
}

boolean
have_miscastingstone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == MISCASTING_STONE)
			return(TRUE);
		}
	if (u.nastinator141) return TRUE;
	return(FALSE);
}

boolean
have_messagesuppressionstone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == MESSAGE_SUPPRESSION_STONE)
			return(TRUE);
		}
	if (u.nastinator142) return TRUE;
	return(FALSE);
}

boolean
have_stuckannouncementstone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == STUCK_ANNOUNCEMENT_STONE)
			return(TRUE);
		}
	if (u.nastinator143) return TRUE;
	return(FALSE);
}

boolean
have_stormstone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == STORM_STONE)
			return(TRUE);
		}
	if (u.nastinator144) return TRUE;
	return(FALSE);
}

boolean
have_maximumdamagestone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == MAXIMUM_DAMAGE_STONE)
			return(TRUE);
		}
	if (u.nastinator145) return TRUE;
	return(FALSE);
}

boolean
have_latencystone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == LATENCY_STONE)
			return(TRUE);
		}
	if (u.nastinator146) return TRUE;
	return(FALSE);
}

boolean
have_starlitskystone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == STARLIT_SKY_STONE)
			return(TRUE);
		}
	if (u.nastinator147) return TRUE;
	if (Role_if(PM_FEMINIST) && u.urmaxlvlUP >= 25 && u.femauspices25 == 5) return TRUE;
	return(FALSE);
}

boolean
have_trapknowledgestone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == TRAP_KNOWLEDGE_STONE)
			return(TRUE);
		}
	if (u.nastinator148) return TRUE;
	if (Role_if(PM_FEMINIST) && u.urmaxlvlUP >= 25 && u.femauspices25 == 4) return TRUE;
	return(FALSE);
}

boolean
have_highscorestone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == HIGHSCORE_STONE)
			return(TRUE);
		}
	if (u.nastinator149) return TRUE;
	return(FALSE);
}

boolean
have_pinkspellstone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == PINK_SPELL_STONE)
			return(TRUE);
		}
	if (u.nastinator150) return TRUE;
	if (Role_if(PM_FEMINIST) && u.urmaxlvlUP >= 11 && u.femauspices11 == 9) return TRUE;
	return(FALSE);
}

boolean
have_greenspellstone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == GREEN_SPELL_STONE)
			return(TRUE);
		}
	if (u.nastinator151) return TRUE;
	if (Role_if(PM_FEMINIST) && u.urmaxlvlUP >= 11 && u.femauspices11 == 5) return TRUE;
	return(FALSE);
}

boolean
have_evcstone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == EVC_STONE)
			return(TRUE);
		}
	if (u.nastinator152) return TRUE;
	if (Role_if(PM_FEMINIST) && u.urmaxlvlUP >= 20 && u.femauspices20 == 6) return TRUE;
	return(FALSE);
}

boolean
have_underlaidstone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == UNDERLAID_STONE)
			return(TRUE);
		}
	if (u.nastinator153) return TRUE;
	if (Role_if(PM_FEMINIST) && u.urmaxlvlUP >= 20 && u.femauspices20 == 7) return TRUE;
	return(FALSE);
}

boolean
have_damagemeterstone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == DAMAGE_METER_STONE)
			return(TRUE);
		}
	if (u.nastinator154) return TRUE;
	return(FALSE);
}

boolean
have_weightstone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == WEIGHT_STONE)
			return(TRUE);
		}
	if (u.nastinator155) return TRUE;
	return(FALSE);
}

boolean
have_infofuckstone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == INFOFUCK_STONE)
			return(TRUE);
		}
	if (u.nastinator156) return TRUE;
	return(FALSE);
}

boolean
have_blackspellstone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == BLACK_SPELL_STONE)
			return(TRUE);
		}
	if (u.nastinator157) return TRUE;
	if (Role_if(PM_FEMINIST) && u.urmaxlvlUP >= 11 && u.femauspices11 == 7) return TRUE;
	return(FALSE);
}

boolean
have_cyanspellstone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == CYAN_SPELL_STONE)
			return(TRUE);
		}
	if (u.nastinator158) return TRUE;
	if (Role_if(PM_FEMINIST) && u.urmaxlvlUP >= 11 && u.femauspices11 == 8) return TRUE;
	return(FALSE);
}

boolean
have_heapstone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == HEAP_STONE)
			return(TRUE);
		}
	if (u.nastinator159) return TRUE;
	return(FALSE);
}

boolean
have_bluespellstone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == BLUE_SPELL_STONE)
			return(TRUE);
		}
	if (u.nastinator160) return TRUE;
	if (Role_if(PM_FEMINIST) && u.urmaxlvlUP >= 11 && u.femauspices11 == 4) return TRUE;
	return(FALSE);
}

boolean
have_tronstone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == TRON_STONE)
			return(TRUE);
		}
	if (u.nastinator161) return TRUE;
	return(FALSE);
}

boolean
have_redspellstone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == RED_SPELL_STONE)
			return(TRUE);
		}
	if (u.nastinator162) return TRUE;
	if (Role_if(PM_FEMINIST) && u.urmaxlvlUP >= 11 && u.femauspices11 == 6) return TRUE;
	return(FALSE);
}

boolean
have_tooheavystone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == TOO_HEAVY_STONE)
			return(TRUE);
		}
	if (u.nastinator163) return TRUE;
	return(FALSE);
}

boolean
have_elongatedstone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == ELONGATED_STONE)
			return(TRUE);
		}
	if (u.nastinator164) return TRUE;
	if (Role_if(PM_FEMINIST) && u.urmaxlvlUP >= 28 && u.femauspices28 == 7) return TRUE;
	return(FALSE);
}

boolean
have_wrapoverstone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == WRAPOVER_STONE)
			return(TRUE);
		}
	if (u.nastinator165) return TRUE;
	return(FALSE);
}

boolean
have_destructionstone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == DESTRUCTION_STONE)
			return(TRUE);
		}
	if (u.nastinator166) return TRUE;
	return(FALSE);
}

boolean
have_meleeprefixstone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == MELEE_PREFIX_STONE)
			return(TRUE);
		}
	if (u.nastinator167) return TRUE;
	if (Role_if(PM_FEMINIST) && u.urmaxlvlUP >= 30 && u.femauspices30 == 8) return TRUE;
	return(FALSE);
}

boolean
have_automorestone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == AUTOMORE_STONE)
			return(TRUE);
		}
	if (u.nastinator168) return TRUE;
	return(FALSE);
}

boolean
have_unfairattackstone()
{
	register struct obj *otmp;

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		if(otmp->otyp == UNFAIR_ATTACK_STONE)
			return(TRUE);
		}
	if (u.nastinator169) return TRUE;
	if (Role_if(PM_FEMINIST) && u.urmaxlvlUP >= 28 && u.femauspices28 == 8) return TRUE;
	return(FALSE);
}

struct obj *
o_on(id, objchn)
unsigned int id;
register struct obj *objchn;
{
	struct obj *temp;

	while(objchn) {
		if(objchn->o_id == id) return(objchn);
		if (Has_contents(objchn) && (temp = o_on(id,objchn->cobj)))
			return temp;
		objchn = objchn->nobj;
	}
	return((struct obj *) 0);
}

boolean
obj_here(obj, x, y)
register struct obj *obj;
int x, y;
{
	register struct obj *otmp;

	for(otmp = level.objects[x][y]; otmp; otmp = otmp->nexthere)
		if(obj == otmp) return(TRUE);
	return(FALSE);
}

#endif /* OVLB */
#ifdef OVL2

struct obj *
g_at(x,y)
register int x, y;
{
	register struct obj *obj = level.objects[x][y];
	while(obj) {
	    if ((obj->oclass == COIN_CLASS) && !(obj == uchain) && !(obj == uball) ) return obj;
	    obj = obj->nexthere;
	}
	return((struct obj *)0);
}

#endif /* OVL2 */
#ifdef OVLB
#ifndef GOLDOBJ
/* Make a gold object from the hero's gold. */
struct obj *
mkgoldobj(q)
register long q;
{
	register struct obj *otmp;

	otmp = mksobj(GOLD_PIECE, FALSE, FALSE);
	u.ugold -= q;
	otmp->quan = q;
	otmp->owt = weight(otmp);
	flags.botl = 1;
	return(otmp);
}
#endif
#endif /* OVLB */
#ifdef OVL1

STATIC_OVL void
compactify(buf)
register char *buf;
/* compact a string of inventory letters by dashing runs of letters */
{
	register int i1 = 1, i2 = 1;
	register char ilet, ilet1, ilet2;

	ilet2 = buf[0];
	ilet1 = buf[1];
	buf[++i2] = buf[++i1];
	ilet = buf[i1];
	while(ilet) {
		if(ilet == ilet1+1) {
			if(ilet1 == ilet2+1)
				buf[i2 - 1] = ilet1 = '-';
			else if(ilet2 == '-') {
				buf[i2 - 1] = ++ilet1;
				buf[i2] = buf[++i1];
				ilet = buf[i1];
				continue;
			}
		}
		ilet2 = ilet1;
		ilet1 = ilet;
		buf[++i2] = buf[++i1];
		ilet = buf[i1];
	}
}

/* match the prompt for either 'T' or 'R' command */
STATIC_OVL boolean
taking_off(action)
const char *action;
{
    return !strcmp(action, "take off") || !strcmp(action, "remove");
}

/* match the prompt for either 'W' or 'P' command */
STATIC_OVL boolean
putting_on(action)
const char *action;
{
    return !strcmp(action, "wear") || !strcmp(action, "put on");
}

STATIC_OVL int
ugly_checks(let, word, otmp)
const char *let, *word;
struct obj *otmp;
{
		register int otyp = otmp->otyp;
		/* ugly check: remove inappropriate things */
		if((taking_off(word) &&
		    (!(otmp->owornmask & (W_ARMOR | W_RING | W_AMUL | W_IMPLANT | W_TOOL))
		     || (otmp==uarm && uarmc)
		     || (otmp==uarmu && (uarm || uarmc))
		    ))
		|| (putting_on(word) &&
		     (otmp->owornmask & (W_ARMOR | W_RING | W_AMUL | W_IMPLANT | W_TOOL)))
							/* already worn */
#if 0	/* 3.4.1 -- include currently wielded weapon among the choices */
		|| (!strcmp(word, "wield") &&
		    (otmp->owornmask & W_WEP))
#endif
		|| (!strcmp(word, "ready") &&
		    (otmp == uwep || (otmp == uswapwep && u.twoweap)))
		    ) {
			return 1;
		}

		/* Second ugly check; unlike the first it won't trigger an
		 * "else" in "you don't have anything else to ___".
		 */
                else if ((putting_on(word) &&
		    ((otmp->oclass == FOOD_CLASS && otmp->otyp != MEAT_RING) ||
		    (otmp->oclass == TOOL_CLASS &&
		     otyp != BLINDFOLD && otyp != EYECLOSER && otyp != DRAGON_EYEPATCH && otyp != CONDOME && otyp != SOFT_CHASTITY_BELT && otyp != TOWEL && otyp != LENSES && otyp != RADIOGLASSES && otyp != BOSS_VISOR)))
/*add check for improving*/
                || ( (!strcmp(word, "wield") || !strcmp(word, "improve")) &&
		    (otmp->oclass == TOOL_CLASS && !is_weptool(otmp)))
		|| (!strcmp(word, "eat") && !is_edible(otmp))
		|| (!strcmp(word, "revive") && otyp != CORPSE) /* revive */
		|| (!strcmp(word, "sacrifice") &&
		    (otyp != CORPSE &&
		     otyp != SEVERED_HAND &&                    
		     otyp != EYEBALL &&	/* KMH -- fixed */
		     otyp != AMULET_OF_YENDOR && otyp != FAKE_AMULET_OF_YENDOR))
		|| (!strcmp(word, "write with") &&
		    (otmp->oclass == TOOL_CLASS &&
		     (!is_lightsaber(otmp) || !otmp->lamplit) &&
		     otyp != MAGIC_MARKER && otyp != FELT_TIP_MARKER && otyp != TOWEL))
		|| (!strcmp(word, "tin") &&
		    (otyp != CORPSE || !tinnable(otmp)))
		|| (!strcmp(word, "rub") &&
		    ((otmp->oclass == TOOL_CLASS &&
		      otyp != OIL_LAMP && otyp != MAGIC_LAMP &&
		      otyp != BRASS_LANTERN) ||
		     (otmp->oclass == GEM_CLASS && !is_graystone(otmp))))
		|| (!strncmp(word, "rub on the stone", 16) &&
		    *let == GEM_CLASS &&	/* using known touchstone */
		    otmp->dknown && objects[otyp].oc_name_known)
		|| ((!strcmp(word, "use or apply") ||
			!strcmp(word, "untrap with")) &&
		     /* Picks, axes, pole-weapons, bullwhips */
		    ((otmp->oclass == WEAPON_CLASS && !is_pick(otmp) &&
		      otyp != SUBMACHINE_GUN &&
		      otyp != DEMON_CROSSBOW &&
		      otyp != AUTO_SHOTGUN &&
		      otyp != ASSAULT_RIFLE &&
		      otyp != FRAG_GRENADE &&
		      otyp != GAS_GRENADE &&
		      otyp != STICK_OF_DYNAMITE &&
		      !is_axe(otmp) && !is_antibar(otmp) && !is_pole(otmp) && otyp != BULLWHIP) ||
		    (otmp->oclass == POTION_CLASS &&
		     /* only applicable potion is oil, and it will only
			be offered as a choice when already discovered */
		     (otyp != POT_OIL || !otmp->dknown ||
		      !objects[POT_OIL].oc_name_known) &&
		      /* water is only for untrapping */
		     (strcmp(word, "untrap with") || 
		      otyp != POT_WATER || !otmp->dknown ||
		      !objects[POT_WATER].oc_name_known)) ||
		     (otmp->oclass == FOOD_CLASS &&
		      otyp != CREAM_PIE && otyp != EUCALYPTUS_LEAF) ||
		     (otmp->oclass == GEM_CLASS && !is_graystone(otmp))))
		|| (!strcmp(word, "invoke") &&
		    (!otmp->oartifact && !otmp->fakeartifact && !objects[otyp].oc_unique &&
		     (otyp != FAKE_AMULET_OF_YENDOR || otmp->known) &&
		     otyp != CRYSTAL_BALL &&	/* #invoke synonym for apply */
		   /* note: presenting the possibility of invoking non-artifact
		      mirrors and/or lamps is a simply a cruel deception... */
		     otyp != MIRROR && otyp != MAGIC_LAMP &&
		     (otyp != OIL_LAMP ||	/* don't list known oil lamp */
		      (otmp->dknown && objects[OIL_LAMP].oc_name_known))))
		|| (!strcmp(word, "untrap with") &&
		    (otmp->oclass == TOOL_CLASS && otyp != CAN_OF_GREASE && otyp != LUBRICANT_CAN))
		|| (!strcmp(word, "charge") && !is_chargeable(otmp))
		|| (!strcmp(word, "randomly enchant") && !is_enchantable(otmp))
		|| (!strcmp(word, "poison") && !is_poisonable(otmp))
		|| (!strcmp(word, "rustproof") && objects[(otmp)->otyp].oc_material == IRON)
		|| (!strcmp(word, "magically enchant") && !(otmp->owornmask & W_ARMOR) )
		|| ((!strcmp(word, "draw blood with") ||
			!strcmp(word, "bandage your wounds with")) &&
		    (otmp->oclass == TOOL_CLASS && otyp != MEDICAL_KIT))
		    )
			return 2;
		else
		    return 0;
}

/* List of valid classes for allow_ugly callback */
static char valid_ugly_classes[MAXOCLASSES + 1];

/* Action word for allow_ugly callback */
static const char *ugly_word;

STATIC_OVL boolean
allow_ugly(obj)
struct obj *obj;
{
    return index(valid_ugly_classes, obj->oclass) &&
	   !ugly_checks(valid_ugly_classes, ugly_word, obj);
}

/*
 * getobj returns:
 *	struct obj *xxx:	object to do something with.
 *	(struct obj *) 0	error return: no object.
 *	&zeroobj		explicitly no object (as in w-).
 *	&thisplace		this place (as in r.).
#ifdef GOLDOBJ
!!!! test if gold can be used in unusual ways (eaten etc.)
!!!! may be able to remove "usegold"
#endif
 */
struct obj *
getobj(let,word)
register const char *let,*word;
{
	register struct obj *otmp;
	register char ilet;
	char buf[BUFSZ], qbuf[QBUFSZ];
	char lets[BUFSZ], altlets[BUFSZ], *ap;
	register int foo = 0;
	register char *bp = buf;
	xchar allowcnt = 0;	/* 0, 1 or 2 */
#ifndef GOLDOBJ
	boolean allowgold = FALSE;	/* can't use gold because they don't have any */
#endif
	boolean usegold = FALSE;	/* can't use gold because its illegal */
	boolean allowall = FALSE;
	boolean allownone = FALSE;
	boolean allowfloor = FALSE;
	boolean usefloor = FALSE;
	boolean allowthisplace = FALSE;
	boolean useboulder = FALSE;
	xchar foox = 0;
	long cnt, prevcnt;
	boolean prezero;
	long dummymask;
	int ugly;
	struct obj *floorchain;
	int floorfollow;

	if(*let == ALLOW_COUNT) let++, allowcnt = 1;
#ifndef GOLDOBJ
	if(*let == COIN_CLASS) let++,
		usegold = TRUE, allowgold = (u.ugold ? TRUE : FALSE);
#else
	if(*let == COIN_CLASS) let++, usegold = TRUE;
#endif

	/* Equivalent of an "ugly check" for gold */
	if (usegold && !strcmp(word, "eat") &&
	    (!metallivorous(youmonst.data)
	     || youmonst.data == &mons[PM_RUST_MONSTER]))
#ifndef GOLDOBJ
		usegold = allowgold = FALSE;
#else
		usegold = FALSE;
#endif

	if(*let == ALL_CLASSES) let++, allowall = TRUE;
	if(*let == ALLOW_NONE) let++, allownone = TRUE;
	if(*let == ALLOW_FLOOROBJ) {
	    let++;
	    if (!u.uswallow) {
		floorchain = can_reach_floorobj() ? level.objects[u.ux][u.uy] :
			     (struct obj *)0;
		floorfollow = BY_NEXTHERE;
	    } else {
		floorchain = u.ustuck->minvent;
		floorfollow = 0;		/* nobj */
	    }
	    usefloor = TRUE;
	    allowfloor = !!floorchain;
	}
	if(*let == ALLOW_THISPLACE) let++, allowthisplace = TRUE;
	/* "ugly check" for reading fortune cookies, part 1 */
	/* The normal 'ugly check' keeps the object on the inventory list.
	 * We don't want to do that for shirts/cookies, so the check for
	 * them is handled a bit differently (and also requires that we set
	 * allowall in the caller)
	 */
	if(allowall && !strcmp(word, "read")) allowall = FALSE;

	/* another ugly check: show boulders (not statues) */
	if(*let == WEAPON_CLASS &&
	   !strcmp(word, "throw") && throws_rocks(youmonst.data))
	    useboulder = TRUE;

	if(allownone) *bp++ = '-';
#ifndef GOLDOBJ
	if(allowgold) *bp++ = def_oc_syms[COIN_CLASS];
#endif
	if(bp > buf && bp[-1] == '-') *bp++ = ' ';
	ap = altlets;

	ilet = 'a';

	for (otmp = invent; otmp; otmp = otmp->nobj) {
	    if (!flags.invlet_constant)
#ifdef GOLDOBJ
		if (otmp->invlet != GOLD_SYM) /* don't reassign this */
#endif
		otmp->invlet = ilet;	/* reassign() */
	    if (!*let || index(let, otmp->oclass)
#ifdef GOLDOBJ
		|| (usegold && otmp->invlet == GOLD_SYM)
#endif
		|| (useboulder && otmp->otyp == BOULDER)
		) {
		bp[foo++] = otmp->invlet;

		/* ugly checks */
		ugly = ugly_checks(let, word, otmp);
		if (ugly == 1) {
		    foo--;
		    foox++;
		} else if (ugly == 2)
		    foo--;
		/* ugly check for unworn armor that can't be worn */
		else if (putting_on(word) && *let == ARMOR_CLASS &&
			 !canwearobj(otmp, &dummymask, FALSE)) {
			foo--;
			allowall = TRUE;
			*ap++ = otmp->invlet;
		}
	    } else {

		/* "ugly check" for reading fortune cookies, part 2 */
		if ((!strcmp(word, "read") &&
		    (otmp->otyp == FORTUNE_COOKIE
			|| otmp->otyp == T_SHIRT
			|| otmp->otyp == STRIPED_SHIRT
			|| otmp->otyp == PRINTED_SHIRT
			|| otmp->otyp == BATH_TOWEL
			|| otmp->otyp == PLUGSUIT
			|| otmp->otyp == SWIMSUIT
			|| otmp->otyp == MEN_S_UNDERWEAR
			|| otmp->otyp == HAWAIIAN_SHIRT
			|| otmp->otyp == BLACK_DRESS
			|| otmp->otyp == CHANTER_SHIRT
			|| otmp->otyp == BAD_SHIRT
			|| otmp->otyp == BODYGLOVE
			|| otmp->otyp == BEAUTIFUL_SHIRT
			|| otmp->otyp == PETA_COMPLIANT_SHIRT
			|| otmp->otyp == RADIOACTIVE_UNDERGARMENT
			|| otmp->otyp == KYRT_SHIRT
			|| otmp->otyp == WOOLEN_SHIRT
			|| otmp->otyp == RUFFLED_SHIRT
			|| otmp->otyp == VICTORIAN_UNDERWEAR
		    )))
			allowall = TRUE;
	    }

	    if(ilet == 'z') ilet = 'A'; else ilet++;
	}
	bp[foo] = 0;
	if(foo == 0 && bp > buf && bp[-1] == ' ') *--bp = 0;
	strcpy(lets, bp);	/* necessary since we destroy buf */
	if(foo > 5)			/* compactify string */
		compactify(bp);
	*ap = '\0';

	if (allowfloor && !allowall) {
	    if (usegold) {
		valid_ugly_classes[0] = COIN_CLASS;
		strcpy(valid_ugly_classes + 1, let);
	    } else
		strcpy(valid_ugly_classes, let);
	    ugly_word = word;
	    for (otmp = floorchain; otmp; otmp = FOLLOW(otmp, floorfollow))
		if (allow_ugly(otmp))
		    break;
	    if (!otmp)
		allowfloor = FALSE;
	}

	if(!foo && !allowall && !allownone &&
#ifndef GOLDOBJ
	   !allowgold &&
#endif
	   !allowfloor && !allowthisplace) {
		You("don't have anything %sto %s.",
			foox ? "else " : "", word);
		if (flags.moreforced && !(MessageSuppression || u.uprops[MESSAGE_SUPPRESSION_BUG].extrinsic || have_messagesuppressionstone() )) display_nhwindow(WIN_MESSAGE, TRUE);    /* --More-- */
		return((struct obj *)0);
	}
	
	for(;;) {
		cnt = 0;
		if (allowcnt == 2) allowcnt = 1;  /* abort previous count */
		prezero = FALSE;
		sprintf(qbuf, "What do you want to %s? [", word);
		bp = eos(qbuf);
		if (buf[0]) {
		    sprintf(bp, "%s or ?", buf);
		    bp = eos(bp);
		}
		*bp++ = '*';
		if (allowfloor)
		    *bp++ = ',';
		if (allowthisplace)
		    *bp++ = '.';
		if (!buf[0] && bp[-2] != '[') {
		    /* "*," -> "* or ,"; "*." -> "* or ."; "*,." -> "*, or ." */
		    --bp;
		    sprintf(bp, " or %c", *bp);
		    bp += 5;
		}
		*bp++ = ']';
		*bp = '\0';
		if (in_doagain)
		    ilet = readchar();
		else
		    ilet = yn_function(qbuf, (char *)0, '\0');
		if (digit(ilet) && !allowcnt) {
		    pline("No count allowed with this command.");
		    continue;
		}
		if(ilet == '0') prezero = TRUE;
		while(digit(ilet)) {
			if (ilet != '?' && ilet != '*')	savech(ilet);

		    /* accumulate unless cnt has overflowed */
		    if (allowcnt < 3) {
			prevcnt = cnt;
			cnt = 10L * cnt + (long)(ilet - '0');
			/* signal presence of cnt */
			allowcnt = (cnt >= prevcnt) ? 2 : 3;
		    }
		    ilet = readchar();
		}
		if (allowcnt == 3) {
		    /* overflow detected; force cnt to be invalid */
		    cnt = -1L;
		    allowcnt = 2;

		}
		if(index(quitchars,ilet)) {
		    if(flags.verbose)
			pline(Never_mind);
		    return((struct obj *)0);
		}
		if(ilet == '-') {
			return(allownone ? &zeroobj : (struct obj *) 0);
		}
		if(ilet == def_oc_syms[COIN_CLASS]) {
			if (!usegold) {
			    if (!strncmp(word, "rub on ", 7)) {
				/* the dangers of building sentences... */
				You("cannot rub gold%s.", word + 3);
			    } else {
				You("cannot %s gold.", word);
			    }
			    return(struct obj *)0;
#ifndef GOLDOBJ
			} else if (!allowgold) {
				You("are not carrying any gold.");
				return(struct obj *)0;
#endif
			} 
			/* Historic note: early Nethack had a bug which was
			 * first reported for Larn, where trying to drop 2^32-n
			 * gold pieces was allowed, and did interesting things
			 * to your money supply.  The LRS is the tax bureau
			 * from Larn.
			 */

			if (cnt <= 0) {
			    if (cnt < 0 || !prezero)
				pline_The("LRS would be very interested to know you have that much.");
			    return (struct obj *)0;
			}

#ifndef GOLDOBJ
			if(!(allowcnt == 2 && cnt < u.ugold))
				cnt = u.ugold;
			return(mkgoldobj(cnt));
#endif
		}
		if(ilet == '.') {
		    if (allowthisplace)
			return &thisplace;
		    else {
			pline(silly_thing_to, word);
			return(struct obj *)0;
		    }
		}
		if(ilet == ',') {
		    int n;
		    menu_item *pick_list;

		    if (!usefloor) {
			pline(silly_thing_to, word);
			return(struct obj *)0;
		    } else if (!allowfloor) {
			if ((Levitation || Flying))
				You("cannot reach the floor to %s while %sing.", word, Levitation ? "float" : "fly");
			else
				pline("There's nothing here to %s.", word);
			return(struct obj *)0;
		    }
		    sprintf(qbuf, "%s what?", word);
		    n = query_objlist(qbuf, floorchain,
			    floorfollow|INVORDER_SORT|SIGNAL_CANCEL, &pick_list,
			    PICK_ONE, allowall ? allow_all : allow_ugly);
		    if (n<0) {
			if (flags.verbose)
			    pline(Never_mind);
			return (struct obj *)0;
		    } else if (!n)
			continue;
		    otmp = pick_list->item.a_obj;
		    if (allowcnt && pick_list->count < otmp->quan)
			otmp = splitobj(otmp, pick_list->count);
		    free((void *)pick_list);
		    return otmp;
		}
		if(ilet == '?' || ilet == '*') {
		    char *allowed_choices = (ilet == '?') ? lets : (char *)0;
		    long ctmp = 0;

		    if (ilet == '?' && !*lets && *altlets)
			allowed_choices = altlets;
		    ilet = display_pickinv(allowed_choices, TRUE,
					   allowcnt ? &ctmp : (long *)0
#ifdef DUMP_LOG
					   , FALSE
#endif
					   );
		    if(!ilet) continue;
		    if (allowcnt && ctmp >= 0) {
			cnt = ctmp;
			if (!cnt) prezero = TRUE;
			allowcnt = 2;
		    }
		    if(ilet == '\033') {
			if(flags.verbose)
			    pline(Never_mind);
			return((struct obj *)0);
		    }
		    /* they typed a letter (not a space) at the prompt */
		}
		/* WAC - throw now takes a count to allow for single/controlled shooting */
		if(allowcnt == 2 && !strcmp(word,"throw")) {
		    /* permit counts for throwing gold, but don't accept
		     * counts for other things since the throw code will
		     * split off a single item anyway */
#ifdef GOLDOBJ
		    if (ilet != def_oc_syms[COIN_CLASS])
#endif
			allowcnt = 1;
		    if(cnt == 0 && prezero) return((struct obj *)0);
		    if (cnt == 1) {
			save_cm = (char *) 1; /* Non zero */
			multi = 0;
		    }
		    if(cnt > 1) {
			/* You("can only throw one item at a time.");
			continue; */
			multi = cnt - 1;
			cnt = 1;
		    }
		}
#ifdef GOLDOBJ
		flags.botl = 1; /* May have changed the amount of money */
#endif
		savech(ilet);
		for (otmp = invent; otmp; otmp = otmp->nobj)
			if (otmp->invlet == ilet) break;
		if(!otmp) {
			You("don't have that object.");
			if (in_doagain) return((struct obj *) 0);
			continue;
		} else if (cnt < 0 || otmp->quan < cnt) {
			You("don't have that many!  You have only %ld.",
			    otmp->quan);
			if (in_doagain) return((struct obj *) 0);
				continue;
		}
		break;
	}

	if(!allowall && let && !index(let,otmp->oclass)
#ifdef GOLDOBJ
	   && !(usegold && otmp->oclass == COIN_CLASS)
#endif
	   ) {
		silly_thing(word, otmp);
		return((struct obj *)0);
	}
	if(allowcnt == 2) {	/* cnt given */
	    if(cnt == 0) return (struct obj *)0;
	    if(cnt != otmp->quan) {
		/* don't split a stack of cursed loadstones */
		if ( (otmp->otyp == LOADSTONE || otmp->otyp == HEALTHSTONE || otmp->otyp == LUCKSTONE || otmp->otyp == MANASTONE || otmp->otyp == SLEEPSTONE || otmp->otyp == LOADBOULDER || otmp->otyp == STARLIGHTSTONE || otmp->otyp == STONE_OF_MAGIC_RESISTANCE || is_nastygraystone(otmp) ) && otmp->cursed)
		    /* kludge for canletgo()'s can't-drop-this message */
		    otmp->corpsenm = (int) cnt;
		else
		    otmp = splitobj(otmp, cnt);
	    }
	}
	return(otmp);
}

void
silly_thing(word, otmp)
const char *word;
struct obj *otmp;
{
	const char *s1, *s2, *s3, *what;
	int ocls = otmp->oclass, otyp = otmp->otyp;

	s1 = s2 = s3 = 0;
	/* check for attempted use of accessory commands ('P','R') on armor
	   and for corresponding armor commands ('W','T') on accessories */
	if (ocls == ARMOR_CLASS) {
	    if (!strcmp(word, "put on"))
		s1 = "W", s2 = "wear", s3 = "";
	    else if (!strcmp(word, "remove"))
		s1 = "T", s2 = "take", s3 = " off";
	} else if ((ocls == RING_CLASS || otyp == MEAT_RING) ||
		ocls == AMULET_CLASS || ocls == IMPLANT_CLASS ||
		(otyp == BLINDFOLD || otyp == EYECLOSER || otyp == DRAGON_EYEPATCH || otyp == CONDOME || otyp == SOFT_CHASTITY_BELT || otyp == TOWEL || otyp == LENSES || otyp == RADIOGLASSES || otyp == BOSS_VISOR)) {
	    if (!strcmp(word, "wear"))
		s1 = "P", s2 = "put", s3 = " on";
	    else if (!strcmp(word, "take off"))
		s1 = "R", s2 = "remove", s3 = "";
	}
	if (s1) {
	    what = "that";
	    /* quantity for armor and accessory objects is always 1,
	       but some things should be referred to as plural */
	    if (otyp == LENSES || otyp == RADIOGLASSES || otyp == BOSS_VISOR || is_gloves(otmp) || is_boots(otmp))
		what = "those";
	    pline("Use the '%s' command to %s %s%s.", s1, s2, what, s3);
	} else {
	    pline(silly_thing_to, word);
		if (flags.moreforced && !(MessageSuppression || u.uprops[MESSAGE_SUPPRESSION_BUG].extrinsic || have_messagesuppressionstone() )) display_nhwindow(WIN_MESSAGE, TRUE);    /* --More-- */
	}
}

#endif /* OVL1 */
#ifdef OVLB

STATIC_PTR int
ckvalidcat(otmp)
register struct obj *otmp;
{
	/* use allow_category() from pickup.c */
	return((int)allow_category(otmp));
}

STATIC_PTR int
ckunpaid(otmp)
register struct obj *otmp;
{
	return((int)(otmp->unpaid));
}

STATIC_PTR int
ckunided(otmp)
register struct obj *otmp;
{
	return((int)(not_fully_identified(otmp)));
}

boolean
wearing_armor()
{
	return((boolean)(uarm || uarmc || uarmf || uarmg || uarmh || uarms
		|| uarmu
		));
}

boolean
is_worn(otmp)
register struct obj *otmp;
{
    return((boolean)(!!(otmp->owornmask & (W_ARMOR | W_RING | W_AMUL | W_IMPLANT | W_TOOL |
			W_SADDLE |
			W_WEP | W_SWAPWEP | W_QUIVER))));
}

static NEARDATA const char removeables[] =
	{ ARMOR_CLASS, WEAPON_CLASS, RING_CLASS, AMULET_CLASS, IMPLANT_CLASS, TOOL_CLASS, 0 };

/* interactive version of getobj - used for Drop, Identify and */
/* Takeoff (A). Return the number of times fn was called successfully */
/* If combo is TRUE, we just use this to get a category list */
int
ggetobj(word, fn, mx, combo, resultflags)
const char *word;
int (*fn)(OBJ_P), mx;
boolean combo;		/* combination menu flag */
unsigned *resultflags;
{
	int (*ckfn)(OBJ_P) = NULL;
	boolean (*filter)(OBJ_P) = NULL;
	boolean takeoff, ident, allflag, m_seen;
	int itemcount;
#ifndef GOLDOBJ
	int oletct, iletct, allowgold, unpaid, unided, oc_of_sym;
#else
	int oletct, iletct, unpaid, unided, oc_of_sym;
#endif
	char sym, *ip, olets[MAXOCLASSES+5], ilets[MAXOCLASSES+5];
	char extra_removeables[3+1];	/* uwep,uswapwep,uquiver */
	char buf[BUFSZ], qbuf[QBUFSZ];

	if (resultflags) *resultflags = 0;
#ifndef GOLDOBJ
	allowgold = (u.ugold && !strcmp(word, "drop")) ? 1 : 0;
#endif
	takeoff = ident = allflag = m_seen = FALSE;
#ifndef GOLDOBJ
	if(!invent && !allowgold){
#else
	if(!invent){
#endif
		You("have nothing to %s.", word);
		return(0);
	}
	add_valid_menu_class(0);	/* reset */
	if (taking_off(word)) {
	    takeoff = TRUE;
	    filter = is_worn;
	} else if (!strcmp(word, "identify")) {
	    ident = TRUE;
	    filter = not_fully_identified;
	}

	iletct = collect_obj_classes(ilets, invent,
				     	FALSE,
#ifndef GOLDOBJ
					(allowgold != 0),
#endif
					filter, &itemcount);
	unpaid = count_unpaid(invent);
	unided = count_notfullyided(invent);

	if (ident && !iletct) {
	    return -1;		/* no further identifications */
	} else if (!takeoff && (unpaid || unided || invent)) {
	    ilets[iletct++] = ' ';
	    if (unpaid && !Hallucination) ilets[iletct++] = 'u';
	    if (unided && !Hallucination) ilets[iletct++] = 'I';
	    if (count_buc(invent, BUC_BLESSED) && !Hallucination)  ilets[iletct++] = 'B';
	    if (count_buc(invent, BUC_UNCURSED) && !Hallucination) ilets[iletct++] = 'U';
	    if (count_buc(invent, BUC_CURSED) && !Hallucination)   ilets[iletct++] = 'C';
	    if (count_buc(invent, BUC_UNKNOWN) && !Hallucination)  ilets[iletct++] = 'X';
	    if (invent) ilets[iletct++] = 'a';
	} else if (takeoff && invent) {
	    ilets[iletct++] = ' ';
	}
	ilets[iletct++] = 'i';
	if (!combo)
	    ilets[iletct++] = 'm';	/* allow menu presentation on request */
	ilets[iletct] = '\0';

	for (;;) {
	    sprintf(qbuf,"What kinds of thing do you want to %s? [%s]",
		    word, ilets);
	    getlin(qbuf, buf);
	    if (buf[0] == '\033') return(0);
	    if (index(buf, 'i')) {
		if (display_inventory((char *)0, TRUE) == '\033') return 0;
	    } else
		break;
	}

	extra_removeables[0] = '\0';
	if (takeoff) {
	    /* arbitrary types of items can be placed in the weapon slots
	       [any duplicate entries in extra_removeables[] won't matter] */
	    if (uwep) (void)strkitten(extra_removeables, uwep->oclass);
	    if (uswapwep) (void)strkitten(extra_removeables, uswapwep->oclass);
	    if (uquiver) (void)strkitten(extra_removeables, uquiver->oclass);
	}

	ip = buf;
	olets[oletct = 0] = '\0';
	while ((sym = *ip++) != '\0') {
	    if (sym == ' ') continue;
	    oc_of_sym = def_char_to_objclass(sym);
	    if (takeoff && oc_of_sym != MAXOCLASSES) {
		if (index(extra_removeables, oc_of_sym)) {
		    ;	/* skip rest of takeoff checks */
		} else if (!index(removeables, oc_of_sym)) {
		    pline("Not applicable.");
		    return 0;
		} else if (oc_of_sym == ARMOR_CLASS && !wearing_armor()) {
		    You("are not wearing any armor.");
		    return 0;
		} else if (oc_of_sym == WEAPON_CLASS &&
			!uwep && !uswapwep && !uquiver) {
		    You("are not wielding anything.");
		    return 0;
		} else if (oc_of_sym == RING_CLASS && !uright && !uleft) {
		    You("are not wearing rings.");
		    return 0;
		} else if (oc_of_sym == AMULET_CLASS && !uamul) {
		    You("are not wearing an amulet.");
		    return 0;
		} else if (oc_of_sym == TOOL_CLASS && !ublindf) {
		    You("are not wearing a blindfold.");
		    return 0;
		}
	    }

	    if (oc_of_sym == COIN_CLASS && !combo) {
#ifndef GOLDOBJ
		if (allowgold == 1)
		    (*fn)(mkgoldobj(u.ugold));
		else if (!u.ugold)
		    You("have no gold.");
		allowgold = 2;
#else
		flags.botl = 1;
#endif
	    } else if (sym == 'a') {
		allflag = TRUE;
	    } else if (sym == 'A') {
		/* same as the default */ ;
	    } else if (sym == 'u') {
		add_valid_menu_class('u');
		ckfn = ckunpaid;
	    } else if (sym == 'I') {
		add_valid_menu_class('I');
		ckfn = ckunided;
	    } else if (sym == 'B') {
	    	add_valid_menu_class('B');
	    	ckfn = ckvalidcat;
	    } else if (sym == 'U') {
	    	add_valid_menu_class('U');
	    	ckfn = ckvalidcat;
	    } else if (sym == 'C') {
	    	add_valid_menu_class('C');
		ckfn = ckvalidcat;
	    } else if (sym == 'X') {
	    	add_valid_menu_class('X');
		ckfn = ckvalidcat;
	    } else if (sym == 'm') {
		m_seen = TRUE;
	    } else if (oc_of_sym == MAXOCLASSES) {
		You("don't have any %c's.", sym);
	    } else if (oc_of_sym != VENOM_CLASS) {	/* suppress venom */
		if (!index(olets, oc_of_sym)) {
		    add_valid_menu_class(oc_of_sym);
		    olets[oletct++] = oc_of_sym;
		    olets[oletct] = 0;
		}
	    }
	}

	if (m_seen)
	    return (allflag || (!oletct && ckfn != ckunpaid)) ? -2 : -3;
	else if (flags.menu_style != MENU_TRADITIONAL && combo && !allflag)
	    return 0;
#ifndef GOLDOBJ
	else if (allowgold == 2 && !oletct)
	    return 1;	/* you dropped gold (or at least tried to) */
	else {
#else
	else /*!!!! if (allowgold == 2 && !oletct)
	    !!!! return 1;	 you dropped gold (or at least tried to) 
            !!!! test gold dropping
	else*/ {
#endif
	    int cnt = askchain(&invent, olets, allflag, fn, ckfn, mx, word); 
	    /*
	     * askchain() has already finished the job in this case
	     * so set a special flag to convey that back to the caller
	     * so that it won't continue processing.
	     * Fix for bug C331-1 reported by Irina Rempt-Drijfhout. 
	     */
	    if (combo && allflag && resultflags)
		*resultflags |= ALL_FINISHED; 
	    return cnt;
	}
}

/*
 * Walk through the chain starting at objchn and ask for all objects
 * with olet in olets (if nonNULL) and satisfying ckfn (if nonnull)
 * whether the action in question (i.e., fn) has to be performed.
 * If allflag then no questions are asked. Max gives the max nr of
 * objects to be treated. Return the number of objects treated.
 */
int
askchain(objchn, olets, allflag, fn, ckfn, mx, word)
struct obj **objchn;
register int allflag, mx;
register const char *olets, *word;	/* olets is an Obj Class char array */
register int (*fn)(OBJ_P), (*ckfn)(OBJ_P);
{
	struct obj *otmp, *otmp2, *otmpo;
	register char sym, ilet;
	register int cnt = 0, dud = 0, tmp;
	boolean takeoff, nodot, ident, ininv;
	char qbuf[QBUFSZ];

	takeoff = taking_off(word);
	ident = !strcmp(word, "identify");
	nodot = (!strcmp(word, "nodot") || !strcmp(word, "drop") ||
		 ident || takeoff);
	ininv = (*objchn == invent);
	/* Changed so the askchain is interrogated in the order specified.
	 * For example, if a person specifies =/ then first all rings will be
	 * asked about followed by all wands -dgk
	 */
nextclass:
	ilet = 'a'-1;
	if (*objchn && (*objchn)->oclass == COIN_CLASS)
		ilet--;		/* extra iteration */
	for (otmp = *objchn; otmp; otmp = otmp2) {
		if(ilet == 'z') ilet = 'A'; else ilet++;
		otmp2 = otmp->nobj;
		if (olets && *olets && otmp->oclass != *olets) continue;
		if (takeoff && !is_worn(otmp)) continue;
		if (ident && !not_fully_identified(otmp)) continue;
		if (ckfn && !(*ckfn)(otmp)) continue;
		if (!allflag) {
			strcpy(qbuf, !ininv ? doname(otmp) :
				xprname(otmp, (char *)0, ilet, !nodot, 0L, 0L));
			strcat(qbuf, "?");
			sym = (takeoff || ident || otmp->quan < 2L) ?
				nyaq(qbuf) : nyNaq(qbuf);
		}
		else	sym = 'y';

		otmpo = otmp;
		if (sym == '#') {
		 /* Number was entered; split the object unless it corresponds
		    to 'none' or 'all'.  2 special cases: cursed loadstones and
		    welded weapons (eg, multiple daggers) will remain as merged
		    unit; done to avoid splitting an object that won't be
		    droppable (even if we're picking up rather than dropping).
		  */
		    if (!yn_number)
			sym = 'n';
		    else {
			sym = 'y';
			if (yn_number < otmp->quan && !welded(otmp) &&
			    (!otmp->cursed || (otmp->otyp != LOADSTONE && otmp->otyp != LUCKSTONE && otmp->otyp != HEALTHSTONE && otmp->otyp != MANASTONE && otmp->otyp != SLEEPSTONE && otmp->otyp != LOADBOULDER && otmp->otyp != STARLIGHTSTONE && otmp->otyp != STONE_OF_MAGIC_RESISTANCE && !is_nastygraystone(otmp) ) )) {
			    otmp = splitobj(otmp, yn_number);
			}
		    }
		}
		switch(sym){
		case 'a':
			allflag = 1;
		case 'y':
			tmp = (*fn)(otmp);
			if(tmp < 0) {
			    if (container_gone(fn)) {
				/* otmp caused magic bag to explode;
				   both are now gone */
				otmp = 0;		/* and return */
			    } else if (otmp && otmp != otmpo) {
				/* split occurred, merge again */
				(void) merged(&otmpo, &otmp);
			    }
			    goto ret;
			}
			cnt += tmp;
			if(--mx == 0) goto ret;
		case 'n':
			if(nodot) dud++;
		default:
			break;
		case 'q':
			/* special case for seffects() */
			if (ident) cnt = -1;
			goto ret;
		}
	}
	if (olets && *olets && *++olets)
		goto nextclass;
	if(!takeoff && (dud || cnt)) pline("That was all.");
	else if(!dud && !cnt) pline("No applicable objects.");
ret:
	return(cnt);
}


/*
 *	Object identification routines:
 */

/* make an object actually be identified; no display updating */
void
fully_identify_obj(otmp)
struct obj *otmp;
{
	if (!rn2(10) && uarmc && OBJ_DESCR(objects[uarmc->otyp]) && (!strcmp(OBJ_DESCR(objects[uarmc->otyp]), "ignorant cloak") || !strcmp(OBJ_DESCR(objects[uarmc->otyp]), "nevezhestvennyye plashch") || !strcmp(OBJ_DESCR(objects[uarmc->otyp]), "johil plash") )) {
		pline("You are too ignorant, and therefore the identification attempt fails.");
		return;
	}

	if (!rn2(3) && RngeIgnorance) {
		pline("You are too ignorant, and therefore the identification attempt fails.");
		return;
	}

	if (otmp->oclass == SCROLL_CLASS && !(otmp->oartifact) && !(otmp->fakeartifact) && rnd(u.idscrollpenalty) > 100) pline("The scroll resisted your identification attempt!");
	else if (otmp->oclass == POTION_CLASS && !(otmp->oartifact) && !(otmp->fakeartifact) && rnd(u.idpotionpenalty) > 3) pline("The potion resisted your identification attempt!");
	else if (otmp->oclass == RING_CLASS && !(otmp->oartifact) && !(otmp->fakeartifact) && (!(otmp->owornmask & W_RING) || ((rnd(u.idringpenalty) > 4) && (rnd(u.idringpenalty) > 4)) ) && rnd(u.idringpenalty) > 4) pline("The ring resisted your identification attempt!");
	else if (otmp->oclass == AMULET_CLASS && !(otmp->oartifact) && !(otmp->fakeartifact) && (!(otmp->owornmask & W_AMUL) || ((rnd(u.idamuletpenalty) > 15) && (rnd(u.idamuletpenalty) > 15)) ) && rnd(u.idamuletpenalty) > 15) pline("The amulet resisted your identification attempt!");
	else if (otmp->oclass == IMPLANT_CLASS && !(otmp->oartifact) && !(otmp->fakeartifact) && (!(otmp->owornmask & W_IMPLANT) || ((rnd(u.idimplantpenalty) > 1) && (rnd(u.idimplantpenalty) > 1)) ) && rnd(u.idimplantpenalty) > 1) pline("The implant resisted your identification attempt!");
	else if (otmp->oclass == WAND_CLASS && !(otmp->oartifact) && !(otmp->fakeartifact) && rnd(u.idwandpenalty) > 3) pline("The wand resisted your identification attempt!");
	else if (otmp->oclass == ARMOR_CLASS && !(otmp->oartifact) && !(otmp->fakeartifact) && (!(otmp->owornmask & W_ARMOR) || ((rnd(u.idarmorpenalty) > 15) && (rnd(u.idarmorpenalty) > 15)) ) && rnd(u.idarmorpenalty) > 15) pline("The armor resisted your identification attempt!");
	else if (otmp->oclass == SPBOOK_CLASS && !(otmp->oartifact) && !(otmp->fakeartifact) && rnd(u.idspellbookpenalty) > 2) pline("The spellbook resisted your identification attempt!");
	else if (otmp->oclass == GEM_CLASS && !(otmp->oartifact) && !(otmp->fakeartifact) && rnd(u.idgempenalty) > 100) pline("The gem resisted your identification attempt!");
	else if (otmp->oclass == TOOL_CLASS && !(otmp->oartifact) && !(otmp->fakeartifact) && rnd(u.idtoolpenalty) > 5) pline("The tool resisted your identification attempt!");
      else makeknown(otmp->otyp);
    if (otmp->oartifact) discover_artifact((int)otmp->oartifact);
    otmp->known = otmp->dknown = otmp->bknown = otmp->rknown = 1;
    if (otmp->otyp == EGG && otmp->corpsenm != NON_PM)
	learn_egg_type(otmp->corpsenm);
}

void
maybe_fully_identify_obj(otmp)
struct obj *otmp;
{
	if (!rn2(10) && uarmc && OBJ_DESCR(objects[uarmc->otyp]) && (!strcmp(OBJ_DESCR(objects[uarmc->otyp]), "ignorant cloak") || !strcmp(OBJ_DESCR(objects[uarmc->otyp]), "nevezhestvennyye plashch") || !strcmp(OBJ_DESCR(objects[uarmc->otyp]), "johil plash") )) {
		pline("You are too ignorant, and therefore the identification attempt fails.");
		return;
	}

	if (!rn2(3) && RngeIgnorance) {
		pline("You are too ignorant, and therefore the identification attempt fails.");
		return;
	}

	if (otmp->oclass == SCROLL_CLASS && !(otmp->oartifact) && !(otmp->fakeartifact) && rnd(u.idscrollpenalty) > 100) pline("The scroll resisted your identification attempt!");
	else if (otmp->oclass == POTION_CLASS && !(otmp->oartifact) && !(otmp->fakeartifact) && rnd(u.idpotionpenalty) > 3) pline("The potion resisted your identification attempt!");
	else if (otmp->oclass == RING_CLASS && !(otmp->oartifact) && !(otmp->fakeartifact) && (!(otmp->owornmask & W_RING) || ((rnd(u.idringpenalty) > 4) && (rnd(u.idringpenalty) > 4)) ) && rnd(u.idringpenalty) > 4) pline("The ring resisted your identification attempt!");
	else if (otmp->oclass == AMULET_CLASS && !(otmp->oartifact) && !(otmp->fakeartifact) && (!(otmp->owornmask & W_AMUL) || ((rnd(u.idamuletpenalty) > 15) && (rnd(u.idamuletpenalty) > 15)) ) && rnd(u.idamuletpenalty) > 15) pline("The amulet resisted your identification attempt!");
	else if (otmp->oclass == IMPLANT_CLASS && !(otmp->oartifact) && !(otmp->fakeartifact) && (!(otmp->owornmask & W_IMPLANT) || ((rnd(u.idimplantpenalty) > 1) && (rnd(u.idimplantpenalty) > 1)) ) && rnd(u.idimplantpenalty) > 1) pline("The implant resisted your identification attempt!");
	else if (otmp->oclass == WAND_CLASS && !(otmp->oartifact) && !(otmp->fakeartifact) && rnd(u.idwandpenalty) > 3) pline("The wand resisted your identification attempt!");
	else if (otmp->oclass == ARMOR_CLASS && !(otmp->oartifact) && !(otmp->fakeartifact) && (!(otmp->owornmask & W_ARMOR) || ((rnd(u.idarmorpenalty) > 15) && (rnd(u.idarmorpenalty) > 15)) ) && rnd(u.idarmorpenalty) > 15) pline("The armor resisted your identification attempt!");
	else if (otmp->oclass == SPBOOK_CLASS && !(otmp->oartifact) && !(otmp->fakeartifact) && rnd(u.idspellbookpenalty) > 2) pline("The spellbook resisted your identification attempt!");
	else if (otmp->oclass == GEM_CLASS && !(otmp->oartifact) && !(otmp->fakeartifact) && rnd(u.idgempenalty) > 100) pline("The gem resisted your identification attempt!");
	else if (otmp->oclass == TOOL_CLASS && !(otmp->oartifact) && !(otmp->fakeartifact) && rnd(u.idtoolpenalty) > 5) pline("The tool resisted your identification attempt!");
      else if (!rn2(3)) makeknown(otmp->otyp);
    if (otmp->oartifact) discover_artifact((int)otmp->oartifact);
    if (!rn2(3)) otmp->known = 1;
    if (!rn2(3)) otmp->dknown = 1;
    if (!rn2(3)) otmp->bknown = 1;
    if (!rn2(3)) otmp->rknown = 1;
    if (otmp->otyp == EGG && otmp->corpsenm != NON_PM)
	learn_egg_type(otmp->corpsenm);
}

/* ggetobj callback routine; identify an object and give immediate feedback */
int
identify(otmp)
struct obj *otmp;
{
    fully_identify_obj(otmp);
    prinv((char *)0, otmp, 0L);
    return 1;
}

int
identifyless(otmp)
struct obj *otmp;
{
    maybe_fully_identify_obj(otmp);
    prinv((char *)0, otmp, 0L);
    return 1;
}

/* menu of unidentified objects; select and identify up to id_limit of them */
STATIC_OVL void
menu_identify(id_limit)
int id_limit;
{
    menu_item *pick_list;
    int n, i, first = 1;
    char buf[BUFSZ];
    /* assumptions:  id_limit > 0 and at least one unID'd item is present */

    while (id_limit) {
	sprintf(buf, "What would you like to identify %s?",
		first ? "first" : "next");
identifydialogue:
	n = query_objlist(buf, invent, SIGNAL_NOMENU|USE_INVLET|INVORDER_SORT,
		&pick_list, PICK_ANY, not_fully_identified);

	if (n == 0) {
		if (yn("Really exit with no object selected?") == 'y')
			pline("You just wasted the opportunity to identify your items.");
		else goto identifydialogue;
	}

	if (n > 0) {
	    if (n > id_limit) n = id_limit;
	    for (i = 0; i < n; i++, id_limit--)
		(void) identify(pick_list[i].item.a_obj);
	    free((void *) pick_list);
	    mark_synch(); /* Before we loop to pop open another menu */
	} else {
	    if (n < 0) pline("That was all.");
	    id_limit = 0; /* Stop now */
	}
	first = 0;
    }
}

/* dialog with user to identify a given number of items; 0 means all */
void
identify_pack(id_limit, wizmodeflag)
int id_limit;
boolean wizmodeflag;
{
    struct obj *obj, *the_obj;
    register struct obj *otmp;
    int n, unid_cnt;

    unid_cnt = 0;
    the_obj = 0;		/* if unid_cnt ends up 1, this will be it */

    for (obj = invent; obj; obj = obj->nobj) {
	if (not_fully_identified(obj)) ++unid_cnt, the_obj = obj;


	if (!id_limit && !issoviet && Has_contents(obj)) { /* full inventory id works on containers --Amy */
	/* In Soviet Russia, people HATE user-friendly interfaces. They would even go so far as to disallow having more than
	 * 52 items in open inventory, but of course I'm not implementing that stupidity ever again. Still, somehow they
	 * seem to like it if you have to put as many fully identified objects away before you read that blessed scroll,
	 * so you can maximize the amount of non-identified ones, only to have bad luck and not actually get everything
	 * IDed. And then they can repeat the entire ordeal when they find the next ID scroll. --Amy */

		for (otmp = obj->cobj; otmp; otmp = otmp->nobj) {
		    if ( (rn2(5) || wizmodeflag) && not_fully_identified(otmp)) {
			if (wizmodeflag) (void) identify(otmp);
			else (void) identifyless(otmp);
			}
		}

	}

    }

    if (!unid_cnt) {
	You("have already identified all of your possessions.");
    } else if (!id_limit) {
	/* identify everything */
	if (unid_cnt == 1) {
	    (void) identify(the_obj);
	} else {

	    /* TODO:  use fully_identify_obj and cornline/menu/whatever here */
	    for (obj = invent; obj; obj = obj->nobj)
		if ( (rn2(5) || wizmodeflag) && not_fully_identified(obj)) {
			if (wizmodeflag) (void) identify(obj);
			else (void) identifyless(obj);
		}

	}
    } else {
	/* identify up to `id_limit' items */
	n = 0;
	if (flags.menu_style == MENU_TRADITIONAL)
	    do {
		n = ggetobj("identify", identify, id_limit, FALSE, (unsigned *)0);
		if (n < 0) break; /* quit or no eligible items */
	    } while ((id_limit -= n) > 0);
	if (n == 0 || n < -1)
	    menu_identify(id_limit);
    }
    update_inventory();
}

#endif /* OVLB */
#ifdef OVL2

STATIC_OVL char
obj_to_let(obj)	/* should of course only be called for things in invent */
register struct obj *obj;
{
#ifndef GOLDOBJ
	if (obj->oclass == COIN_CLASS)
		return GOLD_SYM;
#endif
	if (!flags.invlet_constant) {
		obj->invlet = NOINVSYM;
		reassign();
	}
	return obj->invlet;
}

/*
 * Print the indicated quantity of the given object.  If quan == 0L then use
 * the current quantity.
 */
void
prinv(prefix, obj, quan)
const char *prefix;
register struct obj *obj;
long quan;
{
	if (!prefix) prefix = "";
	pline("%s%s%s",
	      prefix, *prefix ? " " : "",
	      xprname(obj, (char *)0, obj_to_let(obj), TRUE, 0L, quan));
}

#endif /* OVL2 */
#ifdef OVL1

char *
xprname(obj, txt, let, dot, cost, quan)
struct obj *obj;
const char *txt;	/* text to print instead of obj */
char let;		/* inventory letter */
boolean dot;		/* append period; (dot && cost => Iu) */
long cost;		/* cost (for inventory of unpaid or expended items) */
long quan;		/* if non-0, print this quantity, not obj->quan */
{
#ifdef LINT	/* handle static char li[BUFSZ]; */
    char li[BUFSZ];
#else
    static char li[BUFSZ];
#endif
    boolean use_invlet = flags.invlet_constant && let != CONTAINED_SYM;
    long savequan = 0;

    if (quan && obj) {
	savequan = obj->quan;
	obj->quan = quan;
    }

    /*
     * If let is:
     *	*  Then obj == null and we are printing a total amount.
     *	>  Then the object is contained and doesn't have an inventory letter.
     */
    if (cost != 0 || let == '*') {
	/* if dot is true, we're doing Iu, otherwise Ix */
	sprintf(li, "%c - %-45s %6ld %s",
		(dot && use_invlet ? obj->invlet : let),
		(txt ? txt : doname(obj)), cost, currency(cost));
#ifndef GOLDOBJ
    } else if (obj && obj->oclass == COIN_CLASS) {
	sprintf(li, "%ld gold piece%s%s", obj->quan, plur(obj->quan),
		(dot ? "." : ""));
#endif
    } else {
	/* ordinary inventory display or pickup message */
	sprintf(li, "%c - %s%s",
		(use_invlet ? obj->invlet : let),
		(txt ? txt : doname(obj)), (dot ? "." : ""));
    }
    if (savequan) obj->quan = savequan;

    return li;
}

#endif /* OVL1 */
#ifdef OVLB

/* the 'i' command */
int
ddoinv()
{
	/*(void) display_inventory((char *)0, FALSE);
	return 0;*/

	char c;
	struct obj *otmp;
	c = display_inventory((char *)0, TRUE);
	if (!c) return 0;
	for (otmp = invent; otmp; otmp = otmp->nobj)
		if (otmp->invlet == c) break;
	if (otmp) return itemactions(otmp);
	return 0;

}

/*
 * find_unpaid()
 *
 * Scan the given list of objects.  If last_found is NULL, return the first
 * unpaid object found.  If last_found is not NULL, then skip over unpaid
 * objects until last_found is reached, then set last_found to NULL so the
 * next unpaid object is returned.  This routine recursively follows
 * containers.
 */
STATIC_OVL struct obj *
find_unpaid(list, last_found)
    struct obj *list, **last_found;
{
    struct obj *obj;

    while (list) {
	if (list->unpaid) {
	    if (*last_found) {
		/* still looking for previous unpaid object */
		if (list == *last_found)
		    *last_found = (struct obj *) 0;
	    } else
		return (*last_found = list);
	}
	if (Has_contents(list)) {
	    if ((obj = find_unpaid(list->cobj, last_found)) != 0)
		return obj;
	}
	list = list->nobj;
    }
    return (struct obj *) 0;
}

/*
 * Internal function used by display_inventory and getobj that can display
 * inventory and return a count as well as a letter. If out_cnt is not null,
 * any count returned from the menu selection is placed here.
 */
#ifdef DUMP_LOG
static char
display_pickinv(lets, want_reply, out_cnt, want_dump)
register const char *lets;
boolean want_reply;
long* out_cnt;
boolean want_dump;
#else
static char
display_pickinv(lets, want_reply, out_cnt)
register const char *lets;
boolean want_reply;
long* out_cnt;
#endif
{
	struct obj *otmp;
	char ilet, ret;
	char *invlet = flags.inv_order;
	int n, classcount;
	winid win;				/* windows being used */
	static winid local_win = WIN_ERR;	/* window for partial menus */
	anything any;
	menu_item *selected;
#ifdef PROXY_GRAPHICS
	static int busy = 0;
	if (busy)
	    return 0;
	busy++;
#endif

	/* overriden by global flag */
	if (flags.perm_invent) {
	    win = (lets && *lets) ? local_win : WIN_INVEN;
	    /* create the first time used */
	    if (win == WIN_ERR)
		win = local_win = create_nhwindow(NHW_MENU);
	} else
	    win = WIN_INVEN;

	if ( (InventoryLoss || u.uprops[INVENTORY_LOST].extrinsic || (uarmh && uarmh->oartifact == ART_DEEP_INSANITY) || (uarmh && uarmh->oartifact == ART_FLAT_INSANITY) || have_inventorylossstone()) && !program_state.gameover) {pline("Not enough memory to create inventory window");
 		if (flags.moreforced && !(MessageSuppression || u.uprops[MESSAGE_SUPPRESSION_BUG].extrinsic || have_messagesuppressionstone() )) display_nhwindow(WIN_MESSAGE, TRUE);    /* --More-- */
		return 0;
	}	

#ifdef DUMP_LOG
	if (want_dump)   dump("", "Your inventory");
#endif

	/*
	Exit early if no inventory -- but keep going if we are doing
	a permanent inventory update.  We need to keep going so the
	permanent inventory window updates itself to remove the last
	item(s) dropped.  One down side:  the addition of the exception
	for permanent inventory window updates _can_ pop the window
	up when it's not displayed -- even if it's empty -- because we
	don't know at this level if its up or not.  This may not be
	an issue if empty checks are done before hand and the call
	to here is short circuited away.
	*/
	if (!invent && !(flags.perm_invent && !lets && !want_reply)) {
#ifndef GOLDOBJ
	    pline("Not carrying anything%s.", u.ugold ? " except gold" : "");
#else
	    pline("Not carrying anything.");
#endif
#ifdef PROXY_GRAPHICS
	    busy--;
#endif
#ifdef DUMP_LOG
	    if (want_dump) {
#ifdef GOLDOBJ
		dump("  ", "Not carrying anything");
#else
		dump("  Not carrying anything",
		    u.ugold ? " except gold." : ".");
#endif
	    }
#endif
	    return 0;
	}

	/* oxymoron? temporarily assign permanent inventory letters */
	if (!flags.invlet_constant) reassign();

	if (lets && strlen(lets) == 1) {
	    /* when only one item of interest, use pline instead of menus;
	       we actually use a fake message-line menu in order to allow
	       the user to perform selection at the --More-- prompt for tty */
	    ret = '\0';
	    for (otmp = invent; otmp; otmp = otmp->nobj) {
		if (otmp->invlet == lets[0]) {
		    ret = message_menu(lets[0],
			  want_reply ? PICK_ONE : PICK_NONE,
			  xprname(otmp, (char *)0, lets[0], TRUE, 0L, 0L));
		    if (out_cnt) *out_cnt = -1L;	/* select all */
#ifdef DUMP_LOG
		    if (want_dump) {
		      char letbuf[7];
		      sprintf(letbuf, "  %c - ", lets[0]);
		      dump(letbuf,
			   xprname(otmp, (char *)0, lets[0], TRUE, 0L, 0L));
		    }
#endif
		    break;
		}
	    }
#ifdef PROXY_GRAPHICS
	    busy--;
#endif
	    return ret;
	}

	start_menu(win);
nextclass:
	classcount = 0;
	any.a_void = 0;		/* set all bits to zero */

	/*if (flags.alphabetinv) ilet = 'a'-1;*/

	for(otmp = invent; otmp; otmp = otmp->nobj) {
		/*if (!flags.alphabetinv)*/ ilet = otmp->invlet;

		/*if (flags.alphabetinv) {
			if(ilet == 'z') ilet = 'A';
			else if(ilet == 'Z') ilet = 'a';
			else ilet++;
		}*/

		if(!lets || !*lets || index(lets, ilet)) {
			if (!flags.sortpack || otmp->oclass == *invlet) {
			    if (flags.sortpack && !classcount) {
				any.a_void = 0;		/* zero */
				add_menu(win, NO_GLYPH, &any, 0, 0, iflags.menu_headings,
				    let_to_name(*invlet, FALSE, FALSE), MENU_UNSELECTED);
#ifdef DUMP_LOG
                               if (want_dump)
#ifndef SHOWSYM /* 5lo: Very ugly but it works */  /* This will need some manual shuffling around when this gets mplemented, but should work for now --Elronnd */
                                   dump("  ", let_to_name(*invlet, FALSE, FALSE));
#else
                                   dump("  ", let_to_name(*invlet, FALSE, FALSE));
#endif /* SHOWSYM */

#endif
				classcount++;
			    }
			    any.a_char = ilet;
			    add_menu(win, obj_to_glyph(otmp),
					&any, ilet, 0, ATR_NONE, doname(otmp),
					MENU_UNSELECTED);
#ifdef DUMP_LOG
                           if (want_dump) {
                             char letbuf[7];
                             sprintf(letbuf, "  %c - ", ilet);
                             dump(letbuf, doname(otmp));
                           }
#endif
			}
		}
	}
	if (flags.sortpack) {
		if (*++invlet) goto nextclass;
#ifdef WIZARD
		if (--invlet != venom_inv) {
			invlet = venom_inv;
			goto nextclass;
		}
#endif
	}
	end_menu(win, (char *) 0);

	n = select_menu(win, want_reply ? PICK_ONE : PICK_NONE, &selected);
	if (n > 0) {
	    ret = selected[0].item.a_char;
	    if (out_cnt) *out_cnt = selected[0].count;
	    free((void *)selected);
	} else
	    ret = !n ? '\0' : '\033';	/* cancelled */
#ifdef DUMP_LOG
	if (want_dump)  dump("", "");
#endif

#ifdef PROXY_GRAPHICS
	busy--;
#endif
	return ret;
}

/*
 * If lets == NULL or "", list all objects in the inventory.  Otherwise,
 * list all objects with object classes that match the order in lets.
 *
 * Returns the letter identifier of a selected item, or 0 if nothing
 * was selected.
 */
char
display_inventory(lets, want_reply)
register const char *lets;
boolean want_reply;
{
	if ( (InventoryLoss || u.uprops[INVENTORY_LOST].extrinsic || (uarmh && uarmh->oartifact == ART_DEEP_INSANITY) || (uarmh && uarmh->oartifact == ART_FLAT_INSANITY) || have_inventorylossstone()) && !program_state.gameover) {pline("Not enough memory to create inventory window");
		if (flags.moreforced && !(MessageSuppression || u.uprops[MESSAGE_SUPPRESSION_BUG].extrinsic || have_messagesuppressionstone() )) display_nhwindow(WIN_MESSAGE, TRUE);    /* --More-- */
		return 0;
	}

	return display_pickinv(lets, want_reply, (long *)0
#ifdef DUMP_LOG
                               , FALSE
#endif
       );
}

#ifdef DUMP_LOG
/* See display_inventory. This is the same thing WITH dumpfile creation */
char
dump_inventory(lets, want_reply)
register const char *lets;
boolean want_reply;
{
  return display_pickinv(lets, want_reply, (long *)0, TRUE);
}
#endif

/*
 * Returns the number of unpaid items within the given list.  This includes
 * contained objects.
 */
int
count_unpaid(list)
    struct obj *list;
{
    int count = 0;

    while (list) {
	if (list->unpaid) count++;
	if (Has_contents(list))
	    count += count_unpaid(list->cobj);
	list = list->nobj;
    }
    return count;
}

int
count_notfullyided(list)
    struct obj *list;
{
    int count = 0;

    while (list) {
	if (not_fully_identified(list)) count++;
	list = list->nobj;
    }
    return count;
}

/*
 * Returns the number of items with b/u/c/unknown within the given list.  
 * This does NOT include contained objects.
 */
int
count_buc(list, type)
    struct obj *list;
    int type;
{
    int count = 0;

    while (list) {
	if (Role_if(PM_PRIEST) || Role_if(PM_NECROMANCER) || Role_if(PM_CHEVALIER) || Race_if(PM_VEELA)) list->bknown = TRUE;
	switch(type) {
	    case BUC_BLESSED:
		if (list->oclass != COIN_CLASS && list->bknown && list->blessed)
		    count++;
		break;
	    case BUC_CURSED:
		if (list->oclass != COIN_CLASS && list->bknown && list->cursed)
		    count++;
		break;
	    case BUC_UNCURSED:
		if (list->oclass != COIN_CLASS &&
			list->bknown && !list->blessed && !list->cursed)
		    count++;
		break;
	    case BUC_UNKNOWN:
		if (list->oclass != COIN_CLASS && !list->bknown)
		    count++;
		break;
	    default:
		impossible("need count of curse status %d?", type);
		return 0;
	}
	list = list->nobj;
    }
    return count;
}

STATIC_OVL void
dounpaid()
{
    winid win;
    struct obj *otmp, *marker;
    register char ilet;
    char *invlet = flags.inv_order;
    int classcount, count, num_so_far;
    int save_unpaid = 0;	/* lint init */
    long cost, totcost;

    count = count_unpaid(invent);

    if (count == 1) {
	marker = (struct obj *) 0;
	otmp = find_unpaid(invent, &marker);

	/* see if the unpaid item is in the top level inventory */
	for (marker = invent; marker; marker = marker->nobj)
	    if (marker == otmp) break;

	pline("%s", xprname(otmp, distant_name(otmp, doname),
			    marker ? otmp->invlet : CONTAINED_SYM,
			    TRUE, unpaid_cost(otmp), 0L));
	return;
    }

    win = create_nhwindow(NHW_MENU);
    cost = totcost = 0;
    num_so_far = 0;	/* count of # printed so far */
    if (!flags.invlet_constant) reassign();

    do {
	classcount = 0;
	for (otmp = invent; otmp; otmp = otmp->nobj) {
	    ilet = otmp->invlet;
	    if (otmp->unpaid) {
		if (!flags.sortpack || otmp->oclass == *invlet) {
		    if (flags.sortpack && !classcount) {
			putstr(win, 0, let_to_name(*invlet, TRUE, FALSE));
			classcount++;
		    }

		    totcost += cost = unpaid_cost(otmp);
		    /* suppress "(unpaid)" suffix */
		    save_unpaid = otmp->unpaid;
		    otmp->unpaid = 0;
		    putstr(win, 0, xprname(otmp, distant_name(otmp, doname),
					   ilet, TRUE, cost, 0L));
		    otmp->unpaid = save_unpaid;
		    num_so_far++;
		}
	    }
	}
    } while (flags.sortpack && (*++invlet));

    if (count > num_so_far) {
	/* something unpaid is contained */
	if (flags.sortpack)
	    putstr(win, 0, let_to_name(CONTAINED_SYM, TRUE, FALSE));
	/*
	 * Search through the container objects in the inventory for
	 * unpaid items.  The top level inventory items have already
	 * been listed.
	 */
	for (otmp = invent; otmp; otmp = otmp->nobj) {
	    if (Has_contents(otmp)) {
		marker = (struct obj *) 0;	/* haven't found any */
		while (find_unpaid(otmp->cobj, &marker)) {
		    totcost += cost = unpaid_cost(marker);
		    save_unpaid = marker->unpaid;
		    marker->unpaid = 0;    /* suppress "(unpaid)" suffix */
		    putstr(win, 0,
			   xprname(marker, distant_name(marker, doname),
				   CONTAINED_SYM, TRUE, cost, 0L));
		    marker->unpaid = save_unpaid;
		}
	    }
	}
    }

    putstr(win, 0, "");
    putstr(win, 0, xprname((struct obj *)0, "Total:", '*', FALSE, totcost, 0L));
    display_nhwindow(win, FALSE);
    destroy_nhwindow(win);
}


/* query objlist callback: return TRUE if obj type matches "this_type" */
static int this_type;

STATIC_OVL boolean
this_type_only(obj)
    struct obj *obj;
{
    return (obj->oclass == this_type);
}

/* the 'I' command */
int
dotypeinv()
{
	char c = '\0';
	int n, i = 0;
	char *extra_types, types[BUFSZ];
	int class_count, oclass, unpaid_count, itemcount;
	boolean billx = *u.ushops && doinvbill(0);
	menu_item *pick_list;
	boolean traditional = TRUE;
	const char *prompt = "What type of object do you want an inventory of?";

#ifndef GOLDOBJ
	if (!invent && !u.ugold && !billx) {
#else
	if (!invent && !billx) {
#endif
	    You("aren't carrying anything.");
	    return 0;
	}
	unpaid_count = count_unpaid(invent);
	if (flags.menu_style != MENU_TRADITIONAL) {
	    if (flags.menu_style == MENU_FULL ||
				flags.menu_style == MENU_PARTIAL) {
		traditional = FALSE;
		i = UNPAID_TYPES;
		if (billx) i |= BILLED_TYPES;
		n = query_category(prompt, invent, i, &pick_list, PICK_ONE);
		if (!n) return 0;
		this_type = c = pick_list[0].item.a_int;
		free((void *) pick_list);
	    }
	}
	if (traditional) {
	    /* collect a list of classes of objects carried, for use as a prompt */
	    types[0] = 0;
	    class_count = collect_obj_classes(types, invent,
					      FALSE,
#ifndef GOLDOBJ
					      (u.ugold != 0),
#endif
					      (boolean (*)(OBJ_P)) 0, &itemcount);
	    if (unpaid_count) {
		strcat(types, "u");
		class_count++;
	    }
	    if (billx) {
		strcat(types, "x");
		class_count++;
	    }
	    /* add everything not already included; user won't see these */
	    extra_types = eos(types);
	    *extra_types++ = '\033';
	    if (!unpaid_count) *extra_types++ = 'u';
	    if (!billx) *extra_types++ = 'x';
	    *extra_types = '\0';	/* for index() */
	    for (i = 0; i < MAXOCLASSES; i++)
		if (!index(types, def_oc_syms[i])) {
		    *extra_types++ = def_oc_syms[i];
		    *extra_types = '\0';
		}

	    if(class_count > 1) {
		c = yn_function(prompt, types, '\0');
		savech(c);
		if(c == '\0') {
			clear_nhwindow(WIN_MESSAGE);
			return 0;
		}
	    } else {
		/* only one thing to itemize */
		if (unpaid_count)
		    c = 'u';
		else if (billx)
		    c = 'x';
		else
		    c = types[0];
	    }
	}
	if (c == 'x') {
	    if (billx)
		(void) doinvbill(1);
	    else
		pline("No used-up objects on your shopping bill.");
	    return 0;
	}
	if (c == 'u') {
	    if (unpaid_count)
		dounpaid();
	    else
		You("are not carrying any unpaid objects.");
	    return 0;
	}
	if (traditional) {
	    oclass = def_char_to_objclass(c); /* change to object class */
	    if (oclass == COIN_CLASS) {
		return doprgold();
	    } else if (index(types, c) > index(types, '\033')) {
		You("have no such objects.");
		return 0;
	    }
	    this_type = oclass;
	}
	if (query_objlist((char *) 0, invent,
		    (flags.invlet_constant ? USE_INVLET : 0)|INVORDER_SORT,
		    &pick_list, PICK_NONE, this_type_only) > 0)
	    free((void *)pick_list);
	return 0;
}

/* return a string describing the dungeon feature at <x,y> if there
   is one worth mentioning at that location; otherwise null */
const char *
dfeature_at(x, y, buf)
int x, y;
char *buf;
{
	struct rm *lev = &levl[x][y];
	int ltyp = lev->typ, cmap = -1;
	const char *dfeature = 0;
	static char altbuf[BUFSZ];

	if (IS_DOOR(ltyp)) {
	    switch (lev->doormask) {
	    case D_NODOOR:	cmap = S_ndoor; break;	/* "doorway" */
	    case D_ISOPEN:	cmap = S_vodoor; break;	/* "open door" */
	    case D_BROKEN:	dfeature = "broken door"; break;
	    default:	cmap = S_vcdoor; break;	/* "closed door" */
	    }
	    /* override door description for open drawbridge */
	    if (is_drawbridge_wall(x, y) >= 0)
		dfeature = "open drawbridge portcullis",  cmap = -1;
	} else if (IS_FOUNTAIN(ltyp))
	    dfeature = level.flags.lethe ? "sparkling fountain" : "fountain";
	else if (IS_THRONE(ltyp))
	    cmap = S_throne;				/* "opulent throne" */
	else if (IS_GRAVEWALL(ltyp))
	    cmap = S_gravewall;
	else if (IS_TUNNELWALL(ltyp))
	    cmap = S_tunnelwall;
	else if (IS_FARMLAND(ltyp))
	    cmap = S_farmland;
	else if (IS_MOUNTAIN(ltyp))
	    cmap = S_mountain;
	else if (IS_WATERTUNNEL(ltyp))
	    dfeature = level.flags.lethe ? "sparkling water tunnel" : "water tunnel";
	else if (IS_CRYSTALWATER(ltyp))
	    dfeature = level.flags.lethe ? "sparkling crystal water" : "crystal water";
	else if (IS_MOORLAND(ltyp))
	    cmap = S_moorland;
	else if (IS_URINELAKE(ltyp))
	    cmap = S_urinelake;
	else if (IS_SHIFTINGSAND(ltyp))
	    cmap = S_shiftingsand;
	else if (IS_STYXRIVER(ltyp))
	    cmap = S_styxriver;
	else if (IS_PENTAGRAM(ltyp))
	    cmap = S_pentagram;
	else if (IS_WELL(ltyp))
	    dfeature = level.flags.lethe ? "sparkling well" : "well";
	else if (IS_POISONEDWELL(ltyp))
	    dfeature = level.flags.lethe ? "sparkling poisoned well" : "poisoned well";
	else if (IS_WAGON(ltyp))
	    cmap = S_wagon;
	else if (IS_BURNINGWAGON(ltyp))
	    cmap = S_burningwagon;
	else if (IS_WOODENTABLE(ltyp))
	    cmap = S_woodentable;
	else if (IS_CARVEDBED(ltyp))
	    cmap = S_carvedbed;
	else if (IS_STRAWMATTRESS(ltyp))
	    cmap = S_strawmattress;
	else if (IS_SNOW(ltyp))
	    cmap = S_snow;
	else if (IS_ASH(ltyp))
	    cmap = S_ash;
	else if (IS_SAND(ltyp))
	    cmap = S_sand;
	else if (IS_PAVEDFLOOR(ltyp))
	    cmap = S_pavedfloor;
	else if (IS_HIGHWAY(ltyp))
	    cmap = S_highway;
	else if (IS_GRASSLAND(ltyp))
	    cmap = S_grassland;
	else if (IS_NETHERMIST(ltyp))
	    cmap = S_nethermist;
	else if (IS_STALACTITE(ltyp))
	    cmap = S_stalactite;
	else if (IS_CRYPTFLOOR(ltyp))
	    cmap = S_cryptfloor;
	else if (IS_BUBBLES(ltyp))
	    cmap = S_bubbles;
	else if (IS_RAINCLOUD(ltyp))
	    dfeature = level.flags.lethe ? "sparkling rain cloud" : "rain cloud";
	else if (IS_CLOUD(ltyp))
	    cmap = S_cloud;
	else if (is_lava(x,y))
	    cmap = S_lava;				/* "molten lava" */
	else if (is_ice(x,y))
	    cmap = S_ice;				/* "ice" */
	else if (is_pool(x,y))
	    dfeature = "pool of water";
	else if (IS_SINK(ltyp))
	    cmap = S_sink;				/* "sink" */
	else if (IS_TOILET(ltyp))
	    cmap = S_toilet;
	else if (IS_ALTAR(ltyp)) {
	    sprintf(altbuf, "altar to %s (%s)", a_gname(),
		    align_str(Amask2align(lev->altarmask & ~AM_SHRINE)));
	    dfeature = altbuf;
	} else if ((x == xupstair && y == yupstair) ||
		 (x == sstairs.sx && y == sstairs.sy && sstairs.up))
	    cmap = S_upstair;				/* "staircase up" */
	else if ((x == xdnstair && y == ydnstair) ||
		 (x == sstairs.sx && y == sstairs.sy && !sstairs.up))
	    cmap = S_dnstair;				/* "staircase down" */
	else if (x == xupladder && y == yupladder)
	    cmap = S_upladder;				/* "ladder up" */
	else if (x == xdnladder && y == ydnladder)
	    cmap = S_dnladder;				/* "ladder down" */
	else if (ltyp == DRAWBRIDGE_DOWN)
	    cmap = S_vodbridge;			/* "lowered drawbridge" */
	else if (ltyp == DBWALL)
	    cmap = S_vcdbridge;			/* "raised drawbridge" */
	else if (IS_GRAVE(ltyp))
	    cmap = S_grave;				/* "grave" */
	else if (ltyp == TREE)
	    cmap = S_tree;				/* "tree" */
	else if (ltyp == IRONBARS)
	    dfeature = "set of iron bars";

	if (cmap >= 0) dfeature = defsyms[cmap].explanation;
	if (dfeature) strcpy(buf, dfeature);
	return dfeature;
}

/* look at what is here; if there are many objects (5 or more),
   don't show them unless obj_cnt is 0 */
int
look_here(obj_cnt, picked_some)
int obj_cnt;	/* obj_cnt > 0 implies that autopickup is in progess */
boolean picked_some;
{
	struct obj *otmp;
	struct trap *trap;
	const char *verb = Blind ? "feel" : "see";
	const char *dfeature = (char*) 0;
	char fbuf[BUFSZ], fbuf2[BUFSZ];
	winid tmpwin;
	boolean skip_objects = (obj_cnt > iflags.pilesize), felt_cockatrice = FALSE;

	if (u.uswallow && u.ustuck) {
	    struct monst *mtmp = u.ustuck;
	    sprintf(fbuf, "Contents of %s %s",
		s_suffix(mon_nam(mtmp)), mbodypart(mtmp, STOMACH));
	    /* Skip "Contents of " by using fbuf index 12 */
	    You("%s to %s what is lying in %s.",
		Blind ? "try" : "look around", verb, &fbuf[12]);
	    otmp = mtmp->minvent;
	    if (otmp) {
		for ( ; otmp; otmp = otmp->nobj) {
			/* If swallower is an animal, it should have become stone but... */
			if (otmp->otyp == CORPSE || otmp->otyp == EGG) feel_cockatrice(otmp, FALSE);
		}
		if (Blind) strcpy(fbuf, "You feel");
		strcat(fbuf,":");
	    	(void) display_minventory(mtmp, MINV_ALL, fbuf);
	    } else {
		You("%s no objects here.", verb);
	    }
	    return(!!Blind);
	}
	if (!skip_objects && (trap = t_at(u.ux,u.uy)) && trap->tseen)
		There("is %s here.",
			an(defsyms[trap_to_defsym(trap->ttyp)].explanation));

	otmp = level.objects[u.ux][u.uy];
	dfeature = dfeature_at(u.ux, u.uy, fbuf2);
	if (dfeature && !strcmp(dfeature, "pool of water") && Underwater)
		dfeature = 0;

	if (Blind) {
		boolean drift = Is_airlevel(&u.uz) || Is_waterlevel(&u.uz);
		if (dfeature && !strncmp(dfeature, "altar ", 6)) {
		    /* don't say "altar" twice, dfeature has more info */
		    You("try to feel what is here.");
		} else {
		    You("try to feel what is %s%s.",
			drift ? "floating here" : "lying here on the ",
			drift ? ""		: surface(u.ux, u.uy));
		}
		if (dfeature && !drift && !strcmp(dfeature, surface(u.ux,u.uy)))
			dfeature = 0;		/* ice already identifed */
		if (!can_reach_floor()) {
			pline(Hallucination ? "But it seems the stuff actively tries to evade your grasp!" : "But you can't reach it!");
			return(0);
		}
	}

	if (dfeature) {
		sprintf(fbuf, "There is %s here.", an(dfeature));
		if (flags.suppress_alert < FEATURE_NOTICE_VER(0,0,7) &&
			(IS_FOUNTAIN(levl[u.ux][u.uy].typ) ||
			 IS_SINK(levl[u.ux][u.uy].typ) ||
			 IS_WELL(levl[u.ux][u.uy].typ) ||
			 IS_POISONEDWELL(levl[u.ux][u.uy].typ) ||
			 IS_TOILET(levl[u.ux][u.uy].typ)
			))
		    strcat(fbuf, "  Use \"q.\" to drink from it.");

		if (flags.suppress_alert < FEATURE_NOTICE_VER(0,0,7) && IS_THRONE(levl[u.ux][u.uy].typ))
		    strcat(fbuf, "  Use #sit to interact with it.");
		if (flags.suppress_alert < FEATURE_NOTICE_VER(0,0,7) && IS_CARVEDBED(levl[u.ux][u.uy].typ))
		    strcat(fbuf, "  Use #sit to interact with it.");
		if (flags.suppress_alert < FEATURE_NOTICE_VER(0,0,7) && IS_PENTAGRAM(levl[u.ux][u.uy].typ))
		    strcat(fbuf, "  Use #invoke to draw on the magical energies.");
		if (flags.suppress_alert < FEATURE_NOTICE_VER(0,0,7) && IS_ALTAR(levl[u.ux][u.uy].typ))
		    strcat(fbuf, "  Use #offer to make a sacrifice.");

	}

	if (!otmp || is_lava(u.ux,u.uy) || (is_waterypool(u.ux,u.uy) && !Underwater) || (is_watertunnel(u.ux,u.uy) && !Underwater)) {
		if (dfeature) pline(fbuf);
		sense_engr_at(u.ux, u.uy, FALSE); /* Eric Backus */
		if (!skip_objects && (Blind || !dfeature))
		    You("%s no objects here.", verb);
		return(!!Blind);
	}
	/* we know there is something here */

	if (skip_objects) {
	    if (dfeature) pline(fbuf);
	    sense_engr_at(u.ux, u.uy, FALSE); /* Eric Backus */
	    There("are %s%s objects here.",
		  (obj_cnt <= 10) ? "several" : "many",
		  picked_some ? " more" : "");
	} else if (!otmp->nexthere) {
	    /* only one object */
	    if (dfeature) pline(fbuf);
	    sense_engr_at(u.ux, u.uy, FALSE); /* Eric Backus */
	    if ((otmp->oinvis && !See_invisible) || otmp->oinvisreal) verb = "feel";
	    You("%s here %s.", verb, doname(otmp));
	    if (otmp->otyp == CORPSE || otmp->otyp == EGG) feel_cockatrice(otmp, FALSE);
	} else {
	    display_nhwindow(WIN_MESSAGE, FALSE);
	    tmpwin = create_nhwindow(NHW_MENU);
	    if(dfeature) {
		putstr(tmpwin, 0, fbuf);
		putstr(tmpwin, 0, "");
	    }
	    putstr(tmpwin, 0, Blind ? "Things that you feel here:" :
				      "Things that are here:");
	    for ( ; otmp; otmp = otmp->nexthere) {
		if ( (otmp->otyp == CORPSE || otmp->otyp == EGG) && will_feel_cockatrice(otmp, FALSE)) {
			char buf[BUFSZ];
			felt_cockatrice = TRUE;
			strcpy(buf, doname(otmp));
			strcat(buf, "...");
			putstr(tmpwin, 0, buf);
			break;
		}
		putstr(tmpwin, 0, doname(otmp));
	    }
	    display_nhwindow(tmpwin, TRUE);
	    destroy_nhwindow(tmpwin);
	    if (felt_cockatrice) feel_cockatrice(otmp, FALSE);
	    sense_engr_at(u.ux, u.uy, FALSE); /* Eric Backus */
	}
	return(!!Blind);
}

/* explicilty look at what is here, including all objects */
int
dolook()
{
	return look_here(0, FALSE);
}

boolean
will_feel_cockatrice(otmp, force_touch)
struct obj *otmp;
boolean force_touch;
{

	if (uarmg && (otmp->otyp == CORPSE && touch_petrifies(&mons[otmp->corpsenm])) && OBJ_DESCR(objects[uarmg->otyp]) && ( !strcmp(OBJ_DESCR(objects[uarmg->otyp]), "shitty gloves") || !strcmp(OBJ_DESCR(objects[uarmg->otyp]), "der'movyye perchatki") || !strcmp(OBJ_DESCR(objects[uarmg->otyp]), "boktan qo'lqop") ) ) {
		pline("Eek!");
		badeffect();
	}
	if (uarmg && (otmp->otyp == EGG && touch_petrifies(&mons[otmp->corpsenm])) && OBJ_DESCR(objects[uarmg->otyp]) && ( !strcmp(OBJ_DESCR(objects[uarmg->otyp]), "shitty gloves") || !strcmp(OBJ_DESCR(objects[uarmg->otyp]), "der'movyye perchatki") || !strcmp(OBJ_DESCR(objects[uarmg->otyp]), "boktan qo'lqop") ) ) {
		pline("Eek!");
		badeffect();
	}

	if ((Blind || force_touch) && (!uarmg || FingerlessGloves) && !Stone_resistance &&
		(otmp->otyp == CORPSE && touch_petrifies(&mons[otmp->corpsenm])))
			return TRUE;
	if ((Blind || force_touch) && (!uarmg || FingerlessGloves) && !Stone_resistance &&
		(otmp->otyp == EGG && touch_petrifies(&mons[otmp->corpsenm])))
			return TRUE;
	return FALSE;
}

void
feel_cockatrice(otmp, force_touch)
struct obj *otmp;
boolean force_touch;
{
	char kbuf[BUFSZ];

	if (will_feel_cockatrice(otmp, force_touch)) {
	    if(poly_when_stoned(youmonst.data))
			You("touched the %s corpse with your bare %s.",
				mons[otmp->corpsenm].mname, makeplural(body_part(HAND)));
	    else
			pline("Touching the %s corpse is a fatal mistake...",
				mons[otmp->corpsenm].mname);
		sprintf(kbuf, "%s corpse", an(mons[otmp->corpsenm].mname));
		instapetrify(kbuf);
	}
}

#endif /* OVLB */
#ifdef OVL1

void
stackobj(obj)
struct obj *obj;
{
	struct obj *otmp;

	for(otmp = level.objects[obj->ox][obj->oy]; otmp; otmp = otmp->nexthere)
		if(otmp != obj && merged(&obj,&otmp))
			break;
	return;
}

STATIC_OVL boolean
mergable(otmp, obj)	/* returns TRUE if obj  & otmp can be merged */
/* obj is being merged into otmp --Amy */
	register struct obj *otmp, *obj;
{
	struct monst *mtmp;

	if (obj == uchain) return FALSE;
	if (obj == uball) return FALSE;
	if (otmp == uchain) return FALSE;
	if (otmp == uball) return FALSE;

	if (obj->otyp != otmp->otyp) return FALSE;
#ifdef GOLDOBJ
	/* coins of the same kind will always merge */
	if (obj->oclass == COIN_CLASS) return TRUE;
#endif
	if (obj->unpaid != otmp->unpaid ||
	    obj->spe != otmp->spe || (obj->dknown && !otmp->dknown) ||
	    (obj->bknown && !otmp->bknown && !(Role_if(PM_PRIEST) || Role_if(PM_NECROMANCER) || Role_if(PM_CHEVALIER) || Race_if(PM_VEELA) ) ) ||
	    obj->cursed != otmp->cursed || obj->blessed != otmp->blessed ||
	    obj->hvycurse != otmp->hvycurse || obj->prmcurse != otmp->prmcurse ||
	    obj->stckcurse != otmp->stckcurse ||
	    obj->morgcurse != otmp->morgcurse || obj->evilcurse != otmp->evilcurse || obj->bbrcurse != otmp->bbrcurse ||
	    obj->no_charge != otmp->no_charge ||
	    obj->selfmade != otmp->selfmade  ||
	    obj->finalcancel != otmp->finalcancel ||
	    obj->obroken != otmp->obroken ||
	    obj->otrapped != otmp->otrapped ||
	    obj->lamplit != otmp->lamplit ||
	    flags.pickup_thrown && obj->was_thrown != otmp->was_thrown ||
	    obj->oinvis != otmp->oinvis ||
	    obj->oinvisreal != otmp->oinvisreal ||
	    obj->oldtyp != otmp->oldtyp ||
	    obj->greased != otmp->greased ||
	    obj->oeroded != otmp->oeroded ||
	    obj->oeroded2 != otmp->oeroded2 ||
	    obj->bypass != otmp->bypass)
	    return(FALSE);

	if ((obj->oclass==WEAPON_CLASS || obj->oclass==ARMOR_CLASS) &&
	    (obj->oerodeproof!=otmp->oerodeproof || (obj->rknown && !otmp->rknown) ))
	    return FALSE;

	if (obj->oclass == FOOD_CLASS && (obj->oeaten != otmp->oeaten ||
	  obj->odrained != otmp->odrained || obj->orotten != otmp->orotten))
	    return(FALSE);

	if (obj->otyp == CORPSE || obj->otyp == EGG || obj->otyp == TIN) {
		if (obj->corpsenm != otmp->corpsenm)
				return FALSE;
	}

	/* armed grenades do not merge */
	if ((obj->timed || otmp->timed) && is_grenade(obj))
	    return FALSE;

	/* hatching eggs don't merge; ditto for revivable corpses */
	if ((obj->timed || otmp->timed) && (obj->otyp == EGG ||
	    (obj->otyp == CORPSE && otmp->corpsenm >= LOW_PM &&
		 (is_reviver(&mons[otmp->corpsenm]) ||
		 ((((mtmp = get_mtraits(otmp, FALSE)) != (struct monst *)0) ) && mtmp->egotype_troll)
		 )  )))
	    return FALSE;

	/* allow candle merging only if their ages are close */
	/* see begin_burn() for a reference for the magic "25" */
	/* [ALI] Slash'EM can't rely on using 25, because we
	 * have chosen to reduce the cost of candles such that
	 * the initial age is no longer a multiple of 25. The
	 * simplest solution is just to use 20 instead, since
	 * initial candle age is always a multiple of 20.
	 */
	if ((obj->otyp == TORCH || Is_candle(obj)) && obj->age/20 != otmp->age/20)
	    return(FALSE);

	/* burning potions of oil never merge */
	/* MRKR: nor do burning torches */
	if ((obj->otyp == POT_OIL || obj->otyp == TORCH) && obj->lamplit)
	    return FALSE;

	/* don't merge surcharged item with base-cost item */
	if (obj->unpaid && !same_price(obj, otmp))
	    return FALSE;

	/* if they have names, make sure they're the same */
	if ( (obj->onamelth != otmp->onamelth &&
		((obj->onamelth && otmp->onamelth) || obj->otyp == CORPSE)
	     ) ||
	    (obj->onamelth && otmp->onamelth &&
		    strncmp(ONAME(obj), ONAME(otmp), (int)obj->onamelth)))
		return FALSE;

	/* for the moment, any additional information is incompatible */
	if (obj->oxlth || otmp->oxlth) return FALSE;

	if(obj->oartifact != otmp->oartifact) return FALSE;

	if(obj->known == otmp->known || (otmp->known) ||
		!objects[otmp->otyp].oc_uses_known) {
		return((boolean)(objects[obj->otyp].oc_merge));
	} else return(FALSE);
}

/* Manipulating a stack of items is supposed to fail if the stack is very big. --Amy
 * This sounds evil, but if you think about it for a while it makes sense: why should a scroll of enchant weapon
 * have the same odds of enchanting a stack of 5 or 500 darts? That way, players would be well-advised to never use them
 * because in the case of doubt they'll find more darts to make an even bigger stack to enchant all at once!
 * And the vanilla behavior also means that finding a random stack of +5 darts is of no use since you can always make
 * a much bigger one with a few scrolls. On the other hand, water damage, cancellation etc. has the same chance of
 * ruining your stack of 15 teleportation scrolls all at once, which doesn't really make sense either. The best
 * solution would be allowing each individual item to perform a saving throw to see whether it is affected,
 * but lacking that, I'll just allow stacks to perform a saving throw against manipulation.
 * It will affect both "positive" and "negative" effects equally. */

boolean
stack_too_big(otmp)
register struct obj *otmp;
{
	/* returns 0 if the operation can be done on the stack, 1 if it will fail */

	/* In Soviet Russia, people would probably hate this change too, because they somehow hate everything that's changed from SLASH'EM. Why they don't simply play SLASH'EM then, I will never understand. But I guess that if they want to waste their time on reverting every single one of Extended's changes, we'll let them do so. */
	if (issoviet) return 0;

	if (!objects[otmp->otyp].oc_merge) return 0;

	if ( ( (objects[otmp->otyp].oc_skill == P_DAGGER) || (objects[otmp->otyp].oc_skill == P_KNIFE) || (objects[otmp->otyp].oc_skill == P_SPEAR) || (objects[otmp->otyp].oc_skill == P_JAVELIN) || (objects[otmp->otyp].oc_skill == P_BOOMERANG) || (objects[otmp->otyp].oc_skill == -P_BOOMERANG) || (otmp->otyp == WAX_CANDLE) || (otmp->otyp == JAPAN_WAX_CANDLE) || (otmp->otyp == OIL_CANDLE) || (otmp->otyp == UNAFFECTED_CANDLE) || (otmp->otyp == SPECIFIC_CANDLE) || (otmp->otyp == __CANDLE) || (otmp->otyp == GENERAL_CANDLE) || (otmp->otyp == NATURAL_CANDLE) || (otmp->otyp == UNSPECIFIED_CANDLE) || (otmp->otyp == TALLOW_CANDLE) || (otmp->otyp == MAGIC_CANDLE) || (otmp->otyp == TORCH) ) && (rnd(otmp->quan) > 10 ) ) return 1;

	if ( ( (objects[otmp->otyp].oc_skill == P_DART) || (objects[otmp->otyp].oc_skill == P_SHURIKEN) || (objects[otmp->otyp].oc_skill == -P_DART) || (objects[otmp->otyp].oc_skill == -P_SHURIKEN) || (objects[otmp->otyp].oc_skill == -P_BOW) || (objects[otmp->otyp].oc_skill == -P_SLING) || (objects[otmp->otyp].oc_skill == -P_CROSSBOW) || (objects[otmp->otyp].oc_skill == -P_FIREARM) || (otmp->otyp == SPOON) || (objects[otmp->otyp].oc_class == VENOM_CLASS) ) && (rnd(otmp->quan) > 25 ) ) return 1;

	if ( ( (objects[otmp->otyp].oc_class == SCROLL_CLASS) || (objects[otmp->otyp].oc_class == POTION_CLASS) || (objects[otmp->otyp].oc_class == FOOD_CLASS)) && (rnd(otmp->quan) > 1 ) ) return 1;


	else return 0;
}


int
doprgold()
{
	/* the messages used to refer to "carrying gold", but that didn't
	   take containers into account */
#ifndef GOLDOBJ
	if(!u.ugold)
	    Your("wallet is empty.");
	else
	    Your("wallet contains %ld gold piece%s.", u.ugold, plur(u.ugold));
#else
        long umoney = money_cnt(invent);
	if(!umoney)
	    Your("wallet is empty.");
	else
	    Your("wallet contains %ld %s.", umoney, currency(umoney));
#endif
	shopper_financial_report();
	return 0;
}

#endif /* OVL1 */
#ifdef OVLB

int
doprwep()
{
    if (!uwep) {
	if (!u.twoweap){
	You("are empty %s.", body_part(HANDED));
	    return 0;
	}
	/* Avoid printing "right hand empty" and "other hand empty" */
	if (!uswapwep) {
	    You("are attacking with both %s.", makeplural(body_part(HAND)));
	    return 0;
	}
	Your("right %s is empty.", body_part(HAND));
    } else {
	prinv((char *)0, uwep, 0L);
    }
    if (u.twoweap) {
    	if (uswapwep)
    	    prinv((char *)0, uswapwep, 0L);
    	else
    	    Your("other %s is empty.", body_part(HAND));
    }
    return 0;
#if 0
	if(!uwep && !uswapwep && !uquiver) You("are empty %s.", body_part(HANDED));
	else {
		char lets[3];
		register int ct = 0;

		if(uwep) lets[ct++] = obj_to_let(uwep);
		if(uswapwep) lets[ct++] = obj_to_let(uswapwep);
		if(uquiver) lets[ct++] = obj_to_let(uquiver);
		lets[ct] = 0;
		(void) display_inventory(lets, FALSE);
    }
    return 0;
#endif
}

int
doprarm()
{
	if(!wearing_armor())
		You("are not wearing any armor.");
	else {
		char lets[8];
		register int ct = 0;

		if(uarmu) lets[ct++] = obj_to_let(uarmu);
		if(uarm) lets[ct++] = obj_to_let(uarm);
		if(uarmc) lets[ct++] = obj_to_let(uarmc);
		if(uarmh) lets[ct++] = obj_to_let(uarmh);
		if(uarms) lets[ct++] = obj_to_let(uarms);
		if(uarmg) lets[ct++] = obj_to_let(uarmg);
		if(uarmf) lets[ct++] = obj_to_let(uarmf);
		lets[ct] = 0;
		(void) display_inventory(lets, FALSE);
	}
	return 0;
}

int
doprring()
{
	if(!uleft && !uright)
		You("are not wearing any rings.");
	else {
		char lets[3];
		register int ct = 0;

		if(uleft) lets[ct++] = obj_to_let(uleft);
		if(uright) lets[ct++] = obj_to_let(uright);
		lets[ct] = 0;
		(void) display_inventory(lets, FALSE);
	}
	return 0;
}

int
dopramulet()
{
	if (!uamul)
		You("are not wearing an amulet.");
	else
		prinv((char *)0, uamul, 0L);
	return 0;
}

STATIC_OVL boolean
tool_in_use(obj)
struct obj *obj;
{
	if ((obj->owornmask & (W_TOOL
			| W_SADDLE
			)) != 0L) return TRUE;
	if (obj->oclass != TOOL_CLASS) return FALSE;
	return (boolean)(obj == uwep || obj->lamplit ||
				((obj->otyp == LEATHER_LEASH || obj->otyp == INKA_LEASH) && obj->leashmon));
}

int
doprtool()
{
	struct obj *otmp;
	int ct = 0;
	char lets[52+1];

	for (otmp = invent; otmp; otmp = otmp->nobj)
	    if (tool_in_use(otmp))
		lets[ct++] = obj_to_let(otmp);
	lets[ct] = '\0';
	if (!ct) You("are not using any tools.");
	else (void) display_inventory(lets, FALSE);
	return 0;
}

/* '*' command; combines the ')' + '[' + '=' + '"' + '(' commands;
   show inventory of all currently wielded, worn, or used objects */
int
doprinuse()
{
	struct obj *otmp;
	int ct = 0;
	char lets[52+1];

	for (otmp = invent; otmp; otmp = otmp->nobj)
	    if (is_worn(otmp) || tool_in_use(otmp))
		lets[ct++] = obj_to_let(otmp);
	lets[ct] = '\0';
	if (!ct) You("are not wearing or wielding anything.");
	else (void) display_inventory(lets, FALSE);
	return 0;
}

/*
 * uses up an object that's on the floor, charging for it as necessary
 */
void
useupf(obj, numused)
register struct obj *obj;
long numused;
{
	register struct obj *otmp;
	boolean at_u = (obj->ox == u.ux && obj->oy == u.uy);

	/* burn_floor_paper() keeps an object pointer that it tries to
	 * useupf() multiple times, so obj must survive if plural */
	if (obj->quan > numused) {
		otmp = splitobj(obj, numused);
		obj->in_use = FALSE;		/* rest no longer in use */
	}
	else
		otmp = obj;
	if(costly_spot(otmp->ox, otmp->oy)) {
	    if(index(u.urooms, *in_rooms(otmp->ox, otmp->oy, 0)))
	        addtobill(otmp, FALSE, FALSE, FALSE);
	    else (void)stolen_value(otmp, otmp->ox, otmp->oy, FALSE, FALSE,
		    TRUE);
	}
	delobj(otmp);
	if (at_u && u.uundetected && (hides_under(youmonst.data) || (uarmh && OBJ_DESCR(objects[uarmh->otyp]) && ( !strcmp(OBJ_DESCR(objects[uarmh->otyp]), "secret helmet") || !strcmp(OBJ_DESCR(objects[uarmh->otyp]), "sekret shlem") || !strcmp(OBJ_DESCR(objects[uarmh->otyp]), "yashirin dubulg'a") ) ) || (uarmc && uarmc->oartifact == ART_JANA_S_EXTREME_HIDE_AND_SE) ) )
	    u.uundetected = OBJ_AT(u.ux, u.uy);
}

#endif /* OVLB */


#ifdef OVL1

/*
 * Conversion from a class to a string for printing.
 * This must match the object class order.
 */
STATIC_VAR NEARDATA const char *names[] = { 0,
	"Illegal objects", "Weapons", "Armor", "Rings", "Amulets", "Implants",
	"Tools", "Comestibles", "Potions", "Scrolls", "Spellbooks",
	"Wands", "Coins", "Gems", "Boulders/Statues", "Iron balls",
	"Chains", "Venoms"
};

static NEARDATA const char oth_symbols[] = {
	CONTAINED_SYM,
	'\0'
};

static NEARDATA const char *oth_names[] = {
	"Bagged/Boxed items"
};

static NEARDATA char *invbuf = (char *)0;
static NEARDATA unsigned invbufsiz = 0;

char *
let_to_name(let,unpaid,showsym)
char let;
boolean unpaid,showsym;
{
	static const char *ocsymformat = "%s('%c')";
	const char *class_name;
	const char *pos;
	int oclass = (let >= 1 && let < MAXOCLASSES) ? let : 0;
	unsigned len;

	if (oclass)
	    class_name = names[oclass];
	else if ((pos = index(oth_symbols, let)) != 0)
	    class_name = oth_names[pos - oth_symbols];
	else
	    class_name = names[0];

	len = strlen(class_name) + (unpaid ? sizeof "unpaid_" : sizeof "") +
	    ((oclass && showsym) ? strlen(ocsymformat) : 0);
	if (len > invbufsiz) {
	    if (invbuf) free((void *)invbuf);
	    invbufsiz = len + 10; /* add slop to reduce incremental realloc */
	    invbuf = (char *) alloc(invbufsiz);
	}
	if (unpaid)
	    strcat(strcpy(invbuf, "Unpaid "), class_name);
	else
	    strcpy(invbuf, class_name);
	if (oclass && showsym)
	    sprintf(eos(invbuf), ocsymformat,
		    iflags.menu_tab_sep ? "\t" : "  ", def_oc_syms[let]);
	return invbuf;
}

void
free_invbuf()
{
	if (invbuf) free((void *)invbuf),  invbuf = (char *)0;
	invbufsiz = 0;
}

#endif /* OVL1 */
#ifdef OVLB

void
reassign()
{
	register int i;
	register struct obj *obj;

	for(obj = invent, i = 0; obj; obj = obj->nobj, i++)
		obj->invlet = (i < 26) ? ('a'+i) : ('A'+i-26);
	lastinvnr = i;
}

#endif /* OVLB */
#ifdef OVL1

int
domarkforpet()
{
	struct obj *obj;
	pline("Select an item that you don't want to be dropped if your pet is holding it.");
	if (!(obj = getobj(all_count,"mark"))) return(0);

	if (obj->unpaid) {
		pline("You don't own it yet!");
		return(0);
	}

	if (obj->petmarked) {
		pline("The object is no longer marked as undroppable for your pet.");
		obj->petmarked = 0;
	} else {
		pline("The object is now marked, so your pet will hold onto the item if it picks it up.");
		obj->petmarked = 1;
	}

	return(0);
}

int
doorganize()	/* inventory organizer by Del Lamb */
{
	struct obj *obj, *otmp;
	register int ix, cur;
	register char let;
	char alphabet[52+1], buf[52+1];
	char qbuf[QBUFSZ];
	char allowall[2];
	const char *adj_type;

	if (!flags.invlet_constant) reassign();
	/* get a pointer to the object the user wants to organize */
	allowall[0] = ALL_CLASSES; allowall[1] = '\0';
	if (!(obj = getobj(allowall,"adjust"))) return(0);

	/* initialize the list with all upper and lower case letters */
	for (let = 'a', ix = 0;  let <= 'z';) alphabet[ix++] = let++;
	for (let = 'A', ix = 26; let <= 'Z';) alphabet[ix++] = let++;
	alphabet[52] = 0;

	/* blank out all the letters currently in use in the inventory */
	/* except those that will be merged with the selected object   */
	for (otmp = invent; otmp; otmp = otmp->nobj)
		if (otmp != obj && !mergable(otmp,obj)) {
			if (otmp->invlet <= 'Z')
				alphabet[(otmp->invlet) - 'A' + 26] = ' ';
			else	alphabet[(otmp->invlet) - 'a']	    = ' ';
		}

	/* compact the list by removing all the blanks */
	for (ix = cur = 0; ix <= 52; ix++)
		if (alphabet[ix] != ' ') buf[cur++] = alphabet[ix];

	/* and by dashing runs of letters */
	if(cur > 5) compactify(buf);

	/* get new letter to use as inventory letter */
	for (;;) {
		sprintf(qbuf, "Adjust letter to what [%s]?",buf);
		let = yn_function(qbuf, (char *)0, '\0');
		if(index(quitchars,let)) {
			pline(Never_mind);
			return(0);
		}
		if (let == '@' || !letter(let))
			pline("Select an inventory slot letter.");
		else
			break;
	}

	/* change the inventory and print the resulting item */
	adj_type = "Moving:";

	/*
	 * don't use freeinv/addinv to avoid double-touching artifacts,
	 * dousing lamps, losing luck, cursing loadstone, etc.
	 */
	extract_nobj(obj, &invent);

	for (otmp = invent; otmp;)
		if (merged(&otmp,&obj)) {
			adj_type = "Merging:";
			obj = otmp;
			otmp = otmp->nobj;
			extract_nobj(obj, &invent);
		} else {
			if (otmp->invlet == let) {
				adj_type = "Swapping:";
				otmp->invlet = obj->invlet;
			}
			otmp = otmp->nobj;
		}

	/* inline addinv (assuming flags.invlet_constant and !merged) */
	obj->invlet = let;
	obj->nobj = invent; /* insert at beginning */
	obj->where = OBJ_INVENT;
	invent = obj;
	reorder_invent();

	prinv(adj_type, obj, 0L);
	update_inventory();
	return(0);
}

/* common to display_minventory and display_cinventory */
STATIC_OVL void
invdisp_nothing(hdr, txt)
const char *hdr, *txt;
{
	winid win;
	anything any;
	menu_item *selected;

	any.a_void = 0;
	win = create_nhwindow(NHW_MENU);
	start_menu(win);
	add_menu(win, NO_GLYPH, &any, 0, 0, iflags.menu_headings, hdr, MENU_UNSELECTED);
	add_menu(win, NO_GLYPH, &any, 0, 0, ATR_NONE, "", MENU_UNSELECTED);
	add_menu(win, NO_GLYPH, &any, 0, 0, ATR_NONE, txt, MENU_UNSELECTED);
	end_menu(win, (char *)0);
	if (select_menu(win, PICK_NONE, &selected) > 0)
	    free((void *)selected);
	destroy_nhwindow(win);
	return;
}

/* query_objlist callback: return things that could possibly be worn/wielded */
STATIC_OVL boolean
worn_wield_only(obj)
struct obj *obj;
{
    return (obj->oclass == WEAPON_CLASS
		|| obj->oclass == ARMOR_CLASS
		|| obj->oclass == AMULET_CLASS
		|| obj->oclass == IMPLANT_CLASS
		|| obj->oclass == RING_CLASS
		|| obj->oclass == TOOL_CLASS);
}

/*
 * Display a monster's inventory.
 * Returns a pointer to the object from the monster's inventory selected
 * or NULL if nothing was selected.
 *
 * By default, only worn and wielded items are displayed.  The caller
 * can pick one.  Modifier flags are:
 *
 *	MINV_NOLET	- nothing selectable
 *	MINV_ALL	- display all inventory
 */
struct obj *
display_minventory(mon, dflags, title)
register struct monst *mon;
int dflags;
char *title;
{
	struct obj *ret;
#ifndef GOLDOBJ
	struct obj m_gold;
#endif
	char tmp[QBUFSZ];
	int n;
	menu_item *selected = 0;
#ifndef GOLDOBJ
	int do_all = (dflags & MINV_ALL) != 0,
	    do_gold = (do_all && mon->mgold);
#else
	int do_all = (dflags & MINV_ALL) != 0;
#endif

	sprintf(tmp,"%s %s:", s_suffix(noit_Monnam(mon)),
		do_all ? "possessions" : "armament");

#ifndef GOLDOBJ
	if (do_all ? (mon->minvent || mon->mgold)
#else
	if (do_all ? (mon->minvent != 0)
#endif
		   : (mon->misc_worn_check || MON_WEP(mon))) {
	    /* Fool the 'weapon in hand' routine into
	     * displaying 'weapon in claw', etc. properly.
	     */
	    youmonst.data = mon->data;

#ifndef GOLDOBJ
	    if (do_gold) {
		/*
		 * Make temporary gold object and insert at the head of
		 * the mon's inventory.  We can get away with using a
		 * stack variable object because monsters don't carry
		 * gold in their inventory, so it won't merge.
		 */
		m_gold = zeroobj;
		m_gold.otyp = GOLD_PIECE;  m_gold.oclass = COIN_CLASS;
		m_gold.quan = mon->mgold;  m_gold.dknown = 1;
		m_gold.where = OBJ_FREE;
		/* we had better not merge and free this object... */
		if (add_to_minv(mon, &m_gold))
		    panic("display_minventory: static object freed.");
	    }

#endif
	    n = query_objlist(title ? title : tmp, mon->minvent, INVORDER_SORT, &selected,
			(dflags & MINV_NOLET) ? PICK_NONE : PICK_ONE,
			do_all ? allow_all : worn_wield_only);

#ifndef GOLDOBJ
	    if (do_gold) obj_extract_self(&m_gold);
#endif

	    set_uasmon();
	} else {
	    invdisp_nothing(title ? title : tmp, "(none)");
	    n = 0;
	}

	if (n > 0) {
	    ret = selected[0].item.a_obj;
	    free((void *)selected);
#ifndef GOLDOBJ
	    /*
	     * Unfortunately, we can't return a pointer to our temporary
	     * gold object.  We'll have to work out a scheme where this
	     * can happen.  Maybe even put gold in the inventory list...
	     */
	    if (ret == &m_gold) ret = (struct obj *) 0;
#endif
	} else
	    ret = (struct obj *) 0;
	return ret;
}

/*
 * Display the contents of a container in inventory style.
 * Currently, this is only used for statues, via wand of probing.
 * [ALI] Also used when looting medical kits.
 */
struct obj *
display_cinventory(obj)
register struct obj *obj;
{
	struct obj *ret;
	char tmp[QBUFSZ];
	int n;
	menu_item *selected = 0;

	sprintf(tmp,"Contents of %s:", doname(obj));

	if (obj->cobj) {
	    n = query_objlist(tmp, obj->cobj, INVORDER_SORT, &selected,
			    PICK_NONE, allow_all);
	} else {
	    invdisp_nothing(tmp, "(empty)");
	    n = 0;
	}
	if (n > 0) {
	    ret = selected[0].item.a_obj;
	    free((void *)selected);
	} else
	    ret = (struct obj *) 0;
	return ret;
}

/* query objlist callback: return TRUE if obj is at given location */
static coord only;

STATIC_OVL boolean
only_here(obj)
    struct obj *obj;
{
    return (obj->ox == only.x && obj->oy == only.y);
}

/*
 * Display a list of buried items in inventory style.  Return a non-zero
 * value if there were items at that spot.
 *
 * Currently, this is only used with a wand of probing zapped downwards.
 */
int
display_binventory(x, y, as_if_seen)
int x, y;
boolean as_if_seen;
{
	struct obj *obj;
	menu_item *selected = 0;
	int n;

	/* count # of objects here */
	for (n = 0, obj = level.buriedobjlist; obj; obj = obj->nobj)
	    if (obj->ox == x && obj->oy == y) {
		if (as_if_seen) obj->dknown = 1;
		n++;
	    }

	if (n) {
	    only.x = x;
	    only.y = y;
	    if (query_objlist("Things that are buried here:",
			      level.buriedobjlist, INVORDER_SORT,
			      &selected, PICK_NONE, only_here) > 0)
		free((void *)selected);
	    only.x = only.y = 0;
	}
	return n;
}

/* Itemactions function stolen from Unnethack. I'll just print info about the item though. --Amy */
int
itemactions(obj)
struct obj *obj;
{

	if (Hallucination) {

	pline("%s - This item radiates in an array of beautiful colors. It's very mesmerizing.",xname(obj) );

	return 0;
	}

	if (PlayerUninformation) {

	pline("%s - This is the best item in the game if you know how to use it. Good luck making it work!",xname(obj) );

	return 0;

	}

	register int typ = obj->otyp;
	register struct objclass *ocl = &objects[typ];
	register int nn = (ocl->oc_name_known && obj->dknown);
	register const char *dn = OBJ_DESCR(*ocl);

	switch (obj->oclass) {

		case WEAPON_CLASS:
#ifdef PHANTOM_CRASH_BUG
		pline("%s - Color: %s. Material: %s. Appearance: %s.",xname(obj), obj->dknown ? c_obj_colors[objects[obj->otyp].oc_color] : "unknown", obj->dknown ? materialnm[objects[obj->otyp].oc_material] : "unknown", obj->dknown ? dn : "unknown" );
#else
		pline("%s - This is a weapon. Color: %s. Material: %s. Appearance: %s. You can wield it to attack enemies. Some weapons are also suitable for throwing.",xname(obj), obj->dknown ? c_obj_colors[objects[obj->otyp].oc_color] : "unknown", obj->dknown ? materialnm[objects[obj->otyp].oc_material] : "unknown", obj->dknown ? dn : "unknown" );
#endif
		if (!nn) pline("Unfortunately you don't know more about it. You will gain more information if you identify this item.");
		else { 

			switch (obj->otyp) {

			case ORCISH_DAGGER: 
				pline("A crappy dagger that doesn't do much damage. It can be thrown."); break;
			case DAGGER: 
				pline("A basic dagger that doesn't do much damage. It can be thrown."); break;
			case DROVEN_DAGGER: 
				pline("This dagger does a lot of damage but breaks when thrown."); break;
			case ATHAME: 
				pline("A high-quality dagger that can create hard engravings. It can be thrown."); break;
			case MERCURIAL_ATHAME: 
				pline("This silver dagger can create hard engravings. It can be thrown."); break;
			case SILVER_DAGGER: 
				pline("A dagger that does extra damage to undead. It can be thrown."); break;
			case ELVEN_DAGGER: 
				pline("Elven daggers do slightly more damage than standard daggers. It can be thrown."); break;
			case DARK_ELVEN_DAGGER: 
				pline("A good dagger that does respectable damage. It can be thrown."); break;
			case WOODEN_STAKE: 
				pline("A quite powerful dagger. It can be thrown."); break;
			case GREAT_DAGGER: 
				pline("Used to be the strongest dagger in the game, but now gets surpassed by droven daggers. Still stronger than the average dagger though. It can be thrown."); break;
			case WORM_TOOTH: 
				pline("A crappy knife that can be turned into a very powerful knife if enchanted. It can be thrown."); break;
			case KNIFE: 
				pline("A totally piece of crap weapon. It can be thrown."); break;
			case SURVIVAL_KNIFE: 
				pline("A knife that deals extra damage to animals. It can be thrown."); break;
			case STILETTO: 
				pline("This knife is more likely to hit than a regular knife, but it's still awfully weak. It can be thrown."); break;
			case SCALPEL: 
				pline("Don't bother with this knife-class weapon. The surgery technique works better if you have it in your inventory, though. It can be thrown."); break;
			case CRYSKNIFE: 
				pline("A magically enchanted knife that does superb damage. It can be thrown."); break;
			case TOOTH_OF_AN_ALGOLIAN_SUNTIGER: 
				pline("This razor-sharp knife cuts meat like butter. It can be thrown."); break;
			case AXE: 
				pline("A standard axe that does moderate damage. It can be used to chop down trees."); break;
			case OBSIDIAN_AXE: 
				pline("This glass axe does good damage to large monsters. It can be used to chop down trees."); break;
			case MOON_AXE: 
				pline("This silver axe does more damage than a standard axe and is super-effective versus undead. It can be used to chop down trees."); break;
			case BATTLE_AXE: 
				pline("A heavy two-handed axe that does moderate damage. It can be used to chop down trees."); break;
			case DWARVISH_BATTLE_AXE: 
				pline("The heavy hitter among the axes that can chop down most monsters in a few hits. It can be used to chop down trees."); break;
			case DWARVISH_MATTOCK: 
				pline("A two-handed pick-axe that can do a lot of damage. It can be used for digging."); break;
			case ORCISH_SHORT_SWORD: 
				pline("The weakest short sword in the game. It is inferior to other short swords in every way."); break;
			case SHORT_SWORD: 
				pline("A basic short sword that doesn't deal a lot of damage."); break;
			case VIBROBLADE: 
				pline("This object looks like a penis (LOL) and is made of plastic but otherwise it's exactly the same as a bog-standard short sword."); break;
			case DROVEN_SHORT_SWORD: 
				pline("Ever wanted a short sword that can actually hit armored enemies? Then this is for you. Don't throw it though."); break;
			case SILVER_SHORT_SWORD: 
				pline("A basic short sword that doesn't deal a lot of damage. It is effective against undead."); break;
			case DWARVISH_SHORT_SWORD: 
				pline("A stronger version of the regular short sword."); break;
			case ELVEN_SHORT_SWORD: 
				pline("This short sword is definitely better than a regular short sword."); break;
			case DARK_ELVEN_SHORT_SWORD: 
				pline("If your weapon type of choice reads 'short sword', use this. It outdamages all other short swords in the game."); break;
			case BROADSWORD: 
				pline("A standard broadsword. It does more damage than a short sword but less than a long sword."); break;
			case RUNESWORD: 
				pline("This weapon is basically a broadsword, with the exact same stats."); break;
			case BLACK_AESTIVALIS: 
				pline("A useful broadsword."); break;
			case WHITE_FLOWER_SWORD: 
				pline("A wooden broadsword with good damage bonus."); break;
			case ELVEN_BROADSWORD: 
				pline("Far better than a regular broadsword, this weapon has the highest base damage among all broadswords in the game."); break;
			case LONG_SWORD: 
				pline("A basic long sword that does respectable damage."); break;
			case CRYSTAL_SWORD: 
				pline("A basic long sword that breaks when thrown. Use it in melee instead."); break;
			case SILVER_LONG_SWORD: 
				pline("A long sword that does respectable damage, with a bonus against undead."); break;
			case KATANA: 
				pline("This Japanese long sword can deal more damage than a regular long sword."); break;
			case SUGUHANOKEN: 
				pline("A totally shitty longsword. You should replace this with a real longsword!"); break;
			case GREAT_HOUCHOU: 
				pline("Don't be fooled by its name. This thing is basically a longsword that does less damage."); break;
			case ELECTRIC_SWORD: 
				pline("The most powerful of the long swords. It can be applied to bash iron bars."); break;
			case TWO_HANDED_SWORD: 
				pline("It's heavy and requires both hands, but does quite a lot of damage."); break;
			case TSURUGI: 
				pline("A long samurai sword that can only be wielded with both hands. It does lots of damage."); break;
			case CHAINSWORD: 
				pline("A golden two-handed sword that deals enormous amounts of damage."); break;
			case BASTERD_SWORD: 
				pline("This huge fucking sword can make short work of anything that tries to oppose you. However, you have to wield it with two hands."); break;
			case DROVEN_GREATSWORD: 
				pline("It doesn't actually bisect enemies, but it deals a ton and a half of damage. Basically, it's like you were wielding a tank. It requires both hands though."); break;
			case SCIMITAR: 
				pline("A light but useful blade, the scimitar can outdamage a standard short sword."); break;
			case BENT_SABLE: 
				pline("This sharpened scimitar is actually very useful for quickly cutting up your enemies."); break;
			case HIGH_ELVEN_WARSWORD: 
				pline("An elven scimitar that does more damage than a regular scimitar and also hits more often."); break;
			case RAPIER: 
				pline("A basic saber that's not stronger than a short sword."); break;
			case IRON_SABER: 
				pline("It's a saber made of iron that does moderate damage."); break;
			case SILVER_SABER: 
				pline("This saber does moderate damage, but unlike most other weapons it's super-effective against undead."); break;
			case GOLDEN_SABER: 
				pline("A rare saber made of pure gold. It can do good damage. It can be applied to bash iron bars."); break;
			case CLUB: 
				pline("Don't bother with this weapon. The club just doesn't ever deal any meaningful damage."); break;
			case AKLYS: 
				pline("Stronger than a regular club, but still crappy."); break;
			case BASEBALL_BAT: 
				pline("This wooden club does respectable damage for its type."); break;
			case METAL_CLUB: 
				pline("A club made of hard metal. It does solid damage. It can be applied to bash iron bars."); break;
			case BONE_CLUB: 
				pline("A club made of bone that deals just as little damage as a normal club."); break;
			case SPIKED_CLUB: 
				pline("This club isn't that bad, for a club at least."); break;
			case HUGE_CLUB: 
				pline("Thankfully this club isn't overpowered at all despite dealing a ton and a half of damage per hit."); break;
			case LOG: 
				pline("This gigantic log of wood requires two hands to be used and requires the club skill, but wow does it do a lot of damage or what?"); break;
			case FLY_SWATTER: 
				pline("This paddle has good to-hit and small damage, but low large damage."); break;
			case BROOM: 
				pline("A two-handed paddle that doesn't deal a lot of damage."); break;
			case MOP: 
				pline("This two-handed paddle is fairly useless."); break;
			case SPECIAL_MOP: 
				pline("Better than an ordinary mop. It's two-handed and uses the paddle skill."); break;
			case BOAT_OAR: 
				pline("Looking for a reason to use the paddle skill? Then this two-handed weapon might be your first choice."); break;
			case MAGICAL_PAINTBRUSH: 
				pline("It sure sounds like something special, but it's just a two-handed paddle with low damage output."); break;
			case FUTON_SWATTER: 
				pline("A moderately usable paddle."); break;
			case CARDBOARD_FAN: 
				pline("Might as well attack with a trout instead. Just about every weapon in this game does more damage than this paddle!"); break;
			case OTAMA: 
#ifdef PHANTOM_CRASH_BUG
				pline("Paddle-class weapon that does next to no damage so you're probably better off fighting barehanded."); break;
#else
				pline("Good luck figuring out what this is! But I'll help you: I don't know what an 'otama' is supposed to be either, but it's a paddle-class weapon that does next to no damage so you're probably better off fighting barehanded."); break;
#endif
			case INSECT_SQUASHER: 
				pline("A paddle that does superb damage against small foes but next to no damage against large foes."); break;
			case SILVER_MACE: 
				pline("The main use of this mace is to bash undead, which take extra damage from it."); break;
			case MACE: 
				pline("A mace. It's quite a weak weapon, actually."); break;
			case ELVEN_MACE: 
				pline("A mace made of wood. It's slightly better than a standard mace."); break;
			case FLANGED_MACE: 
				pline("This mace does moderate damage but it's nothing to get excited about."); break;
			case REINFORCED_MACE: 
				pline("If you want a mace that does respectable damage, use this one."); break;
			case MORNING_STAR: 
				pline("The morning star can be used to whack enemies. Its damage output is mediocre."); break;
			case BRONZE_MORNING_STAR: 
				pline("This morning star does respectable damage."); break;
			case SPINED_BALL: 
				pline("A metal ball that counts as a morning star. It does good damage."); break;
			case JAGGED_STAR: 
				pline("An improved morning star that actually packs a punch."); break;
			case DEVIL_STAR: 
				pline("The strongest version of the morning star. A very strong one-handed melee weapon. It can be applied to bash iron bars."); break;
			case FLAIL: 
				pline("A basic flail. It doesn't do a lot of damage."); break;
			case KNOUT: 
				pline("A better flail that does mediocre damage."); break;
			case CHAIN_AND_SICKLE: 
				pline("Forget using this weapon. Even a regular flail is probably better."); break;
			case TWO_HANDED_FLAIL: 
				pline("This flail does quite good damage but at the expense of occupying both of your hands."); break;
			case OBSID: 
				pline("A strong flail that does good damage and has good to-hit. It can be applied to bash iron bars."); break;
			case WAR_HAMMER: 
				pline("A relatively weak hammer."); break;
			case SLEDGE_HAMMER: 
				pline("This two-handed hammer can be used to crush annoying critters."); break;
			case HEAVY_HAMMER: 
				pline("This hammer is a definite improvement of the standard war hammer that does good damage."); break;
			case MALLET: 
				pline("A huge hammer made of massive wood that is very useful for bashing down enemies. It can be applied to bash iron bars."); break;
			case WEDGED_LITTLE_GIRL_SANDAL: 
				pline("It's a wedge-heeled sandal. Whacking it over the head of an enemy might deal a bit of damage. It uses the hammer skill. It can be applied to bash iron bars."); break;
			case SOFT_GIRL_SNEAKER: 
				pline("Made of soft leather, this piece of footwear is not a powerful melee weapon. Good to-hit though. It uses the hammer skill."); break;
			case STURDY_PLATEAU_BOOT_FOR_GIRLS: 
				pline("A heavy plateau boot that can be swung at monsters to whack them for mediocre damage. It uses the hammer skill."); break;
			case HUGGING_BOOT: 
				pline("This thick winter boot is made of unyielding material, making it a useful weapon for bonking enemies' heads. It uses the hammer skill. It can be applied to bash iron bars."); break;
			case BLOCK_HEELED_COMBAT_BOOT: 
				pline("A very fleecy lady's boot with a massive block heel. Seems like you can bash enemies' skulls with it. It uses the hammer skill. It can be applied to bash iron bars."); break;
			case WOODEN_GETA: 
				pline("This piece of Japanese footwear is made of extremely hard wood. Striking the head of an enemy with it might leave them with a big dent. It uses the hammer skill."); break;
			case LACQUERED_DANCING_SHOE: 
				pline("This ladies' shoe looks expensive. Wielding it to bash enemies might have some uses. It uses the hammer skill."); break;
			case HIGH_HEELED_SANDAL: 
				pline("A sexy sandal; its heel looks sweet but can actually be used to smash things. It uses the hammer skill. It can be applied to bash iron bars."); break;
			case SEXY_LEATHER_PUMP: 
				pline("This beautiful lilac women's shoe looks very tender. However, the funneled heel can actually cause a lot of damage if it is struck on somebody's head. It uses the hammer skill. It can be applied to bash iron bars."); break;
			case SPIKED_BATTLE_BOOT: 
				pline("A heavy boot with spikes made of steel. Excellent for bashing monsters. It uses the hammer skill."); break;
			case QUARTERSTAFF: 
				pline("The basic quarterstaff is a two-handed weapon that does pitiful damage compared to other two-handers."); break;
			case SILVER_KHAKKHARA: 
				pline("Don't bother unless you're looking for a quarterstaff that does extra damage to undead and demons."); break;
			case RUNED_ROD: 
				pline("Slightly better than a quarterstaff and made of iron, but still a weak two-handed weapon."); break;
			case STAR_ROD: 
				pline("A platinum quarterstaff that requires two hands and doesn't do all that much damage."); break;
			case FIRE_HOOK: 
				pline("This iron quarterstaff does usable damage but it's not great either."); break;
			case PLATINUM_FIRE_HOOK: 
				pline("Quarterstaff that does usable damage."); break;
			case BATTLE_STAFF: 
				pline("A metal quarterstaff that does relatively good damage but requires both hands."); break;
			case IRON_BAR: 
				pline("This is a two-handed quarterstaff made of iron. It does moderately good damage."); break;
			case PARTISAN: 
				pline("A balanced two-handed polearm that can be applied to hit monsters standing two squares away. Using it at point blank range is only useful if you're riding."); break;
			case GLAIVE: 
				pline("A powerful two-handed polearm that can be applied to hit monsters standing two squares away. Using it at point blank range is only useful if you're riding."); break;
			case SPETUM: 
				pline("A finicky two-handed polearm that can be applied to hit monsters standing two squares away. Using it at point blank range is only useful if you're riding."); break;
			case RANSEUR: 
				pline("An unreliable two-handed polearm that can be applied to hit monsters standing two squares away. Using it at point blank range is only useful if you're riding."); break;
			case BARDICHE: 
				pline("A heavy two-handed polearm that can be applied to hit monsters standing two squares away. Using it at point blank range is only useful if you're riding."); break;
			case VOULGE: 
				pline("A dicey two-handed polearm that can be applied to hit monsters standing two squares away. Using it at point blank range is only useful if you're riding."); break;
			case PITCHFORK: 
				pline("A gardening two-handed polearm that can be applied to hit monsters standing two squares away. Using it at point blank range is only useful if you're riding."); break;
			case HALBERD: 
				pline("A massive two-handed polearm that can be applied to hit monsters standing two squares away. Using it at point blank range is only useful if you're riding."); break;
			case BLACK_HALBERD: 
				pline("An inverted two-handed polearm that can be applied to hit monsters standing two squares away. Using it at point blank range is only useful if you're riding."); break;
			case FAUCHARD: 
				pline("A mediocre two-handed polearm that can be applied to hit monsters standing two squares away. Using it at point blank range is only useful if you're riding."); break;
			case GUISARME: 
				pline("A challenging two-handed polearm that can be applied to hit monsters standing two squares away. Using it at point blank range is only useful if you're riding."); break;
			case BILL_GUISARME: 
				pline("A reinforced two-handed polearm that can be applied to hit monsters standing two squares away. Using it at point blank range is only useful if you're riding."); break;
			case LUCERN_HAMMER: 
				pline("A ferocious two-handed polearm that can be applied to hit monsters standing two squares away. Using it at point blank range is only useful if you're riding."); break;
			case BEC_DE_CORBIN: 
				pline("A strong two-handed polearm that can be applied to hit monsters standing two squares away. Using it at point blank range is only useful if you're riding."); break;
			case SICKLE: 
				pline("A weak one-handed polearm that can be applied to hit monsters standing two squares away. Using it at point blank range is only useful if you're riding."); break;
			case ELVEN_SICKLE: 
				pline("A useful one-handed polearm that can be applied to hit monsters standing two squares away. Using it at point blank range is only useful if you're riding."); break;
			case SCYTHE: 
				pline("An extra-damaging two-handed polearm that can be applied to hit monsters standing two squares away. Using it at point blank range is only useful if you're riding."); break;
			case ORCISH_SPEAR: 
				pline("The weakest of the spears. It can be thrown."); break;
			case SPEAR: 
				pline("A standard spear. It can be thrown."); break;
			case DROVEN_SPEAR: 
				pline("This spear deals great damage but will break if you throw it. It is two-handed."); break;
			case SILVER_SPEAR: 
				pline("A spear that does extra damage to undead. It can be thrown."); break;
			case ELVEN_SPEAR: 
				pline("A good spear. It can be thrown."); break;
			case DWARVISH_SPEAR: 
				pline("The strongest spear in the game. It can be thrown."); break;
			case BRONZE_SPEAR: 
				pline("A spear made of bronze. It can be thrown."); break;
			case FLINT_SPEAR: 
				pline("A spear made of mineral. It can be thrown."); break;
			case LONG_STAKE: 
				pline("A rather useless wooden spear. It can be thrown."); break;
			case BAMBOO_SPEAR: 
				pline("Weaker than the elven spear. It can be thrown."); break;
			case JAVELIN: 
				pline("A basic javelin that doesn't do much damage. It can be thrown."); break;
			case SPIRIT_THROWER: 
				pline("A javelin that does good damage. It can be thrown."); break;
			case TORPEDO: 
				pline("A very strong javelin that does lots of damage. It can be thrown. It can be applied to bash iron bars."); break;
			case TRIDENT: 
				pline("The trident does sucky damage but has bonuses versus eels."); break;
			case TWO_HANDED_TRIDENT: 
				pline("A relatively damaging trident that does bonus damage versus eels, however it also requires both hands to use."); break;
			case STYGIAN_PIKE: 
				pline("A trident from the depths of Hell. Good damage and bonus versus eels."); break;
			case MANCATCHER: 
				pline("A very strong trident that does extra damage to eels."); break;
			case RADIOACTIVE_DAGGER:
				pline("This dagger does extra damage to golems, but it's still only a dagger. It can be thrown. It can be applied to bash iron bars."); break;
			case SECRETION_DAGGER:
				pline("A very icky dagger that does moderate amounts of damage and has improved chances to hit. It can be thrown."); break;
			case BITUKNIFE:
				pline("A moderately useful knife with good to-hit. It can be thrown."); break;
			case MEASURER:
				pline("This is a knife made of metal which does low damage. It can be thrown."); break;
			case COLLUSION_KNIFE:
				pline("For a knife, this thing's damage isn't all that bad, but it causes darkness upon hitting something. It can be thrown."); break;
			case SPIRIT_AXE:
				pline("An axe made of erosionproof material. You can use it to chop down trees."); break;
			case SOFT_MATTOCK:
				pline("It's a digging tool covered with silk, which is still capable of smashing solid rock. You need to wield it with both hands, and it can also be used as a weapon. It counts as a pick-axe."); break;
			case INKA_BLADE:
				pline("A short sword made of inka leather."); break;
			case ETERNIUM_BLADE:
				pline("This short sword does good damage. It can be applied to bash iron bars."); break;
			case PAPER_SWORD:
				pline("Well, you probably expected that it's not very good. It uses the broadsword skill and does rather little damage."); break;
			case MEATSWORD:
				pline("An edible broadsword."); break;
			case ICKY_BLADE:
				pline("This long sword has increased chance to hit."); break;
			case GRANITE_IMPALER:
				pline("It's a rather strong longsword, comparable damage-wise with the katana."); break;
			case ORGANOBLADE:
				pline("An organic two-handed sword that does respectable damage."); break;
			case BIDENHANDER:
				pline("This two-handed sword is heavy and in all aspects inferior to a standard two-handed sword."); break;
			case INKUTLASS:
				pline("Slightly stronger than a regular scimitar, and it has increased to-hit."); break;
			case HOE_SABLE:
				pline("A moderately powerful scimitar."); break;
			case YATAGAN:
				pline("This arabic scimitar does rather good damage. It can be applied to bash iron bars."); break;
			case PLATINUM_SABER:
				pline("It's a saber with very good base damage. It can be applied to bash iron bars."); break;
			case WILD_BLADE:
				pline("A saber that does quite good damage, and if you get bored with it, you can also eat it."); break;
			case LEATHER_SABER:
				pline("It's not very strong, but what did you expect?"); break;
			case ARCANE_RAPIER:
				pline("Rapiers are rather weak sabers, and this one is no exception."); break;
			case NATURAL_STICK:
				pline("A club. The damage is about as low as you'd expect."); break;
			case POURED_CLUB:
				pline("This club is very heavy and yet doesn't deal a lot of damage."); break;
			case DIAMOND_SMASHER:
				pline("Use this club to fight small monsters, which take surprisingly large amounts of damage. But don't throw it or it will break! It can be applied to bash iron bars."); break;
			case VERMIN_SWATTER:
				pline("If you want to get rid of small monsters, use this paddle-class weapon, which has a big bonus to hit."); break;
			case PLASTIC_MACE:
				pline("Made of a different material and otherwise similar to the bog-standard mace."); break;
			case BRONZE_MACE:
				pline("It's a mace made of copper, and it doesn't do a lot of damage."); break;
			case MILL_PAIL:
				pline("A nature-friendly mace that does respectable damage."); break;
			case BACKHAND_MACE:
				pline("This mace does rather good damage."); break;
			case ASTERISK:
				pline("Practice your morning star skill with this weapon if you want, but don't expect it to be very good."); break;
			case RHYTHMIC_STAR:
				pline("It's a good morning star that is also resistant to erosion effects."); break;
			case YESTERDAY_STAR:
				pline("A very powerful morning star, which is also made of a material that cannot rust or otherwise degrade!"); break;
			case FLOGGER:
				pline("It uses the flail skill and is very useless because its damage output is so bad."); break;
			case RIDING_CROP:
				pline("A slightly stronger flail."); break;
			case NOVICE_HAMMER:
				pline("It's a total piece of crap weapon that weighs a ton."); break;
			case THUNDER_HAMMER:
				pline("This hammer requires both hands to use, but it does very high amounts of damage and even more if the target is a golem."); break;
			case BRIDGE_MUZZLE:
				pline("A one-handed hammer that does rather high damage. It can be applied to bash iron bars."); break;
			case INKA_BOOT:
				pline("Think of the sweet brown leather your sputa will flow down. :-) It uses the hammer skill and does lots of damage to small monsters but almost no damage to large ones. It can be applied to bash iron bars."); break;
			case SOFT_LADY_SHOE:
				pline("The Amy her first girlfriend was wearing them, and they are sooooooo soft and lovely. They use the hammer skill and deal more damage to small monsters than large ones, although the damage isn't exactly great. However, they give a very large bonus to your chance to hit! It can be applied to bash iron bars."); break;
			case STEEL_CAPPED_SANDAL:
				pline("Such a sweeeeeeet female sandal with stiletto heels made of metal! They deal large amounts of damage to everything and use the hammer skill, but if you use them repeatedly, they will degrade and eventually break."); break;
			case DOGSHIT_BOOT:
				pline("Eww... the previous owner fully stepped into a heap of shit. If for some strange reason you still insist on using it, it uses the hammer skill, deals low damage and has good to-hit."); break;
			case IMPACT_STAFF:
				pline("It's a two-handed staff that does respectable damage and has increased to-hit. It can be applied to bash iron bars."); break;
			case TROUTSTAFF:
				pline("A quarterstaff that requires both hands to use."); break;
			case FIRE_STICK:
				pline("This two-handed quarterstaff does moderate damage and lights up the area around you."); break;
			case OLDEST_STAFF:
				pline("A rather damaging quarterstaff that can only be used with both hands. If you wield it, your spells become easier to successfully cast."); break;
			case COLOSSUS_BLADE:
				pline("The grand daddy of two-handed swords, it deals huge amounts of damage. This comes at a price though - due to the fact that it's so bulky, it reduces your speed to half of its original value while you have it equipped!"); break;
			case TUBING_PLIERS:
				pline("A one-handed axe that does very good damage."); break;
			case CHEMISTRY_SPACE_AXE:
				pline("This two-handed axe not only does good damage, it also conveys acid resistance if you wield it!"); break;
			case OSBANE_KATANA:
				pline("A very good longsword that cannot be eroded."); break;
			case WALKING_STICK:
				pline("This quarterstaff is especially strong versus small monsters, and requires both hands to wield."); break;
			case RAIN_PIPE:
				pline("A heavy two-handed staff that doesn't do all that much damage."); break;
			case PENIS_POLE:
				pline("A phallus-shaped two-handed polearm that can be applied to hit monsters standing two squares away. Using it at point blank range is only useful if you're riding."); break;
			case GARDEN_FORK:
				pline("A forking two-handed polearm that can be applied to hit monsters standing two squares away. Using it at point blank range is only useful if you're riding."); break;
			case PIKE:
				pline("A long two-handed polearm that can be applied to hit monsters standing two squares away. Using it at point blank range is only useful if you're riding."); break;
			case PHYSICIAN_BAR:
				pline("A regenerative two-handed polearm that can be applied to hit monsters standing two squares away. Using it at point blank range is only useful if you're riding. Yes, wielding it speeds up your hit point regeneration rate."); break;
			case HELMET_BEARD:
				pline("A highly accurate two-handed polearm that can be applied to hit monsters standing two squares away. Using it at point blank range is only useful if you're riding."); break;
			case TRAFFIC_LIGHT:
				pline("A dimming two-handed polearm that can be applied to hit monsters standing two squares away. Using it at point blank range is only useful if you're riding. It does great damage but also dims you if you wield it."); break;
			case GIANT_SCYTHE:
				pline("A gigantic two-handed polearm that can be applied to hit monsters standing two squares away. Using it at point blank range is only useful if you're riding."); break;
			case THRESHER:
				pline("A humongous two-handed polearm that can be applied to hit monsters standing two squares away. Using it at point blank range is only useful if you're riding."); break;
			case INKA_SPEAR:
				pline("This spear is rather strong and good for hunting animals. It can be thrown."); break;
			case SILK_SPEAR:
				pline("It's a soft spear with a sharp tip. It can be thrown."); break;
			case BRITTLE_SPEAR:
				pline("A spear that weighs a lot and does low damage. Despite the name, it can be thrown and is not more likely to break than other types of spear."); break;
			case DRAGON_SPEAR:
				pline("A very powerful spear. It can be thrown."); break;
			case ASBESTOS_JAVELIN:
				pline("This javelin poisons the target. It can be thrown."); break;
			case HOMING_TORPEDO:
				pline("An incredibly strong javelin that also has great to-hit. It can be thrown. It can be applied to bash iron bars."); break;
			case COURSE_JAVELIN:
				pline("This javelin weighs a lot, but does respectable damage. It can be thrown."); break;
			case FOURDENT:
				pline("Tridents usually suck, but this one in particular sucks bad."); break;
			case PLOW:
				pline("It's a relatively useful trident with good to-hit."); break;
			case POKER_STICK:
				pline("Lances can be applied to hit monsters at a distance, and if you melee things while riding, you can joust monsters but that can cause it to break. This particular type also causes you to teleport uncontrollably, even if you have equipment that would allow you to control your teleportation."); break;
			case BRONZE_LANCE:
				pline("A slightly stronger version of the lance, this thing can be applied to hit monsters that don't stand right next to you. While riding, you can joust monsters with it by performing standard melee attacks but sometimes the lance breaks if you do so."); break;
			case COMPOST_BOW:
				pline("It can be used to fire arrows. If you fire forbidden arrows from it, you can fire more of them in a single turn."); break;
			case FORBIDDEN_ARROW:
				pline("An arrow type that can be fired with a bow. If you fire it with a compost bow, you can fire more of them in a single turn."); break;
			case WILDHILD_BOW:
				pline("This bow can fire several arrows in a single turn. Firing odor shots from it gives a multishot bonus"); break;
			case ODOR_SHOT:
				pline("These arrows deal extra damage to animals and humanoids because they can't stand the stench. If you fire them with a wildhild bow, you gain a multishot bonus."); break;
			case BRONZE_ARROW:
				pline("A highly damaging type of arrow that can be fired from a bow."); break;
			case PAPER_ARROW:
				pline("If you don't have real arrows, you can use these weak ones with your bow."); break;
			case METAL_SLING:
				pline("A launcher that can shoot rocks and gems."); break;
			case INKA_SLING:
				pline("This weapon can be used to fire rocks, stones and gems at monsters with high accuracy."); break;
			case PAPER_SHOTGUN:
				pline("It must be loaded with shotgun shells, which can be fired to hit monsters standing up to three tiles away."); break;
			case HUNTING_RIFLE:
				pline("A rifle that shoots single bullets over a range of 30 squares."); break;
			case PROCESS_CARD:
				pline("Load this laser gun with blaster bolts or laser beams, and then fire them at monsters standing up to 20 tiles away!"); break;
			case ZOOM_SHOT_CROSSBOW:
				pline("This crossbow has a range of up to 20 squares if you fire bolts with it, and equipping it also improves your sight."); break;
			case BALLISTA:
				pline("A slow-firing crossbow that can fire bolts at a distance of 15 squares."); break;
			case FLEECE_BOLT:
				pline("A very fleecy crossbow bolt."); break;
			case MINERAL_BOLT:
				pline("This crossbow bolt does slightly more damage than a regular one."); break;
			case PIN_BOLT:
				pline("A crossbow bolt that does low damage."); break;
			case INKA_STINGER:
				pline("These darts have a high chance to hit, and are meant to be thrown."); break;
			case FLIMSY_DART:
				pline("You can throw these darts at targets, but they have a tendency to fly in the wrong direction."); break;
			case SOFT_STAR:
				pline("Uses the shuriken skill. It is made of soft material and therefore does less damage than a real shuriken. Meant to be used for throwing."); break;
			case TAR_STAR:
				pline("These shuriken can be thrown at enemies."); break;
			case INKA_SHACKLE:
				pline("A very lovely whip that can cause incredibly soothing pain :) Joking aside, it does relatively good damage and has increased to-hit, but whips are generally a weak type of weapon."); break;
			case BULLETPROOF_CHAINWHIP:
				pline("Whips suck, but this one sucks a bit less than the others because it does relatively good damage. It will break if you throw it, so don't do that unless you want to get rid of it."); break;
			case SECRET_WHIP:
				pline("It's an incredibly sucky weapon that has good to-hit but does very low damage, however there's a hidden quality to it: while wielding it, you take less physical damage than otherwise."); break;
			case MARE_TRIDENT: 
				pline("This trident is made of silver, and does extra damage to eels."); break;
			case LANCE: 
#ifdef PHANTOM_CRASH_BUG
				pline("This thing can be applied to hit monsters that don't stand right next to you. While riding you can joust monsters in melee but that can cause it to break."); break;
#else
				pline("Formerly the only weapon that uses the lance skill, this thing can be applied to hit monsters that don't stand right next to you. While riding, you can joust monsters with it by performing standard melee attacks but sometimes the lance breaks if you do so."); break;
#endif
			case COURSE_LANCE: 
				pline("This weapon is a much stronger version of the lance. Apply it to hit monsters from far away, or melee monsters with it while riding to joust (but that can cause the lance to break)."); break;
			case DROVEN_LANCE: 
				pline("Apply it to hit monsters from far away, joust monsters with it while riding (and risk breaking it), but NEVER throw it or it will definitely break. This thing also requires both hands to wield."); break;
			case ELVEN_LANCE: 
				pline("Apply it to hit monsters from far away or joust monsters with it while riding (and risk breaking it)."); break;
			case FORCE_PIKE: 
				pline("One of Chris_ANG's creations, this weapon is in fact a lance. Apply it to hit monsters from far away. You can also joust monsters with it while riding, but that may cause it to break."); break;
			case PARASOL: 
				pline("A crappy lance that can be applied to hit monsters from far away or used to joust monsters while riding. The latter can cause it to break but I guess it wouldn't be a huge loss."); break;
			case UMBRELLA: 
				pline("You will look like a monumental fool with this 'weapon'. It can be applied to hit monsters from far away, or you can joust monsters while riding and risk breaking it."); break;
			case ORCISH_BOW: 
				pline("A two-handed bow that is meant to be used in conjunction with quivered arrows to fire at enemies."); break;
			case BOW: 
				pline("A two-handed bow that is meant to be used in conjunction with quivered arrows to fire at enemies."); break;
			case ELVEN_BOW: 
				pline("A two-handed bow that is meant to be used in conjunction with quivered arrows to fire at enemies."); break;
			case DARK_ELVEN_BOW: 
				pline("A two-handed bow that is meant to be used in conjunction with quivered arrows to fire at enemies. This bow has a small to-hit bonus."); break;
			case YUMI: 
				pline("A two-handed bow that is meant to be used in conjunction with quivered arrows to fire at enemies."); break;
			case HYDRA_BOW: 
				pline("This bow fires three arrows at once and is therefore very powerful. You need to wield it with both hands though."); break;
			case ORCISH_ARROW: 
				pline("The weakest type of arrow. These are meant to be put in a quiver and shot with a bow."); break;
			case ARROW: 
				pline("A standard arrow. These are meant to be put in a quiver and shot with a bow."); break;
			case GOLDEN_ARROW: 
				pline("Arrows made of pure gold. They deal significant amounts of damage when shot with a bow."); break;
			case ANCIENT_ARROW: 
				pline("This metal arrow can be fired with a bow to deal good damage to enemies."); break;
			case SILVER_ARROW: 
				pline("An arrow that does more damage to undead. These are meant to be put in a quiver and shot with a bow."); break;
			case ELVEN_ARROW: 
				pline("A good quality arrow that does high amounts of damage. These are meant to be put in a quiver and shot with a bow."); break;
			case DARK_ELVEN_ARROW: 
				pline("There are no arrows in the game that deal more damage than this one. These are meant to be put in a quiver and shot with a bow."); break;
			case YA: 
				pline("A high-quality arrow that has a moderate to-hit bonus. These are meant to be put in a quiver and shot with a bow."); break;
			case SLING: 
				pline("The sling is what you want to use if you want your thrown rocks to do more than a single point of damage. You can quiver most types of rocks and gems to shoot them with a sling."); break;
			case CATAPULT: 
				pline("A much better version of the sling that shoots more rocks at once and also grants significant bonuses to hit. You can quiver rocks and gems to fire."); break;
			case PISTOL: 
				pline("This firearm is capable of shooting bullets to deal damage to enemies."); break;
			case FLINTLOCK: 
				pline("You can theoretically use this firearm to shoot single bullets at monsters, but I'd advise you to use an actual pistol instead."); break;
			case BFG: 
#ifdef PHANTOM_CRASH_BUG
				pline("This bad boy will fire a heck of a lot of green beams (BFG ammo) per turn. If you can hit with it, you'll be capable of bringing even the strongest monsters down to their knees."); break;
#else
				pline("An atomic weapon of mass destruction, this bad boy will fire a heck of a lot of green beams (BFG ammo) per turn. If you can hit with it, you'll be capable of bringing even the strongest monsters down to their knees."); break;
#endif
			case HAND_BLASTER: 
				pline("A low-range energy gun with a fairly good rate of fire."); break;
			case ARM_BLASTER: 
				pline("PEW PEW PEW! This gun fires streams of laser ammo at your enemies."); break;
			case CUTTING_LASER: 
				pline("If you need an energy gun capable of hitting enemies standing up to 3 squares away, this thing might be useful. Don't try to shoot monsters standing any farther away though."); break;
			case RAYGUN: 
				pline("This energy gun shoots laser ammo at a good rate of fire over medium distances."); break;
			case SUBMACHINE_GUN: 
				pline("An automatic firearm that can fire three bullets in a single round of combat."); break;
			case HEAVY_MACHINE_GUN: 
				pline("The heavy machine gun requires two hands to use, but it can rip monsters a new one by firing 8 bullets per turn."); break;
			case RIFLE: 
				pline("A two-handed gun with a low rate of fire that shoots single bullets at enemies."); break;
			case ASSAULT_RIFLE: 
				pline("Your standard-issue heavy firearm that fires 5 bullets in one turn."); break;
			case SNIPER_RIFLE: 
				pline("Very slow, two-handed, but highly accurate. It fires single bullets."); break;
			case SHOTGUN: 
				pline("A short-range firearm that fires highly damaging (and accurate) shotgun shells."); break;
			case SAWED_OFF_SHOTGUN: 
				pline("It's a one-handed shotgun with bad to-hit, but its rate of fire is better than the regular shotgun."); break;
			case AUTO_SHOTGUN: 
				pline("This two-handed shotgun is capable of firing several shotgun shells in one round of combat."); break;
			case ROCKET_LAUNCHER: 
				pline("The 'big daddy' of firearms, this baby shoots explosive rockets for massive damage. Yeah, baby. It takes awfully long to reload though."); break;
			case GRENADE_LAUNCHER: 
				pline("If you want your grenades to pack a bigger punch, fire them with this weapon. The grenade launcher has a low rate of fire though."); break;
			case BULLET: 
				pline("A metal bullet that can be fired with pistols, submachine guns, rifles of all kinds, and heavy machine guns."); break;
			case ANTIMATTER_BULLET: 
				pline("This bullet does much more damage than regular ones. It must be fired from a pistol, SMG, rifle or heavy MG."); break;
			case SILVER_BULLET: 
				pline("A silver bullet that can be fired with pistols, submachine guns, rifles of all kinds, and heavy machine guns. Undead monsters take extra damage from it."); break;
			case BLASTER_BOLT: 
				pline("Laser-based ammo to be used by energy guns."); break;
			case HEAVY_BLASTER_BOLT: 
				pline("Strong laser-based ammo to be used by energy guns."); break;
			case LASER_BEAM: 
				pline("The ultimate energy gun ammo capable of dealing a heck of a lot of damage per shot."); break;
			case BFG_AMMO: 
				pline("Only the BFG can fire this type of ammo. The damage per ammo isn't that high, but wait until you see the # of ammo fired per turn!"); break;
			case SHOTGUN_SHELL: 
				pline("This shell does a lot of damage if fired with a shotgun."); break;
			case ROCKET: 
				pline("A highly explosive rocket. It requires a rocket launcher to be used effectively, but the explosion can hit several enemies at once."); break;
			case FRAG_GRENADE: 
				pline("You can just arm this bomb and throw it at a monster, but for better results, fire it with a grenade launcher."); break;
			case GAS_GRENADE: 
				pline("This bomb will explode in a cloud of noxious gas if you arm it. You can also fire it with a grenade launcher."); break;
			case STICK_OF_DYNAMITE: 
				pline("A stick with a fuse that can be armed. Once the fuse is burned out, it detonates to do explosive damage."); break;
			case CROSSBOW: 
				pline("The crossbow is a two-handed ranged weapon that fires bolts, doing respectable damage. Put a stack of bolts in your quiver to fire."); break;
			case DROVEN_CROSSBOW: 
				pline("A more accurate, one-handed version of the crossbow. Use it to fire bolts at your enemies."); break;
			case POWER_CROSSBOW: 
				pline("Two-handed crossbow that can fire more quickly than a regular one."); break;
			case DEMON_CROSSBOW: 
				pline("An automatic crossbow that can quiver several bolts in a single turn."); break;
			case PILE_BUNKER: 
				pline("This crossbow can fire a bit faster than a regular crossbow but at the expense of accuracy. It only requires one hand to be wielded."); break;
			case HELO_CROSSBOW: 
				pline("Want to snipe with a crossbow? Now you can! This thing has a huge range."); break;
			case DROVEN_BOW: 
				pline("A more accurate, one-handed version of the bow. Use it to fire arrows at your enemies."); break;
			case CROSSBOW_BOLT: 
				pline("This is the ammunition used by crossbows. Put it in your quiver while having a wielded crossbow and fire away. They do solid damage, too."); break;
			case DROVEN_BOLT: 
				pline("These glass bolts can be fired with a crossbow, doing more damage than regular bolts, but unfortunately they are very likely to break on impact."); break;
			case KOKKEN: 
				pline("Silver crossbow ammo. They deal very good damage."); break;
			case DROVEN_ARROW: 
				pline("These glass arrows can be fired with a bow, doing more damage than regular arrows, but unfortunately they are very likely to break on impact."); break;
			case DART: 
				pline("Darts are throwing weapons that are often found in stacks. They deal moderate amounts of damage. Don't bother whacking enemies with them though; they're meant to be thrown."); break;
			case DART_OF_DISINTEGRATION: 
				pline("They do more damage than regular darts... much more, in fact. Don't allow the monsters to throw those at you though, unless you want to be disintegrated!"); break;
			case SPIKE: 
				pline("A bone dart that can be thrown at enemies."); break;
			case SHURIKEN: 
				pline("These razor-sharp throwing stars used to be the only weapon to use the shuriken skill. Throw them at enemies to slice them into tiny bits."); break;
			case NEEDLE: 
				pline("Needles use the shuriken skill and can be thrown to kill enemies."); break;
			case CALTROP: 
				pline("If you want to train your shuriken skill, throw these at enemies. They deal much less damage than actual shuriken however."); break;
			case BOOMERANG: 
#ifdef PHANTOM_CRASH_BUG
				pline("You can throw this but it flies in a weird pattern, and meleeing monsters with it can cause it to break."); break;
#else
				pline("Good luck making this crappy weapon work! The boomerang can theoretically be thrown to hit several enemies in a single turn, but its weird flight pattern means it has situational uses at best and no uses at worst. Using the boomerang in melee will probably cause it to break."); break;
#endif
			case SILVER_CHAKRAM: 
				pline("A silver version of the boomerang. If you guessed that this weapon still sucks, you are right."); break;
			case BATARANG: 
				pline("This weapon cannot stun the little poison ivies, but it can kill them! :D Joking aside, it's a far stronger version of the boomerang but it still flies in that weird circle pattern and rarely hits what you aim for."); break;
			case BULLWHIP: 
#ifdef PHANTOM_CRASH_BUG
				pline("Pitiful damage, and thick-skinned monsters are immune to it. Applying it allows you to perform certain feats though."); break;
#else
				pline("*cue Vampire Killer theme* For some reason, Simon Belmont likes to use this weapon. It's got a totally pitiful damage output, and thick-skinned enemies are even outright immune to it. However, you can apply a bullwhip to perform feats like disarming an enemy."); break;
#endif
			case STEEL_WHIP: 
				pline("A metal version of the bullwhip. While far stronger than a regular bullwhip, this weapon is still a whip and you know that whips suck. Steer clear. It can be applied to bash iron bars."); break;

			case CHAINWHIP: 
				pline("Forget it! This iron whip does not make a good weapon."); break;
			case MITHRIL_WHIP: 
				pline("A whip that doesn't erode, but it doesn't deal any significant damage either."); break;
			case FLAME_WHIP: 
				pline("Utterly useless weapon. I guess if you just don't have anything else..."); break;
			case ROSE_WHIP: 
				pline("The only use for this wooden weapon is if you want to train your whip skill for some reason. It deals next to no damage."); break;

			default: pline("Missing item description (this is a bug). Please tell Amy about the item in question so she can add a description."); break;
			case RUBBER_HOSE: 
				pline("The law inforcement officers like to use this whip-type weapon, but you're probably better off using a real weapon if you don't want to die horribly."); break;

			}

		}
		break;

		case ARMOR_CLASS:
#ifdef PHANTOM_CRASH_BUG
		pline("%s - Color: %s. Material: %s. Appearance: %s.",xname(obj), obj->dknown ? c_obj_colors[objects[obj->otyp].oc_color] : "unknown", obj->dknown ? materialnm[objects[obj->otyp].oc_material] : "unknown", obj->dknown ? dn : "unknown");
#else
		pline("%s - This is a piece of armor. Color: %s. Material: %s. Appearance: %s. Slot: %s. It can be worn for protection (armor class, magic cancellation etc.).",xname(obj), obj->dknown ? c_obj_colors[objects[obj->otyp].oc_color] : "unknown", obj->dknown ? materialnm[objects[obj->otyp].oc_material] : "unknown", obj->dknown ? dn : "unknown", is_shield(obj) ? "shield" : is_helmet(obj) ? "helmet" : is_boots(obj) ? "boots" : is_gloves(obj) ? "gloves" : is_cloak(obj) ? "cloak" : is_shirt(obj) ? "shirt" : is_suit(obj) ? "suit" : "weird slot (this may be a bug)");
#endif
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "irregular boots") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "neregulyarnyye sapogi") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "tartibsizlik chizilmasin"))))
			pline("These boots have weird-shaped high heels, which look a bit like a wedge heel with part of it cut out, which can occasionally cause you to fumble. But while you're wearing them, the turn counter advances at half the normal speed.");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "internet helmet") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "vsemirnaya pautina shlem") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "keng dunyo veb-zarbdan"))))
			pline("A special helmet that provides internet access. Watching the webcams can occasionally show you the movement of monsters on the current dungeon level.");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "wedge boots") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "klin sapogi") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "xanjar chizilmasin"))))
			pline("These boots are super-comfortable thanks to their beautifully massive wedge heels!");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "winter stilettos") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "zima stilety") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "qish sandal chizilmasin"))))
			pline("The epitome of beauty and elegance, these very high stiletto boots even allow you to walk on ice without slipping.");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "clunky heels") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "neuklyuzhiye kabluki") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "qisqa ko'chirish to'piqlarni"))))
			pline("You notice that these boots are characterized by extra thick, clunky block heels.");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "ankle boots") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "botil'ony") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "bilagi zo'r chizilmasin"))))
			pline("Ankle boots are a type of high-heeled footwear with cone heels.");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "block-heeled boots") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "blok kablukakh sapogi") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "blok-o'tish chizilmasin"))))
			pline("You love the fleecy block heels of this pair of boots, because they are very kind and gentle.");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "vampiric cloak") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "vampir plashch") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "sindirishi plash"))))
			pline("Wearing this cloak will give you the special effect of resisting drain life effects 1 out of 10 times.");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "aluminium helmet") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "shlem iz alyuminiya") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "alyuminiy dubulg'a"))))
			pline("Prevents telepathy while you wear it.");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "shrouded cloak") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "okutana plashch") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "kafan plash"))))
			pline("Occasionally, this cloak creates a displaced image to fool monsters if you wear it.");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "anti-government helmet") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "antipravitel'stvennaya shlem") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "aksil-hukumat dubulg'a"))))
			pline("Do you hate the government and especially the kops? Now you can put on this helmet, and they will spawn less often and in smaller groups!");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "filtered helmet") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "fil'truyut shlem") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "filtrlangan zarbdan"))))
			pline("A helmet with a gas filter that protects you a bit from inhaling noxious gases.");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "graffiti gloves") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "graffiti perchatki") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "qo'lqop purkash"))))
			pline("Beware, these gloves are made of liquid graffiti. They are very slippery. Expect to drop your weapon at inopportune moments.");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "vampiric gloves") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "vampiry perchatki") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "sindirishi qo'lqop"))))
			pline("You should not wear this pair of gloves if you don't really have to, because they will continuously drain your experience.");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "RNG helmet") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "shlem gsch") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "RNG dubulg'a"))))
			pline("Very rarely, the RNG will make random bad stuff happen if you put on this helmet.");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "mysterious cloak") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "tainstvennyy plashch") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "sirli plash"))))
			pline("Wearing this cloak grants you Angband-style pseudo identification for the objects in your main inventory!");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "ghostly cloak") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "prizrachnyy plashch") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "diniy plash"))))
			pline("The spirits of the deceased may be summoned to haunt you while you wear this.");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "comfortable gloves") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "udobnyye perchatki") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "qulay qo'lqop"))))
			pline("A pair of gloves that is really comfortable and causes your prayer timeout to be faster, so you can pray more often.");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "complete helmet") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "polnaya shlem") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "to'liq dubulg'a"))))
			pline("Your entire head will be covered by this helm. This protects you from beheading attacks.");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "polnish gloves") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "pol'skiye perchatki") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "polsha qo'lqop"))))
			pline("Yes I know, it's called 'polish', but that word is ambiguous... anyway, the #borrow command works better if you wear this pair of gloves.");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "velcro boots") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "lipuchki sapogi") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "cirt chizilmasin"))))
			pline("Warning: This pair of boots will constrict itself around your feet with its velcro lashes, and they will hurt you from time to time while worn. However, if you kick a monster with them, the lashes will scratch up and down the monster's legs, which is a lot of fun.");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "straitjacket cloak") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "smiritel'naya rubashka plashch") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "tor kamzul plash"))))
			pline("Don't expect this cloak to come off easily once you put it on.");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "battle boots") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "bitvy sapogi") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "urush chizilmasin"))))
			pline("Battle boots power up your kicks to do more damage.");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "platform boots") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "plato sapogi") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "platosi chizilmasin"))))
			pline("If you kick someone with this pair of beautiful platform boots, you will sometimes stomp their toes, causing the target to be stunned!");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "plateau boots") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "sapogi na platforme") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "platformalar chizilmasin"))))
			pline("You like plateau boots? Of course you do! You can kick monsters with them, which will occasionally cause you to stomp their toes flat (stunning the monster).");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "combat boots") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "boyevyye sapogi") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "jangovar chizilmasin"))))
			pline("This is a pair of boots that improves your kicking prowess: it greatly reduces the chance that your kick is 'clumsy'.");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "jungle boots") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "dzhunglyakh sapogi") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "o'rmon chizilmasin"))))
			pline("Are you annoyed by the fact that kicking a tree often hurts your legs? Well, with this special footwear that cannot happen any longer!");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "clumsy gloves") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "neuklyuzhiye perchatki") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "qo'pol qo'lqop"))))
			pline("Clumsy gloves are just what they say on the tin. Your ranged weapons will occasionally misfire while wearing these, and have a significant to-hit penalty.");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "fin boots") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "plavnik sapogi") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "kanatcik chizilmasin"))))
			pline("You cannot drown while you have these fins around your feet.");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "profiled boots") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "profilirovannyye sapogi") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "profilli chizilmasin"))))
			pline("Profiled soles are lovely, and stepping into dog shit with them is fun because it's so much work to clean them again! :D Well, actually you will speed up if you step into a heap of shit while wearing them.");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "hot boots") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "goryachiye botinki") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "issiq chizilmasin"))))
			pline("This pair of boots can withstand temperatures of up to 9000 degrees. Wearing them allows you to swim in lava unharmed.");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "politician cloak") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "politik plashch") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "siyosatchi plash"))))
			pline("Nobody likes politicians. So if you dress like one, nobody will like you either.");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "angelic cloak") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "angel'skoye plashch") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "farishtalarning plash"))))
			pline("This lovely cloak makes you look like an angel! Which has the effect that most other angels will not attack you.");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "demonic cloak") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "demonicheskaya plashch") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "jinlarning plash"))))
			pline("Demons are more likely to be peaceful if you don this cloak, because it makes you look like one of them.");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "void cloak") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "nedeystvitel'nym plashch") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "haqiqiy emas plash"))))
			pline("The void cloak generates a weak aura of anti-magic around you, making it harder for monsters to cast spells.");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "grey-shaded gloves") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "sero-zatenennykh perchatki") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "kulrang-soyali qo'lqop"))))
			pline("Are you into BDSM? If you wear these gloves, your chances of sexual pleasure will go up! You can get your nuts kicked, be whacked over the head with sexy high heels, have your legs scratched up and down until it bleeds etc.");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "slippery gloves") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "skol'zkiye perchatki") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "sirg'anchiq qo'lqop"))))
			pline("Slippery gloves are similar to slippery cloaks, often allowing you to slip free of monsters' holding attacks.");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "petrified cloak") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "okamenela plashch") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "qotib plash"))))
			pline("If you hear the cockatrice's hissing, you will often turn to stone. But this lithic cloak allows you to resist most of the time.");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "mirrored gloves") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "zerkal'nyye perchatki") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "akslantirish qo'lqop"))))
			pline("A pair of gloves that's like a mirror. Monsters that try to use their gaze on you will sometimes gaze at the gloves, keeping you safe from the effects.");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "visored helmet") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "zabralom shlema") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "soyabon zarbdan"))))
			pline("The visor keeps you safe from blindness attacks.");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "orange visored helmet") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "oranzhevyy shlem zabralom") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "apelsin soyabon zarbdan"))))
			pline("Your eyes are protected from blinding attacks while wearing this helmet.");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "rainbow boots") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "raduga sapogi") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "kamalak chizilmasin"))))
			pline("A pair of boots that is so colorful, it alerts every monster to your presence.");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "snow boots") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "zimniye sapogi") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "qor chizilmasin"))))
			pline("Walking on ice will not cause you to slip with these boots.");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "winter boots") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "sapogi zimniye") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "qish chizilmasin"))))
			pline("These boots can walk on any type of ice, no matter how slippery.");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "water-pipe helmet") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "shlem kal'yannym") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "suv-quvur dubulg'a"))))
			pline("Wanna do the liquid diet conduct? Now you can - while wearing this helmet, every time you quaff anything you'll gain some nutrition!");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "godless cloak") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "bezbozhnaya plashch") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "xudosiz plash"))))
			pline("Wearing this cloak means that praying can anger your god, even if it's actually safe to pray.");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "weeb cloak") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "zese plashch") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "yaponiya ucube rido"))))
			pline("Only a Mahou Shoujo will benefit from the special effect of this cloak, everyone else isn't Japanese enough. What is the effect? Reduced spellcasting costs! Now your Mahou Shoujo can cast even more spells in the same time!");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "riding gloves") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "yezda perchatki") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "kopgina qo'lqop"))))
			pline("Riding your automobile (steed) becomes much easier with this pair of gloves.");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "riding boots") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "sapogi dlya verkhovoy yezdy") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "kopgina chizilmasin"))))
			pline("You wanna ride a motorcycle? With these boots, you almost cannot fail! They also help when you're trying to ride something else. :-)");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "explosive boots") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "vzryvnyye sapogi") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "portlovchi chizilmasin"))))
			pline("DANGER: This pair of boots is filled with TNT. Wearing them for too long will cause them to detonate.");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "radio helmet") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "translyatsii shlem") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "uzatuvchi zarbdan"))))
			pline("The Dungeons of Doom radio broadcast will sometimes inform you of the location of traps, but the only way to listen to it is by putting on this helm.");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "persian boots") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "persidskiye sapogi") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "fors chizilmasin"))))
			pline("If you ask this very beautiful pair of leather boots whether you may wear them, they will tell you that yes, you may, but you'll have to allow their long zippers to slit your legs from time to time.");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "deadly cloak") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "smertel'noy plashch") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "o'ldiradigan plash"))))
			pline("The deadly cloak will try to kill you while you wear it, but thankfully all it ever does is occasionally causing a small amount of damage.");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "jarring cloak") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "sotryaseniye plashch") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "g'azablantiradigan plash"))))
			pline("Something isn't right with this cloak... it emits jarring noises from time to time...");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "hugging boots") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "obnimat'sya sapogi") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "havola etdi chizilmasin"))))
			pline("They're actually called 'hiking boots', and they love to step into dog shit with their profiled soles. Have fun cleaning them again!");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "brand-new gloves") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "sovershenno novyye perchatki") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "yangi qo'lqop"))))
			pline("This pair of gloves is highly resistant to erosion.");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "scuba helmet") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "podvodnoye shlem") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "tueplue zarbdan"))))
			pline("The ultimate diving equipment. Wearing this helmet will protect your entire inventory from getting wet!");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "boxing gloves") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "boks para perchatok") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "boks qo'lqoplari"))))
			pline("Practising kung-fu or marital arts? Your chances of successfully fighting off your spouse will go up while wearing these!");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "fencing gloves") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "ograzhdeniya perchatki") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "qilichbozlik qo'lqop"))))
			pline("If your main weapon type is swords you will want to wear these gloves for bonus damage. This is especially true if you actually are a Fencer.");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "fleecy boots") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "flis sapogi") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "tozalamoq chizilmasin"))))
			pline("Your potions will be kept safe from cold because these leather boots are so wonderfully fleecy. Yes, this makes sense! The fleeciness keeps the cold weather out!");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "chess boots") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "shakhmatnyye sapogi") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "shaxmat chizilmasin"))))
			pline("You can play chess with the monsters by putting on this pair of boots - well, sort of. It will occasionally prevent monsters from moving diagonally.");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "fingerless gloves") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "mitenki") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "kam qo'lqop barmoq"))))
			pline("Unlike other gloves, these will not cover your fingers.");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "energizer cloak") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "antidepressant plashch") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "energiya plash"))))
			pline("A cloak full of energy, which allows you to draw on the mystical power of any creature you kill.");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "mantle of coat") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "mantiya pal'to") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "ko'ylagi mantiya"))))
			pline("This cloak type grants extra AC, but also causes nastiness sometimes.");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "chilling cloak") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "pugayushchim plashch") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "sovutgichli plash"))))
			pline("Wearing this cloak causes you to be surrounded by frost, and occasionally you'll be frozen.");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "bug-tracking helmet") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "oshibka otslezhivaniya shlem") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "hasharotlar-kuzatish dubulg'a"))))
			pline("Monsters will be able to track you if you wear this type of helm, and it especially attracts bugs.");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "fatal gloves") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "fatal'nyye perchatki") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "halokatli qo'lqop"))))
			pline("If you wear this pair of gloves, your magnetic items can occasionally experience a case of fatal attraction.");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "beautiful heels") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "krasivyye kabluki") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "chiroyli ko'chirish to'piqlarni"))))
			pline("Such a lovely pair of high heels! <3 They will greatly increase your charisma when worn because the cone heels are incredibly cuuuuuute, so you should definitely allow them to gently enclose your sweet feet!");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "electrostatic cloak") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "elektrostaticheskoye plashch") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "elektrofizikaviy kompyuteringizda ornatilgan plash"))))
			pline("It crackles with electricity, and will damage monsters that attack you in melee. However, sometimes you will be confused or numbed by the voltage.");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "weeping helmet") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "placha shlem") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "yig'lab dubulg'a"))))
			pline("If you wear this helmet, you will suffer from levelteleportitis. And if you cannot levelport for some reason, it will drain your experience levels instead, ignoring any drain resistance that you might have.");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "runic gloves") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "runa rukovitsakh") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "runi qo'lqop"))))
			pline("Reading a spellbook will increase your spell memory by more than the standard amount if you do it while wearing these gloves.");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "roman sandals") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "rimskiye sandalii") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "rim fuqarosi kavushlari"))))
			pline("Sandals are impractical for running. This is especially true for 'gladiator' sandals like these, and therefore you will move a bit slower in them.");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "homicidal cloak") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "smertonosnyy plashch") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "halokatli plash"))))
			pline("Sometimes, while you wear this cloak, new traps are generated on the current dungeon level.");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "narrow helmet") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "uzkiy shlem") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "tor dubulg'a"))))
			pline("A helmet that constricts your head in a bad way because it's so narrow, making you more susceptible to psychic blasts.");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "spanish gloves") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "ispanskiy perchatki") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "ispaniya qo'lqop"))))
			pline("Thumb screws in glove form! They will autocurse if you wear them.");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "castlevania boots") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "zamok vaney sapogi") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "qal'a vania chizilmasin"))))
			pline("As if the dungeon wasn't dark enough, these boots will occasionally darken an area while you wear them.");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "greek cloak") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "grecheskiy plashch") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "yunon plash"))))
			pline("This cloak turns everyone into a greek centurion if you wear them. Or, in other words, both you and all monsters will move faster.");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "celtic helmet") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "kel'tskaya shlem") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "seltik dubulg'a"))))
			pline("The Ancient Celts are masters of constructs, and therefore, if you wear them, all newly generated golems will have much more hit points. Useful if you're planning to get some golem pets.");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "english gloves") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "angliyskiye perchatki") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "ingliz tili qo'lqop"))))
			pline("Sniper alert! Your beam spells and wands will have bigger range while wearing this pair of gloves, but because the Amy is stupid, she accidentally gave the bonus to monster-caused beams too... :-P");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "korean sandals") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "koreyskiye sandalii") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "janubiy koreyaning kavushlari"))))
			pline("If you want to turn into a sweet asian amazon, wear these multicolored sweeties. They make you more resistant to fire and also confer weak displacement, but monsters will hit you more often in melee.");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "spider boots") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "pauk sapogi") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "o'rgimchak chizilmasin"))))
			pline("Spider webs will not hold you in place as long as you wear this pair of boots.");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "camo robe") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "kamuflyazhnaya roba") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "kamuflaj to'n"))))
			pline("A robe in urban camo colors that makes you more difficult to spot for your enemies.");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "octarine robe") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "vos'moy tsvet khalata") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "sakkizinchi rang to'n"))))
			pline("This robe is imbued with special magic. Sometimes it will send beams back at whoever fired them.");

		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "forgetful cloak") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "zabyvchiv plashch") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "unutuvchan plash"))))
			pline("You will forget your spells more quickly while wearing this cloak.");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "changing cloak") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "izmeneniye plashch") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "o'zgaruvchan plash"))))
			pline("It causes a weaker form of polymorphitis; wear it at your own risk!");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "shell cloak") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "plashch obolochki") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "qobiq plash"))))
			pline("This cloak provides an antimagic shell that can cause you to sometimes fail when trying to cast a spell, but the same applies to spellcasting monsters.");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "chinese cloak") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "kitayskiy plashch") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "xitoy plash"))))
			pline("It is labelled 'Arabella's Bank of Crossroads Employee of the Month'.");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "polyform cloak") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "sopolimer forma plashch") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "belgigacha bo'lgan poli shakli plash"))))
			pline("Very rarely, you may get a controlled polymorph while wearing this cloak. But beware, occasionally it will be an uncontrolled polymorph instead!");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "absorbing cloak") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "pogloshchayushchiy plashch") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "yutucu plash"))))
			pline("This cloak absorbs light, and will usually cause monsters to be short-sighted.");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "deep cloak") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "glubokiy plashch") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "chuqur plash"))))
			pline("While wearing this cloak, you will occasionally be displaced so monsters have a harder time hitting you, and it can also sometimes prevent level drain effects.");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "pink cloak") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "bakh-rozovyy plashch") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "portlash-pushti plash"))))
			pline("Yes, it doesn't really make sense, but while wearing this bright pink color, monsters will be reluctant to close in on you.");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "birthcloth") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "rozhdeniye tkan'") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "tug'ilgan mato"))))
			pline("Do you wanna have children? Then wear this as you're about to have a sexual encounter!");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "grass cloak") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "plashch trava") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "o't plash"))))
			pline("It's a very green cloak. So green in fact that green-colored monsters may spontaneously become your pets.");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "contaminated coat") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "zagryaznennoye pal'to") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "ifloslangan palto"))))
			pline("This coat is contaminated with botulism spores! If you wear it, you will become deathly ill every once in a while!");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "withered cloak") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "uvyadshiye plashch") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "shol plash"))))
			pline("It's a cloak that shows signs of withering... but strangely enough, that seems to make it much more resilient to damage. After all, if it's already damaged beyond repair, it cannot be damaged any further!");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "ignorant cloak") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "nevezhestvennyye plashch") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "johil plash"))))
			pline("You will become an ignorant fool if you put on this cloak. It will autocurse and occasionally cause your item identification attempts to fail.");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "avenger cloak") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "mstitel' plashch") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "qasoskor plash"))))
			pline("Did you watch the 'Avengers' movies? With this cloak, you can become mighty like Thor and do extra damage with hammers, but such power also comes at a cost: you will aggravate monsters and go hungry much more quickly.");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "gravity cloak") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "gravitatsionnyy plashch") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "agar tortishish kuchi plash"))))
			pline("Gravity will randomly warp around you while wearing this cloak, causing you to be stunned and disoriented.");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "wishful cloak") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "zhelayemoye za deystvitel'noye plashch") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "istalgan plash"))))
			pline("Your wishes will work even with negative luck as long as you're wearing this cloak.");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "poke mongo cloak") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "sovat' mongo plashch") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "soktudun mongo plash"))))
			pline("It turns you into a smombie, but also causes all pokemon to spawn peaceful most of the time. Sometimes they'll even be generated tame. Beware: if any of your pets dies while you wear it, your deity will get angry!");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "levuntation cloak") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "levitatsii plashch") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "havo rido kiygan suzadi"))))
			pline("There is no actual 'levuntation' effect, but wearing this cloak will make all of your potions unsafe to drink.");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "quicktravel cloak") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "bystryy plashch puteshestviya") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "tez safar plash"))))
			pline("A highly dangerous cloak that will prevent multi-turn actions from being interrupted.");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "geek cloak") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "komp'yutershchik plashch") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "qani plash"))))
			pline("If you're a geek or graduate, you will gain the power of Eru Illuvator (sic) by putting it on!");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "nurse cloak") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "medsestra plashch") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "hamshira plash"))))
			pline("Effects that heal you will be improved while you wear it.");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "slexual cloak") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "polovoy plashch") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "jinsiy plash"))))
			pline("Wearing it increases the chance of receiving sexual pleasure from nymphs. :-)");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "angband cloak") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "plashch sredizem'ye krepost'") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "o'rta yer qal'a plash"))))
			pline("While you wear this cloak, the game will behave like Angband. Just see for yourself.");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "fleecy-colored cloak") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "vorsistyye tsvetnoy plashch") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "serjun rangli plash"))))
			pline("The colors are very fleecy! <3 Everyone will like you much more, and therefore your charisma will go way up while you wear it!");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "anorexia cloak") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "yedyat plashch rasstroystvo") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "eb buzilishi plash"))))
			pline("You will suffer from a terrible, life-threatening condition if you wear it. DANGER: anorexia exists in real life too and is just as deadly there. The Amy will not be responsible if you stupidly kill yourself by refusing to eat!");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "flash cloak") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "flesh-plashch") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "bir flesh plash"))))
			pline("This cloak will occasionally hit you with a flash of light that causes blindness.");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "dnethack cloak") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "podzemeliy i vnezemnyye plashch vzlomat'") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "hamzindon va dunyo bo'lmagan doirasi so'yish plash"))))
			pline("Just in case you felt like SLASH'EM Extended was too easy, this cloak will recalculate a bunch of things to make it harder for you.");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "boxing gown") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "plat'ye boks") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "boks libosi"))))
			pline("Wearing this cloak will make you a better martial artist. However, if you don't have the martial arts skill, you won't receive a bonus.");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "team splat cloak") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "vosklitsatel'nyy znak plashch komanda") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "jamoasi xavfsizlik plash"))))
			pline("Gogo junethack team splat! TROPHY GET! :D");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "eldritch cloak") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "sverkh'yestestvennyy plashch") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "aql bovar qilmaydigan plash"))))
			pline("While wearing this cloak, mundane monsters can sometimes turn into dangerous eldritch abominations.");

		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "musical helmet") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "muzykal'nyy shlem") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "musiqiy dubulg'a"))))
			pline("While wearing this helmet, you will be very good at playing music. This means that playing an instrument will train your device skill much faster than usual.");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "secret helmet") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "sekret shlem") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "yashirin dubulg'a"))))
			pline("Wearing this helmet allows you to hide underneath items by moving onto them.");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "inkcoat helmet") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "shlem pal'to chernil") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "siyoh palto dubulg'a"))))
			pline("This helmet is covered with ink, and makes you difficult to spot so monsters may move randomly sometimes.");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "knowledgeable helmet") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "znayushchikh shlem") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "bilimdon dubulg'a"))))
			pline("A helmet that grants you knowledge about the arcane things and improves your spellcasting success chance.");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "formula one helmet") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "formula odin shlem") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "formula bir zarbdan"))))
			pline("Racecar drivers usually wear this helm. It will make you a little faster, but all monsters will also be a bit faster if you wear it!");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "difficult cloak") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "trudnyy plashch") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "qiyin plash"))))
			pline("Wearing this cloak will make the game more difficult by spawning much higher-level monsters.");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "gentle cloak") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "nezhnyy plashch") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "muloyim plash"))))
			pline("It's a very lovely cloak that increases your charisma by one point while worn.");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "irradiation cloak") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "oblucheniye plashch") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "nurlanish plash"))))
			pline("This cloak is radioactive and will slowly damage your maximum hit points and mana while worn.");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "soft cloak") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "myagkiy plashch") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "yumshoq plash"))))
			pline("Wearing this cloak will very slightly decrease the amount of physical damage you take.");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "excrement cloak") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "ekskrementy plashch") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "chiqindi plash"))))
			pline("Don't wear this cloak unless you want to stink like a heap of shit. It causes you to aggravate monsters, and you will be unable to have stealth while you have it on!");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "hungry cloak") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "golodnymi plashch") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "chanqoq plash"))))
			pline("This cloak will occasionally make you much more hungry while you wear it. Careful, don't starve to death!");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "science cloak") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "nauka plashch") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "ilm-fan plash"))))
			pline("Scientific modifications allow the wearer of this cloak to cast spells with an increased chance of success.");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "guild cloak") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "gil'dii plashch") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "birlik plash"))))
			pline("Wearing this cloak puts you in the Mages Guild, so to speak - you will not forget your spells over time while you have it on.");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "erotic boots") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "eroticheskiye sapogi") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "erotik chizilmasin"))))
			pline("You get all wet and horny when looking at this pair of high heels. In particular, you almost have a spontaneous orgasm when you look at the fleecy block heels.");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "sputa boots") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "mokrota sapogi") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "sputa chizilmasin"))))
			pline("Think of the sweet block heels your sputa will flow down.");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "radiant heels") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "izluchayushchiye kabluki") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "yorqin ko'chirish to'piqlarni"))))
			pline("These high heels are very colorful! Gotta love the beautiful wedge heels <3");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "turbo boots") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "turbo sapogi") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "qidiruvi va turbo chizilmasin"))))
			pline("It's a pair of boots with a built-in turbo that makes you move a bit faster, but certain actions that would usually interrupt you will no longer do so.");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "sexy heels") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "seksual'nyye kabluki") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "belgila sexy ko'chirish to'piqlarni"))))
			pline("These high heels are very sexy! In fact, just looking at the cone heels makes you wet!");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "stroking boots") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "poglazhivaya sapogi") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "etiklar silay"))))
			pline("You absolutely want to stroke these wonderful high heels. The block heels certainly feel soooooo incredibly soft!");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "velvet gloves") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "barkhatnyye perchatki") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "baxmal qo'lqop"))))
			pline("Wearing this pair of gloves will halve your spellcasting penalty for wearing armor, so if you want to be able to cast in full plate mail, this is the ticket.");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "racer gloves") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "gonshchik perchatki") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "poygachi qo'lqop"))))
			pline("You will be slightly faster than usual while wearing this pair of gloves, but it prevents you from using the quicktravel command so you have to walk everywhere on foot.");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "shitty gloves") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "der'movyye perchatki") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "boktan qo'lqop"))))
			pline("This pair of gloves is not very good; if you try to pick up a petrifying object with them, bad things will happen. Not necessarily petrification, mind you.");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "sensor gloves") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "sensornyye perchatki") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "tayinlangan qurilmani qo'lqop"))))
			pline("Use this pair of gloves to be warned of traps that you step on - the sensor will sound an alarm whenever you walk over one.");

		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "blue sneakers") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "siniye krossovki") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "ko'k shippak"))))
			pline("These sneakers look cute, and their rough soles can sometimes prevent you from fumbling.");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "colorfade cloak") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "plashch tsveta") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "ranglash plash"))))
			pline("Wearing this cloak causes monsters to have no color.");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "femmy boots") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "zhenskiye sapogi") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "nazokat etigi"))))
			pline("A high-heeled pair of boots that used to be worn by Femmy. She really likes cone heels, it seems. If you let them enclose your feet, the dungeon will slowly become more feminine.");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "dream helmet") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "shlem mechty") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "dubulg'a orzu"))))
			pline("Pleasant dreams will come to you while you have this helmet on - it allows you to regenerate hit points and mana more quickly while sleeping.");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "red sneakers") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "krasnyye krossovki") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "qizil shippak"))))
			pline("This is a lovely pair of bang-red sneakers. If you kill a monster while wearing them, you will heal up a bit.");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "yellow sneakers") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "zheltyye krossovki") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "sariq shippak"))))
			pline("An incredibly cute pair of sneakers made of wonderfully soft velvet. But they are actually powerful and increase the damage you do while kicking, plus they're immune to heaps of shit. However, if you ever allow them to get wet, you'll be paralyzed.");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "pink sneakers") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "rozovyye krossovki") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "pushti shippak"))))
			pline("This pair of sneakers looks very female and lovely! However, they also emit a beguiling stench that can affect both you and monsters.");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "sharp-edged sandals") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "ostrokonechnyye sandalii") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "o'tkir xanjarday kavushlari"))))
			pline("The heels of this pair of female stiletto sandals are very sharp-edged.");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "ski heels") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "lyzhnyye kabluki") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "chang'i poshnalar"))))
			pline("A special pair of high-heeled footwear that can walk over snow, which is now actually in the game and causes them to speed up. But they walk on ice just as well :-). However, they have a tendency to step into invisible heaps of shit and might also trigger other traps without you noticing. Their heels aren't really wedge heels but they don't really fit into any other heel category.");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "slowing gown") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "zamedlennoye plat'ye") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "sekinlashuvi libos"))))
			pline("It's very heavy and improves your armor class by an extra 3 points, but also slows you down to half speed.");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "foundry cloak") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "liteynyy plashch") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "quyish plash"))))
			pline("While wearing this cloak, if you quaff from a fountain you'll get extra nutrition. But quaffing from fountains is like playing russian roulette anyway. Are you foolish enough to do it?");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "fetish heels") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "idol kabluki") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "but poshnalar"))))
			pline("These high heels slow you down greatly, because seriously, they're not meant to be used for walking. After all, stiletto heels with a height of 15 cm are super cumbersome, try it in real life :P They also increase your charisma and allow you to chat to nymphs or farting monsters to pacify them. However, monsters with claw attacks will try to rip you to pieces.");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "rubynus helmet") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "rubinovyy shlem") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "yoqut asosiy dubulg'a"))))
			pline("The rubynus material is actually from Elona and increases 'life rating' there. But here, wearing this helmet will instead increase your constitution.");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "thinking helmet") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "myslyashchiy shlem") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "fikr dubulg'a"))))
			pline("This helmet thinks for you! It improves all of your stats by one. However, it's also rather self-willed and will occasionally cause you to move in an unintended direction.");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "silk fingerlings") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "shelkovyye mal'ki") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "ipak va ling, barmoqlar"))))
			pline("A thin pair of gloves that doesn't cover your fingers. Beware the cockatrice.");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "netradiation helmet") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "obluchonnyy shlem") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "sof radiatsiya dubulg'a"))))
			pline("Made of radioactive material, wearing it for a prolonged time will slowly sap your maximum health. It also has a chance of protecting you from monsters' gaze attacks.");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "velvet pumps") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "barkhatnyye nasosy") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "duxoba nasoslar"))))
			pline("Such a lovely, soft pair of high-heeled female pumps! <3 (No, the Amy does not have a shoe fetish at all. She just happens to love cone heels!)");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "hearing cloak") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "plashch dlya slukha") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "eshitish plash"))))
			pline("If you wear this cloak, listening to the dungeon becomes possible and very occasionally you'll notice a monster being spawned.");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "calf-leather sandals") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "sandalii iz telyach'yey kozhi") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "buzoq-charm kavushlari"))))
			pline("With this pair of sandals, you can kick your enemies into the shins repeatedly, and your kick will never be clumsy. However, it will also deal considerably less damage.");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "velcro sandals") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "sandalii na lipuchkakh") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "cirt kavushlari"))))
			pline("These sandals are a curse if you want to run, because they slow you down. They also don't look very good and reduce your charisma. Plus, you'll be more vulnerable to claw attacks. However, if you kick an enemy, you'll deal extra damage and paralyze the target!");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "spellsucking cloak") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "plashch zaklinaniy") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "so'rib imlo plash"))))
			pline("This cloak randomly increases or decreases your current amount of mana.");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "princess gloves") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "perchatki printsessy") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "malika qo'lqop"))))
			pline("Wearing this royal attire increases your charisma and makes lords and princes spawn peaceful more often. However, lowly monsters will hate you and try to scratch or sting you to death.");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "uncanny gloves") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "sverkh''yestestvennyye perchatki") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "dahshatli qo'lqop"))))
			pline("A pair of gloves that makes your spells both harder to cast and more expensive, but your weapon damage and accuracy will go up.");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "strip bandana") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "polosa bandanu") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "bir ip yengil bosh kiyim"))))
			pline("This headgear is so thin that it doesn't protect your head from the mind flayer's tentacles.");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "roadmap cloak") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "plashch dorozhnoy karty") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "yo'l xaritasi plash"))))
			pline("Occasionally, this cloak will reveal some information about the current dungeon level to you.");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "storm coat") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "shtorm") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "bo'ron palto"))))
			pline("With this cloak, praying to your deity will not always set a prayer timeout. Unfortunately, the game never tells you whether it did or not, though.");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "buffalo boots") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "buyvolovyye sapogi") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "qo'tos botlarni"))))
			pline("Thanks to the plateau soles, this pair of boots counts as high heels (technically one could consider them to be wedge heels). Kicking a monster with them will push it back more often, but you will encounter more heaps of shit and you will fully step into them even if you're flying.");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "fleeceling cloak") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "pushistyy plashch") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "serjunrangli plash"))))
			pline("All glyphs have a 1 in 5 chance of being fleecy-colored while you wear this.");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "heroine mocassins") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "mokasiny dlya geroini") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "qahramoni mokasen"))))
			pline("If you want to be a true heroine, wear these and the damage reduction effect you're getting from having negative AC will be better!");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "up-down cloak") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "plashch s verkhnim plashchem") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "up-pastga plash"))))
			pline("This cloak is weird. It supposedly allows you to take off armor worn underneath it, but the cloak will not come off unless you take off the armor first!");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "twisted visor helmet") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "shlem vitoy shlema") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "buekuemlue soyabon dubulg'a"))))
			pline("It protects you from blindness attacks. However, confusion and hallucination will take five times as long to time out!");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "occultism gloves") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "perchatki okkul'tizma") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "folbinlik qo'lqop"))))
			pline("A pair of gloves that reduces the amount of mana required for casting occult spells.");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "cyanism cloak") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "plashch s tsianom") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "ko'k zaharlanish plash"))))
			pline("Wearing this cloak increases the range of rocks and gems you shoot with your sling.");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "heap of shit boots") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "kucha der'movykh sapog") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "boktan etik to'p"))))
			pline("The previous owner of this pair of profiled boots stepped into a heap of shit again and again until the soles were completely immersed with the stuff, and as a result you'll aggravate monsters and not be stealthy while wearing them.");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "bluy helmet") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "siniy shlem") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "bluy dubulg'a"))))
			pline("This helmet attracts blue monsters.");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "lolita boots") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "botinki s lolitoy") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "bosh ketish etigi"))))
			pline("While wearing these heels, monsters will want to have sex with you. And the Amy will constantly swoon over your very beautiful block heels and want to marry you. <3");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "digger gloves") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "kopatel'skiye perchatki") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "kazici qo'lqop"))))
			pline("This pair of gloves improves your digging speed with tools.");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "long-range cloak") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "dlinnyy plashch") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "uzoq masofaga plash"))))
			pline("If you wear this cloak, beams and breath attacks will have more range, and it applies both ways.");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "inverse gloves") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "obratnyye perchatki") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "teskari qo'lqop"))))
			pline("Putting on these gloves will invert the enchantment value. If that causes it to become negative, they'll also become heavily cursed!");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "weapon light boots") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "legkiye botinki dlya oruzhiya") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "qurol engil etigi"))))
			pline("This fleecy-soft pair of leather boots can slit a person's leg full length in a matter of seconds because the tender combat boot heels can cause a lot of damage even though they look very cute. If you kick a monster with them, you deal a ton of extra damage by scratching the enemy with the lovely stiletto heel, but because you do not have the required weapon light to use them, it will increase your sin counter every time until the cops show up and try to arrest you!");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "slaying gloves") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "ubiystvennyye perchatki") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "o'ldirish qo'lqop"))))
			pline("These gloves increase your accuracy and damage by one point each.");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "less helmet") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "men'she shlem") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "kam dubulg'a"))))
			pline("Every time you put on this helmet, it will disenchant itself if its enchantment was positive.");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "flier cloak") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "plashch letchika") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "uchuvchi plash"))))
			pline("A cloak that allows you to move over water or lava without falling in. Careful: other ground-based hazards can still affect you!");

		if (!nn) pline("Unfortunately you don't know more about it. You will gain more information if you identify this item.");
		else { switch (obj->otyp) {

			case HAWAIIAN_SHIRT: 
				pline("A shirt that can be worn under a suit of armor. Shopkeepers who see you wearing this thing will overcharge you. It can be read."); break;
			case KYRT_SHIRT: 
				pline("This is a legendary shirt that used to be worn by Kurt Cobain. Unlike other shirts, it can be safely enchanted when it's at +5."); break;
			case T_SHIRT: 
				pline("A shirt that can be worn under a suit of armor. It can be read."); break;
			case PRINTED_SHIRT: 
				pline("It's an extra piece of armor that goes in the shirt slot. It can be read."); break;
			case WOOLEN_SHIRT: 
				pline("A shirt that provides cold resistance when worn. It can be read."); break;
			case SWIMSUIT: 
				pline("A plastic shirt that allows you to swim in water. It can be read."); break;
			case BEAUTIFUL_SHIRT: 
				pline("It's an undergarment that acts as an extra piece of armor. It can be read."); break;
			case RADIOACTIVE_UNDERGARMENT: 
				pline("A shirt that grants a lot of armor class, but also repeating vulnerability. It can be read."); break;
			case PETA_COMPLIANT_SHIRT:
				pline("Usually people are eating tasty animals, but this shirt was made of plant material. It can be read."); break;
			case PARTIAL_PLATE_MAIL:
				pline("A very heavy plate mail that offers less protection than regular plate mail."); break;
			case RIBBED_PLATE_MAIL:
				pline("This very strong armor is resistant to rust and corrosion."); break;
			case METAL_LAMELLAR_ARMOR:
				pline("A relatively good suit of armor that resists erosion."); break;
			case BAR_CHAIN_MAIL:
				pline("It's a chain mail that cannot rust, although it's still subject to corrosion."); break;
			case TAR_CHAIN_MAIL:
				pline("Chain mail made of lithic material, useful if you want to cast spells and still wear armor."); break;
			case PEACEKEEPER_MAIL:
				pline("A wooden armor that offers very high armor class and also grants peacevision."); break;
			case GOTHIC_PLATE_MAIL:
				pline("This suit offers even more protection than regular plate mail."); break;
			case EMBOSSED_PLATE_MAIL:
				pline("A very powerful suit that's superior to regular plate mail and doesn't hinder spellcasting, however it's subject to all erosion effects unless you specifically erodeproof it."); break;
			case INKA_MITHRIL_COAT:
				pline("It's misnamed, because the material it is made of isn't actually mithril, but it offers good armor class and 3 points of magic cancellation."); break;
			case DROVEN_MITHRIL_COAT:
				pline("A coat that is not made of mithril. Instead, the material is glass, which means that throwing it will cause it to shatter, but why would you do that? It offers good armor class and 3 points of magic cancellation, and due to the fact it's non-metallic, it doesn't hinder spellcasting."); break;
			case SILK_MAIL:
				pline("A weak suit of armor."); break;
			case HEAVY_MAIL:
				pline("This suit of armor weighs a hell of a lot and offers only three points of armor class."); break;
			case CLOAK_OF_PEACE:
				pline("Wearing this cloak allows you to recognize peaceful monsters without needing to farlook them. FIQ wanted that to be enabled by default without requiring an extrinsic, but there are reasons why I made it the way it is now. Ask me if you need to know the specifics. (Yes, you should really join the IRC channel, unless you deliberately want to play the game unspoiled!)"); break;
			case CLOAK_OF_DIMNESS:
				pline("If you put on this cloak, you are subjected to a status effect that halves your armor class and abuses your wisdom. And in order to fix that, you must take it off again, however these cloaks are usually generated cursed."); break;
			case CLOAK_OF_CONTAMINATION_RESISTA:
				pline("A cloak that provides maximum magic cancellation and tones down the effects of weeping angel contamination."); break;
			case ICKY_SHIELD:
				pline("It's a shield that offers great armor class, but due to the ickiness it's very hard to block projectiles with it."); break;
			case HEAVY_SHIELD:
				pline("A shield that only grants low armor class and weighs a lot."); break;
			case BARRIER_SHIELD:
				pline("This shield offers good armor class and a high chance to block."); break;
			case TROLL_SHIELD:
				pline("A shield that doesn't give all that much protection, but wearing it increases your health regeneration rate."); break;
			case TARRIER:
				pline("This shield might be a good choice for a spellcaster since it's non-metallic. It gives good AC and reasonably high chance to block."); break;
			case SHIELD_OF_PEACE:
				pline("A shield that offers a bit of armor class, low magic cancellation and moderate chance to block. On top of that it also grants peacevision."); break;
			case BATH_TOWEL: 
				pline("Putting this on does nothing special, but it counts as a shirt so you can wear it in addition to whatever other armor you're wearing. It can be read."); break;
			case PLUGSUIT: 
				pline("A suit worth one point of AC that goes in the shirt slot. It can be read."); break;
			case MEN_S_UNDERWEAR: 
				pline("Yes, you can wear it as a female too. Actually, it's a shirt. It can be read."); break;
			case BLACK_DRESS: 
				pline("This very lovely piece of cloth (which counts as a shirt) offers medium magic cancellation. It can be read."); break;
			case BODYGLOVE: 
				pline("Good thing this item is not unbalanced at all. Just a t-shirt that offers sickness resistance and maximum magic cancel-WAIT WHAT OMG I GOTTA WEAR THIS SO BAD!!! It can be read."); break;
			case STRIPED_SHIRT: 
				pline("A shirt that can be worn under a suit of armor. Shopkeepers who see you wearing this will not allow you to enter their shop. It can be read."); break;
			case RUFFLED_SHIRT: 
				pline("A shirt that can be worn under a suit of armor. If you wear a cursed one, you have a small chance of reviving on death. It can be read."); break;
			case VICTORIAN_UNDERWEAR: 
				pline("This wonderful piece of clothing can be worn under a suit of armor to grant 3 points of magic cancellation. If you wear a cursed one, you have a small chance of reviving on death. It can be read."); break;
			case PLATE_MAIL: 
				pline("A very heavy suit of armor that offers good protection."); break;
			case FULL_PLATE_MAIL: 
				pline("This suit of armor covers your whole body and offers great protection, but it's also really heavy."); break;
			case DROVEN_PLATE_MAIL: 
				pline("This plate mail is light, but it offers less protection than actual plate mail."); break;
			case PLASTEEL_ARMOR: 
				pline("A low-weight suit of armor with a good armor value."); break;
			case CRYSTAL_PLATE_MAIL: 
				pline("A very heavy suit of armor that offers good protection."); break;
			case BRONZE_PLATE_MAIL: 
				pline("This suit of armor is inferior to regular plate mail."); break;
			case SPLINT_MAIL: 
				pline("A robust suit of armor that offers good protection."); break;
			case OLIHARCON_SPLINT_MAIL: 
				pline("This robust suit of armor is made of a rare material whose name nobody can spell correctly (for reference: it's ORICHALCUM); it offers good protection."); break;
			case BAMBOO_MAIL: 
				pline("A relatively weak suit of armor."); break;
			case SAILOR_BLOUSE: 
				pline("It's light and yet offers some protection."); break;
			case SAFEGUARD_SUIT: 
				pline("Almost no AC but it grants you the ability to swim."); break;
			case FEATHER_ARMOR: 
				pline("Believe it or not, this armor actually has magic cancellation in addition to its low amount of AC."); break;
			case SCHOOL_UNIFORM: 
				pline("A useless armor that you should wear only if you really can't find anything better."); break;
			case BUNNY_UNIFORM: 
				pline("Wanna look funny? Then wear this. Otherwise, get a real armor."); break;
			case MAID_DRESS: 
				pline("Despite being very beautiful and sexy, this dress doesn't actually do anything special."); break;
			case NURSE_UNIFORM: 
				pline("In some other NetHack variant this uniform grants the player a special ability, but here it's just cosmetical."); break;
			case COMMANDER_SUIT: 
				pline("A suit of armor that offers medium amounts of protection."); break;
			case CAMOUFLAGED_CLOTHES: 
				pline("Wearing this does not make it any harder for monsters to see you, and it ain't got good AC either."); break;
			case SPECIAL_CAMOUFLAGED_CLOTHES: 
				pline("Enemies will have trouble finding you if you wear this light armor, and it also offers a small amount of protection."); break;
			case SHOULDER_RINGS: 
				pline("A weird construction that protects parts of your body, granting medium magic cancellation."); break;
			case BANDED_MAIL: 
				pline("A robust suit of armor that offers good protection."); break;
			case DWARVISH_MITHRIL_COAT: 
				pline("A low-weight suit of armor that offers moderate protection and 3 points of magic cancellation."); break;
			case DARK_ELVEN_MITHRIL_COAT: 
				pline("This is one of the best suits of armor that is not a dragon scale mail, and provides 3 points of magic cancellation."); break;
			case ELVEN_MITHRIL_COAT: 
				pline("A low-weight suit of armor that offers good protection and 3 points of magic cancellation."); break;
			case GNOMISH_SUIT: 
				pline("This suit of armor offers very little protection."); break;
			case ELVEN_TOGA: 
				pline("Light armor that gives medium magic cancellation."); break;
			case NOBLE_S_DRESS: 
				pline("A tank made of mineral that can be worn for great protection and 3 points of magic cancellation."); break;
			case CONSORT_S_SUIT: 
				pline("Low-protection armor. It gives MC though."); break;
			case FORCE_ARMOR: 
				pline("Looking for light armor that still gives 3 points of magic cancellation? Then this is for you."); break;
			case HEALER_UNIFORM: 
				pline("Nothing special, although I might make this thing do something if you're actually playing a healer."); break;
			case JUMPSUIT: 
				pline("Reflection and MC3! This thing's overpowered, I tell ya!"); break;
			case CHAIN_MAIL: 
				pline("A moderately good suit of armor."); break;
			case DROVEN_CHAIN_MAIL: 
				pline("Less protection than regular chain mail but better magic cancellation."); break;
			case ORCISH_CHAIN_MAIL: 
				pline("A crappier version of regular chain mail that offers mediocre protection."); break;
			case SCALE_MAIL: 
				pline("A medium-weight metallic suit of armor that offers mediocre protection."); break;
			case STUDDED_LEATHER_ARMOR: 
				pline("This is a suit of armor made of leather that offers some protection."); break;
			case RING_MAIL: 
				pline("A metallic suit of armor that offers little protection."); break;
			case ORCISH_RING_MAIL: 
				pline("This suit of metal armor offers very little protection."); break;
			case LEATHER_ARMOR: 
				pline("A basic suit of armor that offers little protection."); break;
			case TROLL_LEATHER_ARMOR: 
				pline("A suit of armor that offers little protection, but also regenerates your hit points much faster."); break;
			case LEATHER_JACKET: 
				pline("This thing is only useful if you don't have a better suit of armor."); break;
			case GENTLEMAN_S_SUIT: 
				pline("You should wear this only if you lack a real armor, for it only gives low magic cancellation and nothing else."); break;
			case GENTLEWOMAN_S_DRESS: 
				pline("If you're having no luck finding something with 3 points of magic cancellation, this can be used as a substitute. It only grants a single point of AC though, which is really bad for a body armor."); break;
			case STRAITJACKET: 
				pline("Bad AC and medium magic cancellation aren't good enough to make this 'armor' worth using IMHO, plus you'll look like a retard if you wear it."); break;
			case CURING_UNIFORM: 
				pline("One of Chris_ANG's overpowered creations, this uniform grants sickness resistance. However, there is a dragon scale mail that gives the same property and much more AC. :D"); break;
			case HAWAIIAN_SHORTS:
				pline("A totally useless armor that grants absolutely nothing. Bonus points if it is cursed and prevents you from wearing an actual armor."); break;
			case ROBE:
				pline("Robes can be worn instead of armor. This is mainly useful for monks and jedi who are penalized for wearing 'real' armor."); break;
			case ROBE_OF_PROTECTION:
				pline("If you don't want to wear a real armor, you can use this for some armor class."); break;
			case ROBE_OF_POWER:
				pline("Wearing this robe improves your spellcasting ability but prevents you from wearing an actual suit of armor."); break;
			case ROBE_OF_WEAKNESS:
				pline("If you wear this robe, you will be permanently stunned. They are usually generated cursed."); break;
			case ROBE_OF_MAGIC_RESISTANCE:
				pline("A robe that provides magic resistance."); break;
			case ROBE_OF_PERMANENCE:
				pline("This robe provides several elemental resistances when worn!"); break;
			case ROBE_OF_SPELL_POWER:
				pline("Your spells will be much easier to cast while you wear this."); break;
			case ROBE_OF_FAST_CASTING:
				pline("Grants energy regeneration when worn."); break;
			case ROBE_OF_ENERGY_SUCTION:
				pline("You can wear this robe to become a better caster - every time you kill something, you recover a bit of mana!"); break;
			case ROBE_OF_RANDOMNESS:
				pline("A robe with a random enchantment, which happens to be %s. AC is %d, and MC is %d.", enchname(objects[ROBE_OF_RANDOMNESS].oc_oprop), objects[ROBE_OF_RANDOMNESS].a_ac, objects[ROBE_OF_RANDOMNESS].a_can); break;
			case ROBE_OF_SPECIALTY:
				pline("This robe is always generated with some special enchantment."); break;
			case ROBE_OF_DEFENSE:
				pline("A robe that provides good armor class and 3 points of magic cancellation."); break;
			case ROBE_OF_NASTINESS:
				pline("Wear this robe if you're feeling ballsy, and are ready to suffer from a nasty side effect in exchange for great armor class and magic cancellation."); break;
			case ROBE_OF_PSIONICS:
				pline("This robe gives psi resistance."); break;
			case GRAY_DRAGON_SCALE_MAIL:
				pline("This armor offers great protection as well as magic resistance."); break;
			case SILVER_DRAGON_SCALE_MAIL:
				pline("This armor offers great protection as well as reflection."); break;
			case MERCURIAL_DRAGON_SCALE_MAIL:
				pline("This armor offers great protection as well as extra speed."); break;
			case SHIMMERING_DRAGON_SCALE_MAIL:
				pline("This armor offers great protection as well as displacement."); break;
			case DEEP_DRAGON_SCALE_MAIL:
				pline("This armor offers great protection as well as level-drain resistance."); break;
			case RED_DRAGON_SCALE_MAIL:
				pline("This armor offers great protection as well as fire resistance."); break;
			case WHITE_DRAGON_SCALE_MAIL: 
				pline("This armor offers great protection as well as cold resistance."); break;
			case ORANGE_DRAGON_SCALE_MAIL: 
				pline("This armor offers great protection as well as sleep resistance."); break;
			case BLACK_DRAGON_SCALE_MAIL: 
				pline("This armor offers great protection as well as disintegration resistance."); break;
			case BLUE_DRAGON_SCALE_MAIL: 
				pline("This armor offers great protection as well as shock resistance."); break;
			case COPPER_DRAGON_SCALE_MAIL: 
				pline("This armor offers great protection as well as %s.", enchname(objects[COPPER_DRAGON_SCALE_MAIL].oc_oprop) ); break;
			case PLATINUM_DRAGON_SCALE_MAIL: 
				pline("This armor offers great protection as well as %s.", enchname(objects[PLATINUM_DRAGON_SCALE_MAIL].oc_oprop) ); break;
			case BRASS_DRAGON_SCALE_MAIL: 
				pline("This armor offers great protection as well as %s.", enchname(objects[BRASS_DRAGON_SCALE_MAIL].oc_oprop) ); break;
			case AMETHYST_DRAGON_SCALE_MAIL: 
				pline("This armor offers great protection as well as %s.", enchname(objects[AMETHYST_DRAGON_SCALE_MAIL].oc_oprop) ); break;
			case PURPLE_DRAGON_SCALE_MAIL: 
				pline("This armor offers great protection as well as %s.", enchname(objects[PURPLE_DRAGON_SCALE_MAIL].oc_oprop) ); break;
			case DIAMOND_DRAGON_SCALE_MAIL: 
				pline("This armor offers great protection as well as %s.", enchname(objects[DIAMOND_DRAGON_SCALE_MAIL].oc_oprop) ); break;
			case EMERALD_DRAGON_SCALE_MAIL: 
				pline("This armor offers great protection as well as %s.", enchname(objects[EMERALD_DRAGON_SCALE_MAIL].oc_oprop) ); break;
			case SAPPHIRE_DRAGON_SCALE_MAIL: 
				pline("This armor offers great protection as well as %s.", enchname(objects[SAPPHIRE_DRAGON_SCALE_MAIL].oc_oprop) ); break;
			case RUBY_DRAGON_SCALE_MAIL: 
				pline("This armor offers great protection as well as %s.", enchname(objects[RUBY_DRAGON_SCALE_MAIL].oc_oprop) ); break;
			case GREEN_DRAGON_SCALE_MAIL: 
				pline("This armor offers great protection as well as poison resistance."); break;
			case GOLDEN_DRAGON_SCALE_MAIL: 
				pline("This armor offers great protection as well as sickness resistance."); break;
			case STONE_DRAGON_SCALE_MAIL: 
				pline("This armor offers great protection as well as petrification resistance."); break;
			case CYAN_DRAGON_SCALE_MAIL: 
				pline("This armor offers great protection as well as fear resistance."); break;
			case PSYCHIC_DRAGON_SCALE_MAIL: 
				pline("This armor offers great protection as well as ESP."); break;
			case YELLOW_DRAGON_SCALE_MAIL: 
				pline("This armor offers great protection as well as acid resistance."); break;
			case RAINBOW_DRAGON_SCALE_MAIL: 
				pline("This armor offers great protection as well as resistance to spell damage."); break;
			case BLOOD_DRAGON_SCALE_MAIL: 
				pline("This armor offers great protection as well as resistance to normal damage."); break;
			case PLAIN_DRAGON_SCALE_MAIL: 
				pline("This armor offers a huge amount of protection."); break;
			case SKY_DRAGON_SCALE_MAIL: 
				pline("This armor offers great protection as well as flying."); break;
			case WATER_DRAGON_SCALE_MAIL: 
				pline("This armor offers great protection as well as swimming."); break;
			case GRAY_DRAGON_SCALES: 
				pline("This armor offers moderate protection as well as magic resistance."); break;
			case SILVER_DRAGON_SCALES: 
				pline("This armor offers moderate protection as well as reflection."); break;
			case MERCURIAL_DRAGON_SCALES: 
				pline("This armor offers moderate protection as well as extra speed."); break;
			case SHIMMERING_DRAGON_SCALES: 
				pline("This armor offers moderate protection as well as displacement."); break;
			case DEEP_DRAGON_SCALES: 
				pline("This armor offers moderate protection as well as level-drain resistance."); break;
			case RED_DRAGON_SCALES: 
				pline("This armor offers moderate protection as well as fire resistance."); break;
			case WHITE_DRAGON_SCALES: 
				pline("This armor offers moderate protection as well as cold resistance."); break;
			case ORANGE_DRAGON_SCALES: 
				pline("This armor offers moderate protection as well as sleep resistance."); break;
			case BLACK_DRAGON_SCALES: 
				pline("This armor offers moderate protection as well as disintegration resistance."); break;
			case BLUE_DRAGON_SCALES: 
				pline("This armor offers moderate protection as well as shock resistance."); break;
			case COPPER_DRAGON_SCALES: 
				pline("This armor offers moderate protection as well as %s.", enchname(objects[COPPER_DRAGON_SCALES].oc_oprop) ); break;
			case PLATINUM_DRAGON_SCALES: 
				pline("This armor offers moderate protection as well as %s.", enchname(objects[PLATINUM_DRAGON_SCALES].oc_oprop) ); break;
			case BRASS_DRAGON_SCALES: 
				pline("This armor offers moderate protection as well as %s.", enchname(objects[BRASS_DRAGON_SCALES].oc_oprop) ); break;
			case AMETHYST_DRAGON_SCALES: 
				pline("This armor offers moderate protection as well as %s.", enchname(objects[AMETHYST_DRAGON_SCALES].oc_oprop) ); break;
			case PURPLE_DRAGON_SCALES: 
				pline("This armor offers moderate protection as well as %s.", enchname(objects[PURPLE_DRAGON_SCALES].oc_oprop) ); break;
			case DIAMOND_DRAGON_SCALES: 
				pline("This armor offers moderate protection as well as %s.", enchname(objects[DIAMOND_DRAGON_SCALES].oc_oprop) ); break;
			case EMERALD_DRAGON_SCALES: 
				pline("This armor offers moderate protection as well as %s.", enchname(objects[EMERALD_DRAGON_SCALES].oc_oprop) ); break;
			case SAPPHIRE_DRAGON_SCALES: 
				pline("This armor offers moderate protection as well as %s.", enchname(objects[SAPPHIRE_DRAGON_SCALES].oc_oprop) ); break;
			case RUBY_DRAGON_SCALES: 
				pline("This armor offers moderate protection as well as %s.", enchname(objects[RUBY_DRAGON_SCALES].oc_oprop) ); break;
			case GREEN_DRAGON_SCALES: 
				pline("This armor offers moderate protection as well as poison resistance."); break;
			case GOLDEN_DRAGON_SCALES: 
				pline("This armor offers moderate protection as well as sickness resistance."); break;
			case STONE_DRAGON_SCALES: 
				pline("This armor offers moderate protection as well as petrification resistance."); break;
			case CYAN_DRAGON_SCALES: 
				pline("This armor offers moderate protection as well as fear resistance."); break;
			case PSYCHIC_DRAGON_SCALES: 
				pline("This armor offers moderate protection as well as ESP."); break;
			case YELLOW_DRAGON_SCALES: 
				pline("This armor offers moderate protection as well as acid resistance."); break;
			case RAINBOW_DRAGON_SCALES: 
				pline("This armor offers moderate protection as well as resistance to spell damage."); break;
			case BLOOD_DRAGON_SCALES: 
				pline("This armor offers moderate protection as well as resistance to normal damage."); break;
			case PLAIN_DRAGON_SCALES: 
				pline("This armor offers a very large amount of protection."); break;
			case SKY_DRAGON_SCALES: 
				pline("This armor offers moderate protection as well as flying."); break;
			case WATER_DRAGON_SCALES: 
				pline("This armor offers moderate protection as well as swimming."); break;
			case MUMMY_WRAPPING: 
				pline("A cloak that can be worn to suppress invisibility. Other than that, it's inferior to most other cloaks."); break;
			case ORCISH_CLOAK: 
				pline("This cloak provides medium magic cancellation and no other protection."); break;
			case DWARVISH_CLOAK: 
				pline("This cloak provides medium magic cancellation and no other protection."); break;
			case OILSKIN_CLOAK: 
				pline("A very useful cloak that provides 3 points of magic cancellation and also protects from grabbing attacks."); break;
			case ELVEN_CLOAK: 
				pline("A powerful cloak that provides 3 points of magic cancellation and stealth."); break;
			case DROVEN_CLOAK: 
				pline("This cloak only provides 3 points of magic cancellation and nothing else. I guess if you don't have anything better, you may want to use it..."); break;
			case LAB_COAT: 
				pline("This cloak is highly useful as it provides all of the following: 3 points of magic cancellation, poison resistance and acid resistance."); break;
			case LEATHER_CLOAK: 
				pline("A basic cloak that has low magic cancellation."); break;
			case ALCHEMY_SMOCK:
				pline("Yes, it's back, and it's got 3 points of magic cancellation to boot... but unlike the lab coat, it only gives poison resistance."); break;
			case LEO_NEMAEUS_HIDE:
				pline("Good AC and half physical damage might almost make this cloak worth using, but it doesn't give any magic cancellation at all..."); break;
			case PLASTEEL_CLOAK: 
				pline("A lightweight cloak with medium magic cancellation and robust armor class."); break;
			case CLOAK_OF_PROTECTION: 
				pline("This cloak provides good armor class and 3 points of magic cancellation."); break;
			case CLOAK_OF_FUMBLING: 
				pline("A cloak that provides 3 points of magic cancellation, but also causes fumbling when worn."); break;
			case POISONOUS_CLOAK: 
				pline("Wearing this cloak without poison resistance can kill you. Other than that, it provides 3 points of magic cancellation."); break;
			case CLOAK_OF_DEATH: 
				pline("Putting this cloak on without magic resistance can kill you. Other than that, it provides 3 points of magic cancellation."); break;
			case CLOAK_OF_INVISIBILITY: 
				pline("This cloak renders the wearer invisible and also grants medium magic cancellation."); break;
			case CLOAK_OF_MAGIC_RESISTANCE: 
				pline("A superb cloak that provides magic resistance in addition to 3 points of magic cancellation."); break;
			case CLOAK_OF_DRAIN_RESISTANCE: 
				pline("A cloak that some guy named KMH removed from the game but Amy added it back in. It provides drain resistance in addition to 3 points of magic cancellation."); break;
			case CLOAK_OF_REFLECTION: 
				pline("A superb cloak that provides reflection in addition to 3 points of magic cancellation."); break;
			case MANACLOAK: 
				pline("A superb cloak that provides energy regeneration in addition to 3 points of magic cancellation."); break;
			case CLOAK_OF_CONFUSION: 
				pline("Wearing this cloak will confuse you, but it also has good armor class and 3 points of magic cancellation."); break;
			case CLOAK_OF_WARMTH: 
				pline("Wearing this cloak grants cold resistance and 3 points of magic cancellation."); break;
			case TROLL_HIDE: 
				pline("Wearing this cloak grants regeneration and 3 points of magic cancellation."); break;
			case CLOAK_OF_GROUNDING: 
				pline("Wearing this cloak grants shock resistance and medium magic cancellation."); break;

			case CLOAK_OF_UNSPELLING: 
				pline("This cloak causes spell loss. It grants good armor class and 3 points of magic cancellation."); break;
			case ANTI_CASTER_CLOAK: 
				pline("This cloak causes casting problems. It grants very good armor class and 3 points of magic cancellation."); break;
			case HEAVY_STATUS_CLOAK: 
				pline("This cloak causes heavy status effects. It grants extremely good armor class and 4 points of magic cancellation."); break;
			case CLOAK_OF_LUCK_NEGATION: 
				pline("This cloak causes bad luck. It grants extremely good armor class and 3 points of magic cancellation."); break;
			case YELLOW_SPELL_CLOAK: 
				pline("This cloak causes yellow spells. It grants great armor class and 8 points of magic cancellation."); break;
			case VULNERABILITY_CLOAK: 
				pline("This cloak causes vulnerability. It grants low armor class and 5 points of magic cancellation."); break;
			case CLOAK_OF_INVENTORYLESSNESS: 
				pline("This cloak causes inventory loss. It grants incredibly great armor class and 9 points of magic cancellation."); break;
			case CLOAK_OF_RESPAWNING: 
				pline("This cloak causes monster respawn. It grants very good armor class and medium magic cancellation."); break;
			case ADOM_CLOAK: 
				pline("This cloak causes monsters to be of a higher level. It grants good armor class and no magic cancellation."); break;
			case SPAWN_CLOAK: 
				pline("This cloak causes faster respawn. It grants good armor class and low magic cancellation."); break;
			case EGOIST_CLOAK: 
				pline("This cloak causes egotype monster spawns. It grants mediocre armor class and low magic cancellation."); break;
			case CLOAK_OF_TIME: 
				pline("This cloak causes faster passing of time. It grants very good armor class and medium magic cancellation."); break;

			case CHATBOX_CLOAK: 
				pline("This cloak causes messages to be replaced randomly. It grants good armor class and 5 points of magic cancellation."); break;
			case HERETIC_CLOAK:
				pline("This cloak causes altars to malfunction. It grants moderate armor class and medium magic cancellation."); break;
			case EERIE_CLOAK:
				pline("This cloak turns monsters into ghosts. It grants good armor class and 3 points of magic cancellation."); break;
			case CLOAK_OF_NAKEDNESS:
				pline("This cloak causes nakedness and grants 3 points of magic cancellation. If someone else than you wears it, it also grants incredibly good armor class to the wearer, nyah-nyah!"); break;

			case WHITE_SPELL_CLOAK:
				pline("This cloak causes white spells. It grants good armor class and 3 points of magic cancellation."); break;
			case GREYOUT_CLOAK:
				pline("This cloak causes completely gray spells. It grants very good armor class and 3 points of magic cancellation. Don't confuse it with the grayout cloak, which has a completely different effect."); break;
			case TRUMP_COAT:
				pline("This cloak causes intrinsic nasty effects. It grants superb armor class and 9 points of magic cancellation."); break;
			case CLOAK_OF_SUDDEN_ATTACK:
				pline("This cloak causes monsters to use secret attacks. It grants very good armor class and 5 points of magic cancellation."); break;
			case CLOAK_OF_BAD_TRAPPING:
				pline("This cloak causes all bad traps to have the same spawn chance. It grants very good armor class and 7 points of magic cancellation."); break;
			case GRAYOUT_CLOAK:
				pline("This cloak causes random grayouts of the playing field. It grants very good armor class and 5 points of magic cancellation. Don't confuse it with the greyout cloak, which has a completely different effect."); break;
			case PSEUDO_TELEPORTER_CLOAK:
				pline("This cloak obscures your immediate surroundings. It grants great armor class and 3 points of magic cancellation."); break;
			case CLOCKLOAK:
				pline("This cloak completely fucks up your directional keys. It grants incredibly good armor class and 8 points of magic cancellation."); break;
			case NOKEDEX_CLOAK:
				pline("This cloak disables the pokedex. It grants good armor class and no magic cancellation."); break;
			case NAYLIGHT_CLOAK:
				pline("This cloak stops the hilite patch from doing its thing. It grants moderate armor class and no points of magic cancellation."); break;
			case BATTERY_CLOAK:
				pline("This cloak turns you into a living mana battery. It grants moderate armor class and medium magic cancellation."); break;
			case CLOAK_OF_WRONG_ANNOUNCEMENT:
				pline("This cloak stops the bottom status line from updating automatically. It grants mediocre armor class and 3 points of magic cancellation."); break;
			case STORMY_CLOAK:
				pline("This cloak makes you bloodthirsty, like the Stormbringer. It grants low armor class and 3 points of magic cancellation."); break;
			case GIMP_CLOAK:
				pline("This cloak causes you to always take maximum damage from attacks. It grants no armor class and 9 points of magic cancellation."); break;
			case SNARENET_CLOAK:
				pline("This cloak prevents you from identifying the type of a trap. It grants excellent armor class and 7 points of magic cancellation."); break;
			case PINK_SPELL_CLOAK:
				pline("This cloak causes pink spells. It grants great armor class and 8 points of magic cancellation."); break;
			case EVENCORE_CLOAK:
				pline("This cloak spawns evencore pictures. It grants almost no armor class and 3 points of magic cancellation."); break;
			case UNDERLAYER_CLOAK:
				pline("This cloak spawns hidden markers that cause you to lose turns. It grants mediocre armor class and 3 points of magic cancellation."); break;
			case CYAN_SPELL_CLOAK:
				pline("This cloak causes cyan spells. It grants good armor class and 4 points of magic cancellation."); break;
			case ELONGATION_CLOAK:
				pline("This cloak increases the range of certain monster attacks. It grants quite good armor class and 3 points of magic cancellation."); break;
			case UNFAIR_ATTACK_CLOAK:
				pline("This cloak allows monsters to use unfair attacks on you. It grants great armor class and 7 points of magic cancellation."); break;

			case CLOAK_OF_QUENCHING: 
				pline("Wearing this cloak grants fire resistance and 3 points of magic cancellation."); break;

			case CLOAK_OF_AGGRAVATION:
				pline("Usually generated cursed. It makes monsters come after you directly, but provides 3 points of magic cancellation."); break;
			case CLOAK_OF_CONFLICT:
				pline("Usually generated cursed, and autocurses when worn. It causes monsters to attack each other and you. And it also grants 3 points of magic cancellation."); break;
			case CLOAK_OF_MAGICAL_BREATHING:
				pline("You will no longer need to breathe if you wear this, and it gives the mandatory 3 points of magic cancellation that you want on an ascension cloak."); break;
			case CLOAK_OF_STAT_LOCK:
				pline("This cloak prevents your stats from changing. It is usually generated cursed and provides medium magic cancellation."); break;
			case WING_CLOAK:
				pline("You can fly like an angel and also get 3 points of magic cancellation as well as a bit of extra AC if you wear this!"); break;
			case CLOAK_OF_PREMATURE_DEATH:
				pline("Wear this if you don't mind suddenly dying for no reason. Yes, it gives 3 points of magic cancellation, but there are better cloaks that do so too..."); break;
			case ANTIDEATH_CLOAK:
				pline("Death beam resistance and, of course, 3 points of magic cancellation. That's what you get if you don this cloak."); break;
			case CLOAK_OF_LEECH:
				pline("Very useful cloak that grants 3 points of magic cancellation and manaleech."); break;
			case FILLER_CLOAK:
				pline("This cloak gives 3 points of magic cancellation and nothing else."); break;

			case LETHE_CLOAK:
				pline("3 points of magic cancellation, but putting it on causes amnesia once."); break;
			case CLOAK_OF_MAP_AMNESIA:
				pline("This cloak provides 3 points of magic cancellation and prevents you from remembering the map."); break;
			case CLOAK_OF_POLYMORPH:
				pline("Trying to wear this cloak will polymorph both you and the cloak!"); break;
			case CLOAK_OF_TRANSFORMATION:
				pline("A cloak that causes polymorphitis and is usually generated cursed. It provides 3 points of magic cancellation."); break;
			case CLOAK_OF_WATER_SQUIRTING:
				pline("Putting it on will wet your entire inventory, but once you do actually have it on, it's just a MC3 cloak."); break;
			case CLOAK_OF_PARALYSIS:
				pline("If you put on this cloak, you will be paralyzed for a few turns. It doesn't cause more paralysis while worn though, and has 3 points of magic cancellation."); break;
			case CLOAK_OF_SICKNESS:
				pline("You will become deathly sick in a way that cannot be vomited out of your body if you wear this. Cure the sickness and it will act as a standard cloak with 3 points of magic cancellation."); break;
			case CLOAK_OF_SLIMING:
				pline("A cloak that turns you to slime if you put it on. If you can solve that problem, it will then give 3 points of magic cancellation and nothing else."); break;
			case CLOAK_OF_STARVING:
				pline("This cloak makes you very hungry if you wear it, but it only happens during the act of putting it on. Afterwards it's safe to keep on, and gives 3 points of magic cancellation."); break;
			case CLOAK_OF_CURSE:
				pline("You will probably lose some of your intrinsics if you wear this cloak, but once you actually wear it, no more bad stuff will happen. Instead, you get 3 points of magic cancellation."); break;
			case CLOAK_OF_DISENCHANTMENT:
				pline("Wearing this cloak will try to disenchant your open inventory, but as soon as you have it on, it's safe. It also provides 3 points of magic cancellation."); break;
			case CLOAK_OF_OUTRIGHT_EVILNESS:
				pline("The act of putting on this cloak can curse your entire inventory. Afterwards, it will provide a bit of AC and 3 points of magic cancellation without further curse effects."); break;
			case CLOAK_OF_STONE:
				pline("Wear it, and you'll turn to stone! But if you can cure that, it will then act as a standard MC3 cloak."); break;
			case CLOAK_OF_LYCANTHROPY:
				pline("Putting this cloak on allows you to acquire lycanthropy, which will always make you a wereWOLF as opposed to another wereform. It also gives more AC than usual and 3 points of magic cancellation."); break;
			case CLOAK_OF_UNLIGHT:
				pline("If you put on this cloak, the area around you will become unlit. But you can walk around in it normally and it won't darken any other areas unless you take it off and put it on again. It also has 3 points of magic cancellation."); break;
			case CLOAK_OF_ESCALATION:
				pline("Putting it on increases the escalation damage amount, and while you wear it, escalation won't time out. It also gives 3 points of magic cancellation."); break;
			case CLOAK_OF_MAGICAL_DRAINAGE:
				pline("Putting it on drains your mana once, then it works as a standard 3 points of magic cancellation cloak."); break;
			case CLOAK_OF_ANGRINESS:
				pline("Wearing this cloak will anger your deity, which is probably not a good idea, but if you don't care about that, it will then provide 3 points of AC and magic cancellation of 3."); break;
			case CLOAK_OF_CANCELLATION:
				pline("If you wear it, there is a chance that your items are cancelled once. After that it just gives 3 points of magic cancellation."); break;
			case CLOAK_OF_TURN_LOSS:
				pline("The turn counter will increase by 1000 if you wear it, and this cloak will also heavily autocurse. It provides 3 points of magic cancellation."); break;
			case CLOAK_OF_ATTRIBUTE_LOSS:
				pline("Wearing this cloak will decrease all of your stats by one. It provides 3 points of magic cancellation."); break;
			case CLOAK_OF_TOTTER:
				pline("You probably should not put this on, because if you do, your directional keys will be reversed _permanently_! It also gives 3 points of AC and 3 points of magic cancellation."); break;
			case CLOAK_OF_DRAIN_LIFE:
				pline("If you put it on, this cloak will drain you once. Afterwards it's just a plain cloak with 3 points of magic cancellation."); break;
			case CLOAK_OF_AWAKENING:
				pline("Sleep resistance and 3 points of magic cancellation."); break;
			case CLOAK_OF_STABILITY:
				pline("This cloak provides disintegration resistance and 3 points of magic cancellation!"); break;
			case ANTI_DISQUIET_CLOAK:
				pline("A cloak that makes you resistant to poison, and it also has the required MC3."); break;
			case HUGGING_GOWN:
				pline("It's a kind of lab coat, although this one only provides acid resistance. It also has 3 points of magic cancellation."); break;
			case COCLOAK:
				pline("Is its name a pun on the words 'cock' and 'cloak' sounding similar? Anyway, it protects you from cockatrices by making you resistant to stoning, and it has 3 points of magic cancellation to boot!"); break;
			case CLOAK_OF_HEALTH:
				pline("Provides regeneration and MC3."); break;
			case CLOAK_OF_DISCOVERY:
				pline("This cloak grants autosearching as well as MC3."); break;
			case BIONIC_CLOAK:
				pline("You can see invisible things while wearing this cloak, and will also have 3 points of magic cancellation."); break;
			case CLOAK_OF_PORTATION:
				pline("Teleportitis and MC3 are the effects of wearing this cloak!"); break;
			case CLOAK_OF_CONTROL:
				pline("A cloak that allows you to control teleports, and it gives 3 points of magic cancellation."); break;
			case CLOAK_OF_SHIFTING:
				pline("You will have polymorph control while wearing it, and MC3 of course."); break;
			case FLOATCLOAK:
				pline("It causes you to levitate, and also provides 3 points of magic cancellation."); break;
			case CLOAK_OF_PRESCIENCE:
				pline("A cloak that provides warning and 3 points of magic cancellation."); break;
			case SENSOR_CLOAK:
				pline("You will have extra-sensory perception as well as 3 points of magic cancellation while wearing it."); break;
			case CLOAK_OF_SPEED:
				pline("Well, if you guessed that wearing it will make you very fast, you're right! And like any other good ascension kit cloak, it provides 3 points of magic cancellation."); break;
			case VAULT_CLOAK:
				pline("A cloak that allows you to jump around while also giving 3 points of magic cancellation."); break;
			case CLOAK_OF_SPELL_RESISTANCE:
				pline("You will resist spell damage by wearing this, and it also has MC3."); break;
			case CLOAK_OF_PHYSICAL_RESISTANCE:
				pline("You will resist physical damage by wearing this, and it also has MC3."); break;
			case OPERATION_CLOAK:
				pline("This cloak prevents you from becoming deathly sick and also has 3 points of magic cancellation."); break;
			case BESTEST_CLOAK:
				pline("It's the best cloak in existence! Putting it on will reveal the locations of all undead monsters, and of course it has to have MC3 as well!"); break;
			case CLOAK_OF_FREEDOM:
				pline("A cloak of free action. You probably expect it to have 3 points of magic cancellation, and of course it does."); break;
			case CLOAK_OF_DISCOUNT_ACTION:
				pline("This cloak reduces the time for which you're paralyzed, and also provides 3 points of magic cancellation."); break;
			case CLOAK_OF_TECHNICALITY:
				pline("A cloak that makes your techniques more effective while worn. It grants 3 points of magic cancellation."); break;
			case CLOAK_OF_FULL_NUTRITION:
				pline("With this cloak, actions that use up nutrition will use up less than usual. Also, it provides 3 points of magic cancellation."); break;
			case BIKINI:
				pline("A cloak that allows you to swim in water, but unfortunately it only provides medium magic cancellation."); break;
			case CLOAK_OF_PERMANENCE:
				pline("This cloak prevents you from polymorphing, so it should actually be called 'cloak of unchanging'. It also grants 3 points of magic cancellation."); break;
			case CLOAK_OF_SLOW_DIGESTION:
				pline("It slows your metabolism while also granting MC3."); break;
			case CLOAK_OF_INFRAVISION:
				pline("If you're not of a race that has intrinsic infravision, you can wear this cloak to get it extrinsically. It also gives MC3 extrinsically, because there is no intrinsic MC. :-P"); break;
			case CLOAK_OF_BANISHING_FEAR:
				pline("You cannot be subjected to the detrimental 'Fear' status effect while wearing this cloak, and your magic cancellation will also be 3."); break;
			case CLOAK_OF_MEMORY:
				pline("Provides keen memory and 3 points of magic cancellation."); break;
			case CLOAK_OF_THE_FORCE:
				pline("While wearing this cloak, the power of the #force command is extended if you use it on monsters. It also grants 3 points of magic cancellation."); break;
			case CLOAK_OF_SEEING:
				pline("Your line of sight will extend to 2 squares while wearing it, and it also gives 3 points of magic cancellation."); break;
			case CLOAK_OF_CURSE_CATCHING:
				pline("This very helpful cloak makes you highly resistant to the obnoxious 'curse items' spell that monsters like to use, and also grants 3 points of magic cancellation."); break;
			case LION_CLOAK:
				pline("A cloak that grants stun resistance; you can still be stunned, but the effect will be less crippling. And of course it provides MC3."); break;
			case TIGER_CLOAK:
				pline("This special cloak will lessen the effects of confusion and also provides MC3."); break;
			case CLOAK_OF_PRACTICE:
				pline("You can wear this cloak if you want to train your skills more quickly, and it grants MC3 as well!"); break;
			case CLOAK_OF_ELEMENTALISM:
				pline("A cloak that makes you resistant to the base elements while also giving you 3 points of magic cancellation."); break;
			case PSIONIC_CLOAK:
				pline("Psi resistance and 3 points of magic cancellation are what you get by wearing this."); break;

			case AYANAMI_WRAPPING:
				pline("No defense but low magic cancellation."); break;
			case RUBBER_APRON:
				pline("A cloak that grants nothing but a single point of armor class."); break;
			case KITCHEN_APRON:
				pline("This cloak doesn't do anything when worn."); break;
			case FRILLED_APRON:
				pline("You might have mistaken this item for an alchemy smock, but it's just a plain apron."); break;
			case SUPER_MANTLE:
				pline("Wanna look like Superman? Sure, you can do so now. But it doesn't grant you any superpowers, just low armor class and magic cancellation."); break;
			case WINGS_OF_ANGEL:
				pline("These wings cannot actually be used to fly, however they sell for a lot. They also grant low armor class and magic cancellation."); break;
			case DUMMY_WINGS:
				pline("A cloak with wings that aren't capable of flight. It grants low armor class and magic cancellation."); break;
			case FUR:
				pline("It's the fur of some wild animal that offers next to no protection."); break;
			case HIDE:
				pline("It's the hide of some wild animal that offers very little protection."); break;
			case DISPLACER_BEAST_HIDE:
				pline("A cloak that offers no magic cancellation but grants displacement to the wearer."); break;
			case THE_NEMEAN_LION_HIDE:
				pline("This might actually be an ascension kit quality cloak. Good armor class, 3 points of magic cancellation and stun resistance!"); break;
			case CLOAK_OF_SPRAY:
				pline("A fire-resistant cloak that unfortunately only offers medium magic cancellation."); break;
			case CLOAK_OF_FLAME:
				pline("A cold-resistant cloak that unfortunately only offers medium magic cancellation."); break;
			case CLOAK_OF_INSULATION:
				pline("A shock-resistant cloak that unfortunately only offers medium magic cancellation."); break;
			case CLOAK_OF_MATADOR:
				pline("It's an exact replica of the cloak of spray: MC2 and fire resistance."); break;

			case CLOAK_OF_DISPLACEMENT: 
				pline("Wearing this cloak grants displacement and medium magic cancellation."); break;
			case ELVEN_LEATHER_HELM:
				pline("A light helmet that grants good armor class."); break;
			case ELVEN_HELM: 
				pline("It's a standard elven helm that grants low armor class."); break;
			case HIGH_ELVEN_HELM: 
				pline("A mithril helm with good armor class."); break;
			case WAR_HAT: 
				pline("Good armor class and medium magic cancellation."); break;
			case FIRE_HELMET:
				pline("This helmet conveys fire resistance when worn."); break;
			case GNOMISH_HELM:
				pline("This headgear is a total waste of your time."); break;
			case ORCISH_HELM:
				pline("A basic helmet that gives a little bit of protection."); break;
			case DWARVISH_IRON_HELM:
				pline("A good helmet that offers some protection."); break;
			case DROVEN_HELM:
				pline("This helmet offers surprisingly good protection."); break;
			case FEDORA:
				pline("While it doesn't grant armor class, this headgear can increase your luck if worn."); break;
			case CORNUTHAUM:
				pline("Only wizards can use this headgear effectively."); break;
			case DUNCE_CAP:
				pline("This cap sets your intelligence to a low value but prevents it from changing, so you'll be protected from mind-eating attacks."); break;
			case DENTED_POT:
				pline("A relatively weak headgear."); break;
			case PLASTEEL_HELM:
				pline("Good protection and 9 points of magic cancellation, but this helmet prevents you from performing certain actions."); break;
			case HELMET:
				pline("A standard helmet that can be worn for protection."); break;
			case SEDGE_HAT:
				pline("The main use of this helm is to give you acid resistance."); break;
			case SKULLCAP:
				pline("Nothing special, just a helmet made of iron."); break;
			case NURSE_CAP:
				pline("It has a red cross painted on the front of it. Despite this, it doesn't do anything special."); break;
			case KATYUSHA:
				pline("Some kinda headgear with no special abilities."); break;
			case BUNNY_EAR:
				pline("You will look funny if you don this thing, which won't actually do anything."); break;
			case DRAGON_HORNED_HEADPIECE:
				pline("A plain dragonhide headgear."); break;
			case STRAW_HAT:
				pline("This hat isn't special in any way."); break;
			case SPEEDWAGON_S_HAT:
				pline("A plain headgear that, despite its name, does not make you super fast."); break;
			case MECHA_IRAZU:
				pline("Whatever the hell it is, it's made of plastic and covers your head."); break;
			case SCHOOL_CAP:
				pline("A basic cap."); break;
			case CROWN:
				pline("Headgear worn by kings and queens. It doesn't have special abilities though."); break;
			case ANTENNA:
				pline("Listening to the radio is not implemented in this game, otherwise that would be the effect of wearing this item."); break;
			case CHAIN_COIF:
				pline("Yet another unspectacular headgear."); break;
			case COLOR_CONE:
				pline("This hat doesn't do anything special other than looking like a dunce cap if unidentified."); break;
			case MINING_HELM:
				pline("A helmet with an integrated magic candle that increases your field of view."); break;
			case HELM_VERSUS_DEATH:
				pline("A helmet that protects you from death rays, touch of death and similar stuff."); break;
			case HELM_OF_FULL_NUTRITION:
				pline("This helmet provides full nutrients, which means that your nutrition decreases more slowly when you perform actions that reduce it."); break;
			case FIELD_HELM:
				pline("A moderately good iron helmet."); break;
			case HELM_OF_BEGINNER_S_LUCK:
				pline("A helmet that sets your luck to +13 as long as the game still considers you a beginner."); break;
			case HELM_OF_SAFEGUARD:
				pline("This helmet allows you to swim."); break;
			case HELM_OF_CHAOTIC:
				pline("Wanna convert to chaotic alignment? Then wear this thing!"); break;
			case HELM_OF_NEUTRAL:
				pline("You can become neutral if you aren't already by donning this helmet."); break;
			case HELM_OF_LAWFUL:
				pline("In order to abandon your previous alignment and start walking on the path of law, use this."); break;
			case HELM_OF_UNDERWATER_ACTION:
				pline("A helmet that grants magical breathing."); break;
			case HELM_OF_JAMMING:
				pline("This helmet generates a stealth field around you so to speak, by making it harder for monsters to sense your presence."); break;
			case HELM_OF_DECONTAMINATION:
				pline("A helmet that grants you contamination resistance, which reduces all incoming contamination by a factor 5."); break;
			case FLACK_HELMET:
				pline("Extra line of sight, good AC and low magic cancellation make this helmet worth using."); break;
			case CRYSTAL_HELM:
				pline("This glass helmet gives low magic cancellation but its main trait is the very good AC it grants."); break;
			case HELM_OF_OBSCURED_DISPLAY:
				pline("This helmet causes display loss. It has good AC and medium magic cancellation."); break;
			case HELM_OF_LOSE_IDENTIFICATION:
				pline("This helmet causes unidentification. It has moderately good AC and 5 points of magic cancellation."); break;
			case HELM_OF_THIRST:
				pline("This helmet causes thirst. It has mediocre AC and 3 points of magic cancellation."); break;
			case BLACKY_HELMET:
				pline("This helmet summons Blacky. It has great AC and 9 points of magic cancellation."); break;
			case ANTI_DRINKER_HELMET:
				pline("This helmet affects potions. It has good AC and low magic cancellation."); break;
			case WHISPERING_HELMET:
				pline("This helmet displays random rumors. It has low AC and low magic cancellation."); break;
			case CYPHER_HELM:
				pline("This helmet initiates a cipher. It has very good AC and 3 points of magic cancellation."); break;

			case MOMHAT:
				pline("This helmet insults your momma. It has almost no AC and no magic cancellation."); break;
			case CARTRIDGE_OF_HAVING_A_HORROR:
				pline("This helmet subjects you to bad status effects. It has quite good AC and 4 points of magic cancellation."); break;
			case BORDERLESS_HELMET:
				pline("This helmet makes the walls invisible. It has low AC and low magic cancellation."); break;
			case HELMET_OF_ANTI_SEARCHING:
				pline("This helmet prevents the search command from ever finding anything. It has low AC and low magic cancellation."); break;
			case HELM_OF_COUNTER_ROTATION:
				pline("This helmet fucks up your directional keys. It has great AC and 6 points of magic cancellation."); break;
			case DELIGHT_HELMET:
				pline("This helmet turns squares you've visited unlit. It has mediocre AC and no magic cancellation."); break;
			case MESSAGE_FILTER_HELMET:
				pline("This helmet can turn certain messages into random ones. It has medium AC and low magic cancellation."); break;
			case FLICKER_VISOR:
				pline("This helmet causes the bottom status line to display garbage strings. It has no AC and 3 points of magic cancellation."); break;
			case SCRIPTED_HELMET:
				pline("This helmet makes your inventory very fleecy. It has almost no AC and medium magic cancellation."); break;
			case EMPTY_LINE_HELMET:
				pline("This helmet prevents the top status line from displaying anything. It has great AC and 9 points of magic cancellation."); break;
			case GREEN_SPELL_HELMET:
				pline("This helmet causes green spells. It has quite good AC and 3 points of magic cancellation."); break;
			case INFOLESS_HELMET:
				pline("This helmet prevents you from displaying what character you are playing. It has low AC and no magic cancellation."); break;
			case BLUE_SPELL_HELMET:
				pline("This helmet causes blue spells. It has medium AC and 3 points of magic cancellation."); break;
			case MORE_HELMET:
				pline("This helmet disables the --More-- prompts. It has good AC and no magic cancellation."); break;

			case RARE_HELMET:
				pline("While wearing this helmet, monsters that would usually be uncommon by a certain frequency will instead be common, so they spawn more often. It has moderate AC and 3 points of magic cancellation."); break;
			case SOUND_EFFECT_HELMET:
				pline("A helmet that causes sound effects in written form. It has no AC and no magic cancellation."); break;
			case HELM_OF_BAD_ALIGNMENT:
				pline("This helmet causes alignment failures. It has good AC and 3 points of magic cancellation."); break;
			case SOUNDPROOF_HELMET:
				pline("This helmet causes deafness. It has mediocre AC and medium magic cancellation."); break;
			case ANGER_HELM:
				pline("This helmet causes angry monsters. It has moderately good AC and 3 points of magic cancellation."); break;
			case CAPTCHA_HELM:
				pline("This helmet causes captchas. It has no AC and no magic cancellation."); break;
			case OUT_OF_MEMORY_HELMET:
				pline("This helmet causes memory loss. It has great AC and 9 points of magic cancellation."); break;
			case HELM_OF_TRUE_SIGHT:
				pline("Headgear that allows you to see invisible things."); break;
			case HELM_OF_WARNING:
				pline("You will be alerted that monsters are coming while wearing this."); break;
			case HELM_OF_DETOXIFICATION:
				pline("Yay, a helmet that grants sickness resistance! Now you can freely eat all those tainted corpses for intrinsics!"); break;
			case HELM_OF_NO_DIGESTION:
				pline("Unlike the ring, this thing completely stops your food consumption. However, you won't be able to eat while wearing it, and it gets a terrible ancient morgothian curse, err, prime curse whenever you put it on!"); break;
			case TINFOIL_HELMET:
				pline("This helmet prevents both confusion and telepathy. Decide for yourself if you like this combo."); break;
			case PARANOIA_HELMET:
				pline("A helmet that gives psi resistance, which is otherwise very hard to get."); break;
			case BOOGEYMAN_HELMET:
				pline("Usually generated cursed, this headgear aggravates monsters. However, it also has good AC and medium magic cancellation, which might still make it worth wearing."); break;
			case HELM_OF_BRILLIANCE:
				pline("This helmet can be worn to increase your intelligence."); break;
			case HELM_OF_OPPOSITE_ALIGNMENT:
				pline("If you put on this helmet, your alignment will be changed and you lose all divine protection that you might have."); break;
			case HELM_OF_STEEL:
				pline("A robust helmet that offers good armor class."); break;
			case HELM_OF_SPEED:
				pline("This very useful helmet makes you faster."); break;
			case HELMET_OF_UNDEAD_WARNING:
				pline("A helmet that offers good armor class and magic cancellation in addition to displaying the locations of all the undead monsters on the level."); break;
			case HELM_OF_DRAIN_RESISTANCE:
				pline("You can get resistance to level drain by putting on this helm."); break;
			case HELM_OF_FEAR:
				pline("A helmet that grants good AC and magic cancellation, but you also get the 'fear' status effect while wearing it, causing you to miss a lot more often. It is usually generated cursed."); break;
			case HELM_OF_HUNGER:
				pline("A helmet that grants good AC and magic cancellation, but you also get the 'hunger' status effect while wearing it, causing you to burn nutrition at a faster rate. It is usually generated cursed."); break;
			case HELM_OF_STORMS:
#ifdef PHANTOM_CRASH_BUG
				pline("This grants resist fire, cold and lightning as well as detect monsters. But the helm covers your entire face so you cannot eat, quaff, levelport or control teleports and monsters respawn faster. Autocurses."); break;
#else
				pline("The very powerful Helm of Storms grants its wearer control over the elements, which is to say, resistance to fire, cold and lightning. It also allows you to detect monsters until the helm is removed, but you can't eat, quaff potions, levelport, or control your teleports while wearing it. Monsters also respawn much faster for as long as you wear it. This helm autocurses if you put it on."); break;
#endif
			case HELM_OF_DETECT_MONSTERS:
				pline("When worn, this helm grants you the ability to detect monsters until removed. It also prevents you from eating or quaffing potions, and this helm autocurses every time it is put on."); break;
			case HELM_OF_DISCOVERY:
				pline("This helmet grants automatic searching if you wear it."); break;
			case HELM_OF_AMNESIA:
				pline("This helm causes amnesia. It provides very good AC and 3 points of magic cancellation."); break;
			case HELM_OF_TELEPORTATION:
				pline("Put it on to get teleportitis. Usually generated cursed."); break;
			case HELM_OF_TELEPORT_CONTROL:
				pline("Control your teleports by wearing this helm."); break;
			case BIGSCRIPT_HELM:
				pline("This helm causes BIGscript. It provides low AC and no magic cancellation."); break;
			case QUIZ_HELM:
				pline("This helm causes quizzes. It provides medium AC and no magic cancellation."); break;
			case DIZZY_HELMET:
				pline("This helm causes map bugs. It provides moderately good AC and medium magic cancellation."); break;
			case MUTING_HELM:
				pline("This helm causes muteness. It provides excellent AC and 6 points of magic cancellation."); break;
			case ULCH_HELMET:
				pline("This helm causes rotten food. It provides medium AC and 3 points of magic cancellation."); break;
			case DIMMER_HELMET:
				pline("This helm causes weak sight. It provides good AC and medium magic cancellation."); break;
			case HELM_OF_STARVATION:
				pline("This helm might cause you to starve eventually. It provides good AC and medium magic cancellation."); break;
			case QUAFFER_HELMET:
				pline("This helm causes dehydration. It provides relatively good AC and medium magic cancellation."); break;
			case INCORRECTLY_ADJUSTED_HELMET:
				pline("This helm prevents you from obtaining intrinsics via eating. It provides low AC and low magic cancellation."); break;
			case HELM_OF_SENSORY_DEPRIVATION:
				pline("This helm causes blindness, hallucination and confusion and is usually generated cursed. However, it also provides extremely good AC and medium magic cancellation."); break;
			case HELM_OF_TELEPATHY:
				pline("Wearing this helmet conveys 'weak' telepathy that displays monsters close by, and 'good' telepathy that displays all monsters on the level if you are blind."); break;
			case PLASTEEL_GLOVES:
				pline("This pair of gloves offers good protection."); break;
			case GAUNTLETS_OF_THE_FORCE:
				pline("This pair of gloves offers good protection and enhances your ability to use the force."); break;
			case LEATHER_GLOVES:
				pline("A standard pair of gloves that offers little protection."); break;
			case ORIHALCYON_GAUNTLETS:
				pline("It doesn't matter whether you're able to spell the name of this item. What does matter is that wearing it gives motherfucking magic resistance!!!!!111oneoneone"); break;
			case GAUNTLETS_OF_FUMBLING:
				pline("You will fumble if you put on this pair of gloves. They are usually generated cursed."); break;
			case GAUNTLETS_OF_SLOWING:
				pline("A pair of gloves that slows your movement speed when worn. They are usually generated cursed."); break;
			case GAUNTLETS_OF_PANIC:
				pline("A pair of gloves that makes you fearful while wearing them. It grants good protection and medium magic cancellation. They are usually generated cursed."); break;
			case OILSKIN_GLOVES:
				pline("This pair of gloves will cause you to drop your weapon, and you'll be unable to re-equip it. They provide some AC and 3 points of magic cancellation, but these gloves autocurse if you put them on."); break;

			case GAUNTLETS_OF_SAFEGUARD:
				pline("A pair of gauntlets that allows you to swim in water."); break;
			case GAUNTLETS_OF_PLUGSUIT:
				pline("Wearing this pair of gloves does nothing special."); break;
			case COMMANDER_GLOVES:
				pline("They sure look good but unfortunately these gloves are actually rather plain."); break;
			case FIELD_GLOVES:
				pline("A bog-standard pair of gloves."); break;
			case GAUNTLETS:
				pline("A standard pair of gauntlets."); break;
			case SILVER_GAUNTLETS:
				pline("These gauntlets may or may not be made of silver."); break;
			case GAUNTLETS_OF_FAST_CASTING:
				pline("A pair of gauntlets that increases the rate of mana regeneration."); break;
			case GAUNTLETS_OF_NO_FLICTION:
				pline("Yay! It's a pair of very smooth gauntlets, and they don't autocurse either! But wait until you try to take them off..."); break;

			case MENU_NOSE_GLOVES:
				pline("This pair of gloves causes menu bugs. They provide moderately good AC and medium magic cancellation."); break;
			case UNWIELDY_GLOVES:
				pline("This pair of gloves causes the free hand to be full. They provide good AC and 7 points of magic cancellation."); break;
			case CONFUSING_GLOVES:
				pline("This pair of gloves causes confusing problems. They provide extremely good AC."); break;
			case UNDROPPABLE_GLOVES:
				pline("This pair of gloves causes drop bugs. They provide moderately good AC and 4 points of magic cancellation."); break;
			case GAUNTLETS_OF_MISSING_INFORMATI:
				pline("This pair of gloves causes a lack of feedback. They provide good AC and 6 points of magic cancellation."); break;
			case GAUNTLETS_OF_TRAP_CREATION:
				pline("This pair of gloves causes traps to be generated. They provide good AC and medium magic cancellation."); break;
			case SADO_MASO_GLOVES:
				pline("This pair of gloves causes fifty shades of grey. They provide low AC and low magic cancellation."); break;
			case BANKING_GLOVES:
				pline("This pair of gloves causes money to be put in a bank. They provide mediocre AC and 7 points of magic cancellation."); break;
			case DIFFICULT_GLOVES:
				pline("This pair of gloves causes techniques to fail. They provide good AC and 4 points of magic cancellation."); break;
			case CHAOS_GLOVES:
				pline("This pair of gloves causes chaos terrain. They provide low AC and medium magic cancellation."); break;
			case LEVELING_GLOVES:
				pline("This pair of gloves causes high-level monsters to be more common. They provide medium AC and low magic cancellation."); break;
			case GAUNTLETS_OF_STEALING:
				pline("This pair of gloves causes your items to get stolen more often. They provide low AC and no magic cancellation."); break;
			case GAUNTLETS_OF_MISFIRING:
				pline("This pair of gloves causes your projectiles to misfire. They provide mediocre AC and medium magic cancellation."); break;

			case SCALER_MITTENS:
				pline("This pair of gloves activates a minimum level for newly spawned monsters. They provide excellent AC and 4 points of magic cancellation."); break;
			case GLOVES_OF_ENERGY_DRAINING:
				pline("This pair of gloves causes your wands and tools to lose more charges per use. They provide medium AC and medium magic cancellation."); break;
			case MARY_SUE_GLOVES:
				pline("This pair of gloves can make you break potions when trying to pick them up. They provide medium AC and 3 points of magic cancellation."); break;
			case GAUNTLETS_OF_BAD_CASTING:
				pline("This pair of gloves causes your spells to backlash constantly. They provide good AC and 3 points of magic cancellation."); break;
			case METER_GAUNTLETS:
				pline("This pair of gloves deactivates the showdamage patch. They provide almost no AC and no magic cancellation."); break;
			case WEIGHTING_GAUNTLETS:
				pline("This pair of gloves deactivates the showweight and invweight patches. They provide almost no AC and no magic cancellation."); break;
			case BLACK_SPELL_GAUNTLETS:
				pline("This pair of gloves causes black spells. They provide good AC and 4 points of magic cancellation."); break;
			case HEAVY_GRABBING_GLOVES:
				pline("This pair of gloves burdens you whenever you pick up an item. They provide quite good AC and 3 points of magic cancellation."); break;
			case GAUNTLETS_OF_REVERSE_ENCHANTME:
				pline("This pair of gloves can turn your positively enchanted items into negatively enchanted ones. They provide good AC and no magic cancellation."); break;
			case FUCKUP_MELEE_GAUNTLETS:
				pline("This pair of gloves fucks up your melee attacks, unless you prefix them. They provide very good AC and no magic cancellation."); break;

			case GAUNTLETS_OF_POWER:
				pline("A powerful pair of gauntlets that increases the wearer's strength."); break;
			case GAUNTLETS_OF_REFLECTION:
				pline("Wear this pair of gloves, and you'll be able to reflect beams and certain other attacks!"); break;
			case GAUNTLETS_OF_TYPING:
				pline("These gauntlets are nothing special, but they offer some points of armor class."); break;
			case GAUNTLETS_OF_STEEL:
				pline("A pair of gloves made of metal. They offer good protection."); break;
			case GAUNTLETS_OF_SWIMMING:
				pline("Magic gloves that allow the wearer to swim."); break;
			case GAUNTLETS_OF_FREE_ACTION:
				pline("You want paralysis resistance, right? Well, you just found a way to get it!"); break;
			case ELVEN_GAUNTLETS:
				pline("Light gauntlets that provide stealth."); break;
			case GAUNTLETS_OF_GOOD_FORTUNE:
				pline("This pair of gloves acts as a luckstone when worn."); break;
			case GAUNTLETS_OF_DEXTERITY:
				pline("Depending on their enchantment, these gloves can increase or decrease your dexterity if you wear them."); break;
			case GAUNTLETS_OF_LEECH:
				pline("Wear this pair of gauntlets and you will recover some mana for every creature you kill!"); break;
			case SMALL_SHIELD:
				pline("A wooden shield that offers a little protection."); break;
			case ELVEN_SHIELD:
				pline("This shield offers some protection from enemy attacks."); break;
			case URUK_HAI_SHIELD:
				pline("A good shield that offers solid armor class."); break;
			case ORCISH_SHIELD:
				pline("A good shield that offers solid armor class."); break;
			case STEEL_SHIELD:
				pline("This metal shield can deflect lots of attacks."); break;
			case CRYSTAL_SHIELD:
				pline("This glass shield can deflect lots of attacks and also gives MC1."); break;
			case LARGE_SHIELD:
				pline("A shield made of iron with a very good armor value."); break;
			case DWARVISH_ROUNDSHIELD:
				pline("This shield offers very good protection."); break;
			case FLAME_SHIELD:
				pline("A heat-resistant shield that offers fire resistance to the wearer."); break;
			case ORCISH_GUARD_SHIELD:
				pline("Similar to a standard orcish shield, this offers good armor class."); break;
			case SHIELD:
				pline("A big shield that can deflect attacks."); break;
			case SILVER_SHIELD:
				pline("This shield is made of silver and offers good protection."); break;
			case MIRROR_SHIELD:
				pline("A glass shield that reflects beams at who- or whatever shot them."); break;
			case RAPIRAPI:
				pline("This is a good shield made of mineral."); break;
			case PAPER_SHIELD:
				pline("Someone in the chat suggested this shield (if it's you, feel free to remember me so I can give credits). It gives no armor class at all but a high chance to block projectiles."); break;
			case ICE_SHIELD:
				pline("A cold-resistant shield that offers cold resistance to the wearer."); break;
			case LIGHTNING_SHIELD:
				pline("A shock-resistant shield that offers shock resistance to the wearer."); break;
			case VENOM_SHIELD:
				pline("A poison-resistant shield that offers poison resistance to the wearer."); break;
			case SHIELD_OF_LIGHT:
				pline("This shield conveys infravision when worn."); break;
			case SHIELD_OF_MOBILITY:
				pline("A useful shield that prevents the wearer from being paralyzed."); break;
			case SHIELD_OF_REFLECTION:
				pline("One of the most powerful shields in the game. This reflexive shield protects the wearer from rays, gaze attacks and similar crap while also providing excellent AC."); break;
			case GRAY_DRAGON_SCALE_SHIELD:
				pline("A shield made from dragon hide that provides magic resistance as well as protection."); break;
			case SILVER_DRAGON_SCALE_SHIELD:
				pline("A shield made from dragon hide that provides reflection as well as protection."); break;
			case MERCURIAL_DRAGON_SCALE_SHIELD:
				pline("A shield made from dragon hide that provides extra speed as well as protection."); break;
			case SHIMMERING_DRAGON_SCALE_SHIELD:
				pline("A shield made from dragon hide that provides displacement as well as protection."); break;
			case DEEP_DRAGON_SCALE_SHIELD:
				pline("A shield made from dragon hide that provides drain resistance as well as protection."); break;
			case RED_DRAGON_SCALE_SHIELD:
				pline("A shield made from dragon hide that provides fire resistance as well as protection."); break;
			case WHITE_DRAGON_SCALE_SHIELD:
				pline("A shield made from dragon hide that provides cold resistance as well as protection."); break;
			case ORANGE_DRAGON_SCALE_SHIELD:
				pline("A shield made from dragon hide that provides sleep resistance as well as protection."); break;
			case BLACK_DRAGON_SCALE_SHIELD:
				pline("A shield made from dragon hide that provides disintegration resistance as well as protection."); break;
			case BLUE_DRAGON_SCALE_SHIELD:
				pline("A shield made from dragon hide that provides shock resistance as well as protection."); break;
			case COPPER_DRAGON_SCALE_SHIELD: 
				pline("A shield made from dragon hide that provides %s as well as protection.", enchname(objects[COPPER_DRAGON_SCALE_SHIELD].oc_oprop) ); break;
			case PLATINUM_DRAGON_SCALE_SHIELD: 
				pline("A shield made from dragon hide that provides %s as well as protection.", enchname(objects[PLATINUM_DRAGON_SCALE_SHIELD].oc_oprop) ); break;
			case BRASS_DRAGON_SCALE_SHIELD: 
				pline("A shield made from dragon hide that provides %s as well as protection.", enchname(objects[BRASS_DRAGON_SCALE_SHIELD].oc_oprop) ); break;
			case AMETHYST_DRAGON_SCALE_SHIELD: 
				pline("A shield made from dragon hide that provides %s as well as protection.", enchname(objects[AMETHYST_DRAGON_SCALE_SHIELD].oc_oprop) ); break;
			case PURPLE_DRAGON_SCALE_SHIELD: 
				pline("A shield made from dragon hide that provides %s as well as protection.", enchname(objects[PURPLE_DRAGON_SCALE_SHIELD].oc_oprop) ); break;
			case DIAMOND_DRAGON_SCALE_SHIELD: 
				pline("A shield made from dragon hide that provides %s as well as protection.", enchname(objects[DIAMOND_DRAGON_SCALE_SHIELD].oc_oprop) ); break;
			case EMERALD_DRAGON_SCALE_SHIELD: 
				pline("A shield made from dragon hide that provides %s as well as protection.", enchname(objects[EMERALD_DRAGON_SCALE_SHIELD].oc_oprop) ); break;
			case SAPPHIRE_DRAGON_SCALE_SHIELD: 
				pline("A shield made from dragon hide that provides %s as well as protection.", enchname(objects[SAPPHIRE_DRAGON_SCALE_SHIELD].oc_oprop) ); break;
			case RUBY_DRAGON_SCALE_SHIELD: 
				pline("A shield made from dragon hide that provides %s as well as protection.", enchname(objects[RUBY_DRAGON_SCALE_SHIELD].oc_oprop) ); break;
			case GREEN_DRAGON_SCALE_SHIELD:
				pline("A shield made from dragon hide that provides poison resistance as well as protection."); break;
			case GOLDEN_DRAGON_SCALE_SHIELD:
				pline("A shield made from dragon hide that provides sickness resistance as well as protection."); break;
			case STONE_DRAGON_SCALE_SHIELD:
				pline("A shield made from dragon hide that provides petrification resistance as well as protection."); break;
			case CYAN_DRAGON_SCALE_SHIELD:
				pline("A shield made from dragon hide that provides fear resistance as well as protection."); break;
			case PSYCHIC_DRAGON_SCALE_SHIELD:
				pline("A shield made from dragon hide that provides ESP as well as protection."); break;
			case YELLOW_DRAGON_SCALE_SHIELD:
				pline("A shield made from dragon hide that provides acid resistance as well as protection."); break;
			case RAINBOW_DRAGON_SCALE_SHIELD:
				pline("A shield made from dragon hide that provides spell damage resistance as well as protection."); break;
			case BLOOD_DRAGON_SCALE_SHIELD:
				pline("A shield made from dragon hide that provides normal damage resistance as well as protection."); break;
			case PLAIN_DRAGON_SCALE_SHIELD:
				pline("A shield made from dragon hide that provides a ton of protection."); break;
			case SKY_DRAGON_SCALE_SHIELD:
				pline("A shield made from dragon hide that provides flying as well as protection."); break;
			case WATER_DRAGON_SCALE_SHIELD:
				pline("A shield made from dragon hide that provides swimming as well as protection."); break;

			case PLASTEEL_BOOTS:
				pline("A pair of boots that offers 9 points of magic cancellation. This is a heck of a lot for an item that has no downsides, mind you."); break;
			case LOW_BOOTS:
				pline("This basic pair of boots offers minimal protection from enemy attacks."); break;
			case GNOMISH_BOOTS:
				pline("Don't bother wearing these things. Find a better pair of footwear instead."); break;
			case HIGH_BOOTS:
				pline("These boots offer moderate protection when worn."); break;
			case IRON_SHOES:
				pline("Dwarves like to wear these, but they aren't actually dwarven. They provide relatively good AC."); break;
			case CRYSTAL_BOOTS:
				pline("A pair of boots that offers high armor class."); break;
			case WEDGE_SANDALS:
				pline("A lovely pair of high-heeled women's sandals that provides no protection but looks pretty."); break;
			case DANCING_SHOES:
				pline("This pair of soft footwear has profiled soles and looks incredibly lovely. Your feet will probably like being enclosed by them."); break;
			case SWEET_MOCASSINS:
				pline("A pair of sexy beauties made of leather. They look comfortable."); break;
			case SOFT_SNEAKERS:
				pline("A valuable pair of lightweight leather sneakers that seems very comfortable."); break;
			case FEMININE_PUMPS:
				pline("These high heels look incredibly lovely and tender with their cone heels. You will surely look great if you wear them."); break;
			case LEATHER_PEEP_TOES:
				pline("A pair of asian footwear with plateau soles and stiletto heels. They are made of beautifully soft black leather."); break;

			case AUTODESTRUCT_DE_VICE_BOOTS:
				pline("This footwear causes auto-destruct mechanisms to be initiated. They provide good AC and medium magic cancellation."); break;
			case SPEEDBUG_BOOTS:
				pline("This footwear causes the speed bug. They provide good AC and low magic cancellation."); break;
			case DISCONNECTED_BOOTS:
				pline("This footwear causes disconnected staircases. They provide low AC and medium magic cancellation."); break;

			case COMPETITION_BOOTS:
				pline("This footwear speeds up monsters. They provide medium AC and medium magic cancellation."); break;
			case QUASIMODULAR_BOOTS:
				pline("This footwear causes you to see only a few tiles on the playing field. They provide incredibly great AC and 7 points of magic cancellation."); break;
			case SINFUL_HEELS:
				pline("This footwear is block-heeled and makes it unsafe to pray. They provide very good AC and 3 points of magic cancellation."); break;
			case BLOODSUCKING_SHOES:
				pline("This footwear causes you to take double damage. They provide good AC and 9 points of magic cancellation."); break;
			case COVETED_BOOTS:
				pline("This footwear is block-heeled and improves the AI of covetous monsters. They provide medium AC and 4 points of magic cancellation."); break;
			case LIGHTLESS_BOOTS:
				pline("This footwear prevents you from seeing lit squares if them being lit was the only reason you saw them. They provide mediocre AC and medium magic cancellation."); break;
			case KILLER_HEELS:
				pline("This footwear is high-heeled (stilettos that can kill both the wearer and the one that's being kicked by them :D just kidding!) and allows monsters to create more traps. They provide very good AC and 7 points of magic cancellation."); break;
			case CHECKER_BOOTS:
				pline("This footwear causes checkerboard vision. They provide excellent AC and 3 points of magic cancellation."); break;
			case ELVIS_SHOES:
				pline("This footwear makes the walls dangerous. They provide low AC and medium magic cancellation."); break;
			case AIRSTEP_BOOTS:
				pline("This footwear disables the paranoid patch, so you'd better be careful around water and lava. They provide great AC and no magic cancellation."); break;
			case BOOTS_OF_INTERRUPTION:
				pline("This footwear causes consumables to take multiple turns to use. They provide good AC and no magic cancellation."); break;
			case HIGH_HEELED_SKIERS:
				pline("This footwear is high-heeled (the shape is remotely similar to a wedge heel) and causes artifical latency. They provide quite good AC and 6 points of magic cancellation."); break;
			case HIGH_SCORING_HEELS:
				pline("This footwear is block-heeled and causes the highscore effect from SPACE WARS that the Amy her roommate invented. They provide excellent AC and 8 points of magic cancellation."); break;
			case REPEATABLE_BOOTS:
				pline("This footwear repeats messages. They provide almost no AC and medium magic cancellation."); break;
			case TRON_BOOTS:
				pline("This footwear prevents you from moving into the same direction twice in a row. They provide quite good AC and 3 points of magic cancellation."); break;
			case RED_SPELL_HEELS:
				pline("This footwear is high-heeled (stilettos, in fact) and causes red spells. They provide excellent AC and 5 points of magic cancellation."); break;
			case DESTRUCTIVE_HEELS:
				pline("This footwear is high-heeled and causes random destruction of your inventory items because the cone heels absolutely want to destroy your stuff. They provide good AC and 7 points of magic cancellation."); break;

			case BOSS_BOOTS:
				pline("This footwear causes boss monsters to spawn more often. They provide low AC and low magic cancellation."); break;
			case SENTIENT_HIGH_HEELED_SHOES:
				pline("This high-heeled footwear randomly tries to hurt the wearer with their stilettos. They provide very good AC and 8 points of magic cancellation."); break;
			case BOOTS_OF_FAINTING:
				pline("This footwear causes fainting. They provide very good AC."); break;
			case DIFFICULT_BOOTS:
				pline("This footwear causes increased difficulty. They provide mediocre AC and medium magic cancellation."); break;
			case BOOTS_OF_WEAKNESS:
				pline("This footwear causes weakness. They provide good AC and medium magic cancellation."); break;
			case UGG_BOOTS:
				pline("According to jonadab these things are ugly, and therefore wearing them will reduce your charisma."); break;
			case BOOTS_OF_FREEDOM:
				pline("A pair of very comfortable boots that cause attempts to paralyze you to fail."); break;
			case BOOTS_OF_TOTAL_STABILITY:
				pline("These lovely boots are a possible way to become disintegration resistant."); break;
			case BOOTS_OF_DISPLACEMENT:
				pline("Enemies will sometimes see you in a different location while you're wearing these."); break;
			case BOOTS_OF_SWIMMING:
				pline("A pair of boots with fins, allowing you to swim in water without sinking like a rock."); break;
			case ANTI_CURSE_BOOTS:
				pline("This pair of boots can be very useful, since it mitigates the effects of the 'curse items' spell and also some other effects that can curse your stuff."); break;
			case GRIDBUG_CONDUCT_BOOTS:
				pline("This footwear forces its wearer to adhere to the grid bug conduct. They provide extremely good AC and 7 points of magic cancellation."); break;
			case DISENCHANTING_BOOTS:
				pline("This footwear causes disenchantment. They provide extremely good AC and 8 points of magic cancellation."); break;
			case LIMITATION_BOOTS:
				pline("This footwear causes your ascension turn limitation to decrease. They provide very good AC and 3 points of magic cancellation."); break;
			case STAIRWELL_STOMPING_BOOTS:
				pline("This footwear causes stairwells to be trapped. They provide very good AC and 6 points of magic cancellation."); break;
			case PET_STOMPING_PLATFORM_BOOTS:
				pline("This footwear causes cats and dogs to hate you, but they're high-heeled so you can kick the vermin to death. In fact, they have kind of a wedge heel. They provide mediocre AC and medium magic cancellation."); break;
			case ASS_KICKER_BOOTS:
				pline("This footwear causes pets to spontaneously rebel. They provide low AC and low magic cancellation."); break;
			case DEMENTIA_BOOTS:
				pline("This footwear causes the dungeon to regrow rapidly. They provide good AC and no magic cancellation."); break;

			case HIPPIE_HEELS:
				pline("This pair of red leather plateau boots looks extraordinarily sexy. You get the feeling that they would love to be worn by you. Can you resist the temptation to put on these block-heeled beauties? :-)"); break;
			case COMBAT_STILETTOS:
				pline("This is a pair of high-heeled combat boots. Probably meant to be used by a kung-fu ninja woman or something like that."); break;
			case SPEED_BOOTS:
				pline("This piece of footwear makes its wearer speed up."); break;
			case BOOTS_OF_MOLASSES:
				pline("Don't wear these unless you want to move at half speed. They are usually generated cursed."); break;
			case WATER_WALKING_BOOTS:
				pline("If you want to be like Jesus and walk on water, wear this pair of boots. They also allow you to walk on lava, but keep in mind they will be touching it if you do."); break;
			case JUMPING_BOOTS:
				pline("Wearing this pair of boots allows you to jump around."); break;
			case FLYING_BOOTS:
				pline("A funny pair of boots with wings that allows the wearer to fly like an eagle."); break;
			case BOOTS_OF_ACID_RESISTANCE:
				pline("Wearing these boots grats the otherwise hard-to-get acid resistance property, but unfortunately it won't protect your equipment."); break;
			case ELVEN_BOOTS:
				pline("Wearers of this pair of boots can walk very quietly."); break;
			case KICKING_BOOTS:
				pline("If you want to be a kung-fu fighter, wear these boots to power up your kicks."); break;
			case ATSUZOKO_BOOTS:
				pline("Don't put these boots on unless you want to fumble around. That said, at least they aren't usually generated cursed. And they're incredibly high-heeled stilettos too."); break;
			case RUBBER_BOOTS:
				pline("A normal pair of boots made of plastic."); break;
			case LEATHER_SHOES:
				pline("Just some plain old slippers."); break;
			case SNEAKERS:
				pline("These shoes are comfortable to wear."); break;
			case MULTI_SHOES:
#ifdef PHANTOM_CRASH_BUG
				pline("They will autocurse when worn and provide all of the following properties: waterwalking, speed, jumping, stealth, hallucination, wounded legs, levitation and fumbling."); break;
#else
				pline("Oh boy, better think twice before putting these on... They will autocurse when worn and provide all of the following properties: waterwalking, speed, jumping, stealth, hallucination, wounded legs, levitation and fumbling. Think carefully whether the benefits outweigh the downsides."); break;
#endif
			case BOOTS_OF_PLUGSUIT:
				pline("A rather mundane pair of boots."); break;
			case ROLLER_BLADE:
				pline("These shoes will make you very fast thanks to their wheels on the soles, but you will also fumble while wearing them."); break;
			case FIELD_BOOTS:
				pline("A pair of boots worth some AC."); break;
			case BOOTS_OF_SAFEGUARD:
				pline("It doesn't really make sense, but with these boots on, you will be able to swim."); break;
			case STOMPING_BOOTS:
				pline("A very powerful pair of boots that not only allows you to run very fast but also improves your ability to kick. Unfortunately they're loud and will probably cause monsters to be alerted to your position."); break;
			case CARRYING_BOOTS:
				pline("This pair of boots can carry you through the dungeon... well, not really, but it lights up dark areas for easier scouting."); break;
			case FREEZING_BOOTS:
				pline("A really thick pair of boots that allows you to withstand temperatures as low as 200 degrees Kelvin."); break;
			case FUMBLE_BOOTS:
				pline("Wear this pair of boots if you want to fumble, which probably won't ever be the case. They are usually generated cursed."); break;
			case FIRE_BOOTS:
				pline("A pair of boots that grants great AC and magic cancellation but also burns you when worn. They are usually generated cursed."); break;
			case ZIPPER_BOOTS:
				pline("By watching these boots closely, you notice their zippers are trying to touch and damage your skin. They're sharp-edged too, so be careful."); break;
			case LEVITATION_BOOTS:
#ifdef PHANTOM_CRASH_BUG
				pline("You will float into the air if you wear this pair of boots. These are usually generated cursed and prevent you from picking up items or using a set of downstairs."); break;
#else
				pline("You will float into the air if you wear this pair of boots. Unlike Castle of the Winds, this is NOT a good thing as these are usually generated cursed and prevent you from picking up items or using a set of downstairs."); break;
#endif

			case RANDOMIZED_HELMET: 
				pline("The RNG created this helmet; it grants %s, has %d points of AC and provides a MC of %d.", enchname(objects[RANDOMIZED_HELMET].oc_oprop), objects[RANDOMIZED_HELMET].a_ac, objects[RANDOMIZED_HELMET].a_can ); break;
			case HIGH_STILETTOS: 
				pline("This pair of shoes is very high-heeled. Wearing it gives you %s, and also %d points of AC and %d points of MC!", enchname(objects[HIGH_STILETTOS].oc_oprop), objects[HIGH_STILETTOS].a_ac, objects[HIGH_STILETTOS].a_can ); break;
			case UNKNOWN_GAUNTLETS: 
				pline("The random enchantment of this gloves is %s today. AC is %d, and MC is %d.", enchname(objects[UNKNOWN_GAUNTLETS].oc_oprop), objects[UNKNOWN_GAUNTLETS].a_ac, objects[UNKNOWN_GAUNTLETS].a_can ); break;
			case MISSING_CLOAK: 
				pline("A randomly generated cloak. Its main property is %s, but it gives armor class of %d and magic cancellation of %d as well.", enchname(objects[MISSING_CLOAK].oc_oprop), objects[MISSING_CLOAK].a_ac, objects[MISSING_CLOAK].a_can ); break;

			case NASTY_CLOAK:
				pline("It's a female cloak that is labeled 'Arabella's Nasty Clothing Facilities, Inc.' Apparently it gives maximum magic cancellation and good armor class..."); break;
			case UNWANTED_HELMET:
				pline("Do you really want this helmet? The great armor class and magic cancellation values suggest that it's a trap of some kind!"); break;
			case EVIL_GLOVES:
				pline("Evil. Plain and simple. With unheard of values for armor class and magic cancellation, these things are trying to lure you into their cold, possessive clutches."); break;
			case UNFAIR_STILETTOS:
				pline("You see shreds of human skin clinging from the heels of this pair of leather boots, and their zippers are blood-smeared. But on the other hand they offer great AC and magic cancellation..."); break;

			case SPECIAL_CLOAK: 
				pline("This cloak is special in some way. Stats: %d points of AC and a MC of %d.", objects[SPECIAL_CLOAK].a_ac, objects[SPECIAL_CLOAK].a_can ); break;
			case WONDER_HELMET: 
				pline("A helmet that usually has some kind of special effect. Stats: %d points of AC and a MC of %d.", objects[WONDER_HELMET].a_ac, objects[WONDER_HELMET].a_can ); break;
			case ARCANE_GAUNTLETS: 
				pline("These gauntlets have mysterious properties! Stats: %d points of AC and a MC of %d.", objects[ARCANE_GAUNTLETS].a_ac, objects[ARCANE_GAUNTLETS].a_can ); break;
			case SKY_HIGH_HEELS: 
				pline("There is something special about this pair of female footwear, apart from the fact that they're very high-heeled stilettos. Stats: %d points of AC and a MC of %d.", objects[SKY_HIGH_HEELS].a_ac, objects[SKY_HIGH_HEELS].a_can ); break;
			case PLAIN_CLOAK: 
				pline("A rather boring cloak. Stats: %d points of AC and a MC of %d.", objects[PLAIN_CLOAK].a_ac, objects[PLAIN_CLOAK].a_can ); break;
			case POINTED_HELMET: 
				pline("This helmet is nothing special. Stats: %d points of AC and a MC of %d.", objects[POINTED_HELMET].a_ac, objects[POINTED_HELMET].a_can ); break;
			case PLACEHOLDER_GLOVES: 
				pline("A pair of gloves without magical properties. Stats: %d points of AC and a MC of %d.", objects[PLACEHOLDER_GLOVES].a_ac, objects[PLACEHOLDER_GLOVES].a_can ); break;
			case PREHISTORIC_BOOTS: 
				pline("The neanderthals played soccer with these. Stats: %d points of AC and a MC of %d.", objects[PREHISTORIC_BOOTS].a_ac, objects[PREHISTORIC_BOOTS].a_can ); break;
			case ARCHAIC_CLOAK: 
				pline("It was created at a time when magical properties of armor pieces didn't yet exist. Stats: %d points of AC and a MC of %d.", objects[ARCHAIC_CLOAK].a_ac, objects[ARCHAIC_CLOAK].a_can ); break;
			case BOG_STANDARD_HELMET: 
				pline("The name says it all. Stats: %d points of AC and a MC of %d.", objects[BOG_STANDARD_HELMET].a_ac, objects[BOG_STANDARD_HELMET].a_can ); break;
			case PROTECTIVE_GLOVES: 
				pline("A rather unspectacular pair of gloves. Stats: %d points of AC and a MC of %d.", objects[PROTECTIVE_GLOVES].a_ac, objects[PROTECTIVE_GLOVES].a_can ); break;
			case SYNTHETIC_SANDALS: 
				pline("Lovely female sandals that unfortunately aren't high-heeled. But they're sweet! Stats: %d points of AC and a MC of %d.", objects[SYNTHETIC_SANDALS].a_ac, objects[SYNTHETIC_SANDALS].a_can ); break;

			case DUMMY_CLOAK_A: 
				pline("This cloak is not randomly generated and only appears under certain rare conditions, but it has properties anyway just in case one does generate (as seen here, since otherwise you wouldn't be reading this). Its main property is %s, but it gives armor class of %d and magic cancellation of %d as well.", enchname(objects[DUMMY_CLOAK_A].oc_oprop), objects[DUMMY_CLOAK_A].a_ac, objects[DUMMY_CLOAK_A].a_can ); break;
			case DUMMY_CLOAK_B: 
				pline("This cloak is not randomly generated and only appears under certain rare conditions, but it has properties anyway just in case one does generate (as seen here, since otherwise you wouldn't be reading this). Its main property is %s, but it gives armor class of %d and magic cancellation of %d as well.", enchname(objects[DUMMY_CLOAK_B].oc_oprop), objects[DUMMY_CLOAK_B].a_ac, objects[DUMMY_CLOAK_B].a_can ); break;
			case DUMMY_CLOAK_C: 
				pline("This cloak is not randomly generated and only appears under certain rare conditions, but it has properties anyway just in case one does generate (as seen here, since otherwise you wouldn't be reading this). Its main property is %s, but it gives armor class of %d and magic cancellation of %d as well.", enchname(objects[DUMMY_CLOAK_C].oc_oprop), objects[DUMMY_CLOAK_C].a_ac, objects[DUMMY_CLOAK_C].a_can ); break;
			case DUMMY_CLOAK_D: 
				pline("This cloak is not randomly generated and only appears under certain rare conditions, but it has properties anyway just in case one does generate (as seen here, since otherwise you wouldn't be reading this). Its main property is %s, but it gives armor class of %d and magic cancellation of %d as well.", enchname(objects[DUMMY_CLOAK_D].oc_oprop), objects[DUMMY_CLOAK_D].a_ac, objects[DUMMY_CLOAK_D].a_can ); break;
			case DUMMY_CLOAK_E: 
				pline("This cloak is not randomly generated and only appears under certain rare conditions, but it has properties anyway just in case one does generate (as seen here, since otherwise you wouldn't be reading this). Its main property is %s, but it gives armor class of %d and magic cancellation of %d as well.", enchname(objects[DUMMY_CLOAK_E].oc_oprop), objects[DUMMY_CLOAK_E].a_ac, objects[DUMMY_CLOAK_E].a_can ); break;
			case DUMMY_CLOAK_F: 
				pline("This cloak is not randomly generated and only appears under certain rare conditions, but it has properties anyway just in case one does generate (as seen here, since otherwise you wouldn't be reading this). Its main property is %s, but it gives armor class of %d and magic cancellation of %d as well.", enchname(objects[DUMMY_CLOAK_F].oc_oprop), objects[DUMMY_CLOAK_F].a_ac, objects[DUMMY_CLOAK_F].a_can ); break;
			case DUMMY_CLOAK_G: 
				pline("This cloak is not randomly generated and only appears under certain rare conditions, but it has properties anyway just in case one does generate (as seen here, since otherwise you wouldn't be reading this). Its main property is %s, but it gives armor class of %d and magic cancellation of %d as well.", enchname(objects[DUMMY_CLOAK_G].oc_oprop), objects[DUMMY_CLOAK_G].a_ac, objects[DUMMY_CLOAK_G].a_can ); break;
			case DUMMY_CLOAK_H: 
				pline("This cloak is not randomly generated and only appears under certain rare conditions, but it has properties anyway just in case one does generate (as seen here, since otherwise you wouldn't be reading this). Its main property is %s, but it gives armor class of %d and magic cancellation of %d as well.", enchname(objects[DUMMY_CLOAK_H].oc_oprop), objects[DUMMY_CLOAK_H].a_ac, objects[DUMMY_CLOAK_H].a_can ); break;
			case DUMMY_CLOAK_I: 
				pline("This cloak is not randomly generated and only appears under certain rare conditions, but it has properties anyway just in case one does generate (as seen here, since otherwise you wouldn't be reading this). Its main property is %s, but it gives armor class of %d and magic cancellation of %d as well.", enchname(objects[DUMMY_CLOAK_I].oc_oprop), objects[DUMMY_CLOAK_I].a_ac, objects[DUMMY_CLOAK_I].a_can ); break;
			case DUMMY_CLOAK_J: 
				pline("This cloak is not randomly generated and only appears under certain rare conditions, but it has properties anyway just in case one does generate (as seen here, since otherwise you wouldn't be reading this). Its main property is %s, but it gives armor class of %d and magic cancellation of %d as well.", enchname(objects[DUMMY_CLOAK_J].oc_oprop), objects[DUMMY_CLOAK_J].a_ac, objects[DUMMY_CLOAK_J].a_can ); break;
			case DUMMY_CLOAK_K: 
				pline("This cloak is not randomly generated and only appears under certain rare conditions, but it has properties anyway just in case one does generate (as seen here, since otherwise you wouldn't be reading this). Its main property is %s, but it gives armor class of %d and magic cancellation of %d as well.", enchname(objects[DUMMY_CLOAK_K].oc_oprop), objects[DUMMY_CLOAK_K].a_ac, objects[DUMMY_CLOAK_K].a_can ); break;
			case DUMMY_CLOAK_L: 
				pline("This cloak is not randomly generated and only appears under certain rare conditions, but it has properties anyway just in case one does generate (as seen here, since otherwise you wouldn't be reading this). Its main property is %s, but it gives armor class of %d and magic cancellation of %d as well.", enchname(objects[DUMMY_CLOAK_L].oc_oprop), objects[DUMMY_CLOAK_L].a_ac, objects[DUMMY_CLOAK_L].a_can ); break;
			case DUMMY_CLOAK_M: 
				pline("This cloak is not randomly generated and only appears under certain rare conditions, but it has properties anyway just in case one does generate (as seen here, since otherwise you wouldn't be reading this). Its main property is %s, but it gives armor class of %d and magic cancellation of %d as well.", enchname(objects[DUMMY_CLOAK_M].oc_oprop), objects[DUMMY_CLOAK_M].a_ac, objects[DUMMY_CLOAK_M].a_can ); break;
			case DUMMY_CLOAK_N: 
				pline("This cloak is not randomly generated and only appears under certain rare conditions, but it has properties anyway just in case one does generate (as seen here, since otherwise you wouldn't be reading this). Its main property is %s, but it gives armor class of %d and magic cancellation of %d as well.", enchname(objects[DUMMY_CLOAK_N].oc_oprop), objects[DUMMY_CLOAK_N].a_ac, objects[DUMMY_CLOAK_N].a_can ); break;
			case DUMMY_CLOAK_O: 
				pline("This cloak is not randomly generated and only appears under certain rare conditions, but it has properties anyway just in case one does generate (as seen here, since otherwise you wouldn't be reading this). Its main property is %s, but it gives armor class of %d and magic cancellation of %d as well.", enchname(objects[DUMMY_CLOAK_O].oc_oprop), objects[DUMMY_CLOAK_O].a_ac, objects[DUMMY_CLOAK_O].a_can ); break;
			case DUMMY_CLOAK_P: 
				pline("This cloak is not randomly generated and only appears under certain rare conditions, but it has properties anyway just in case one does generate (as seen here, since otherwise you wouldn't be reading this). Its main property is %s, but it gives armor class of %d and magic cancellation of %d as well.", enchname(objects[DUMMY_CLOAK_P].oc_oprop), objects[DUMMY_CLOAK_P].a_ac, objects[DUMMY_CLOAK_P].a_can ); break;
			case DUMMY_CLOAK_Q: 
				pline("This cloak is not randomly generated and only appears under certain rare conditions, but it has properties anyway just in case one does generate (as seen here, since otherwise you wouldn't be reading this). Its main property is %s, but it gives armor class of %d and magic cancellation of %d as well.", enchname(objects[DUMMY_CLOAK_Q].oc_oprop), objects[DUMMY_CLOAK_Q].a_ac, objects[DUMMY_CLOAK_Q].a_can ); break;
			case DUMMY_CLOAK_R: 
				pline("This cloak is not randomly generated and only appears under certain rare conditions, but it has properties anyway just in case one does generate (as seen here, since otherwise you wouldn't be reading this). Its main property is %s, but it gives armor class of %d and magic cancellation of %d as well.", enchname(objects[DUMMY_CLOAK_R].oc_oprop), objects[DUMMY_CLOAK_R].a_ac, objects[DUMMY_CLOAK_R].a_can ); break;
			case DUMMY_CLOAK_S: 
				pline("This cloak is not randomly generated and only appears under certain rare conditions, but it has properties anyway just in case one does generate (as seen here, since otherwise you wouldn't be reading this). Its main property is %s, but it gives armor class of %d and magic cancellation of %d as well.", enchname(objects[DUMMY_CLOAK_S].oc_oprop), objects[DUMMY_CLOAK_S].a_ac, objects[DUMMY_CLOAK_S].a_can ); break;
			case DUMMY_CLOAK_T: 
				pline("This cloak is not randomly generated and only appears under certain rare conditions, but it has properties anyway just in case one does generate (as seen here, since otherwise you wouldn't be reading this). Its main property is %s, but it gives armor class of %d and magic cancellation of %d as well.", enchname(objects[DUMMY_CLOAK_T].oc_oprop), objects[DUMMY_CLOAK_T].a_ac, objects[DUMMY_CLOAK_T].a_can ); break;
			case DUMMY_CLOAK_U: 
				pline("This cloak is not randomly generated and only appears under certain rare conditions, but it has properties anyway just in case one does generate (as seen here, since otherwise you wouldn't be reading this). Its main property is %s, but it gives armor class of %d and magic cancellation of %d as well.", enchname(objects[DUMMY_CLOAK_U].oc_oprop), objects[DUMMY_CLOAK_U].a_ac, objects[DUMMY_CLOAK_U].a_can ); break;
			case DUMMY_CLOAK_V: 
				pline("This cloak is not randomly generated and only appears under certain rare conditions, but it has properties anyway just in case one does generate (as seen here, since otherwise you wouldn't be reading this). Its main property is %s, but it gives armor class of %d and magic cancellation of %d as well.", enchname(objects[DUMMY_CLOAK_V].oc_oprop), objects[DUMMY_CLOAK_V].a_ac, objects[DUMMY_CLOAK_V].a_can ); break;

			case DUMMY_HELMET_A: 
				pline("This helmet is not randomly generated and only appears under certain rare conditions, but it has properties anyway just in case one does generate (as seen here, since otherwise you wouldn't be reading this). Its main property is %s, but it gives armor class of %d and magic cancellation of %d as well.", enchname(objects[DUMMY_HELMET_A].oc_oprop), objects[DUMMY_HELMET_A].a_ac, objects[DUMMY_HELMET_A].a_can ); break;
			case DUMMY_HELMET_B: 
				pline("This helmet is not randomly generated and only appears under certain rare conditions, but it has properties anyway just in case one does generate (as seen here, since otherwise you wouldn't be reading this). Its main property is %s, but it gives armor class of %d and magic cancellation of %d as well.", enchname(objects[DUMMY_HELMET_B].oc_oprop), objects[DUMMY_HELMET_B].a_ac, objects[DUMMY_HELMET_B].a_can ); break;
			case DUMMY_HELMET_C: 
				pline("This helmet is not randomly generated and only appears under certain rare conditions, but it has properties anyway just in case one does generate (as seen here, since otherwise you wouldn't be reading this). Its main property is %s, but it gives armor class of %d and magic cancellation of %d as well.", enchname(objects[DUMMY_HELMET_C].oc_oprop), objects[DUMMY_HELMET_C].a_ac, objects[DUMMY_HELMET_C].a_can ); break;
			case DUMMY_HELMET_D: 
				pline("This helmet is not randomly generated and only appears under certain rare conditions, but it has properties anyway just in case one does generate (as seen here, since otherwise you wouldn't be reading this). Its main property is %s, but it gives armor class of %d and magic cancellation of %d as well.", enchname(objects[DUMMY_HELMET_D].oc_oprop), objects[DUMMY_HELMET_D].a_ac, objects[DUMMY_HELMET_D].a_can ); break;
			case DUMMY_HELMET_E: 
				pline("This helmet is not randomly generated and only appears under certain rare conditions, but it has properties anyway just in case one does generate (as seen here, since otherwise you wouldn't be reading this). Its main property is %s, but it gives armor class of %d and magic cancellation of %d as well.", enchname(objects[DUMMY_HELMET_E].oc_oprop), objects[DUMMY_HELMET_E].a_ac, objects[DUMMY_HELMET_E].a_can ); break;
			case DUMMY_HELMET_F: 
				pline("This helmet is not randomly generated and only appears under certain rare conditions, but it has properties anyway just in case one does generate (as seen here, since otherwise you wouldn't be reading this). Its main property is %s, but it gives armor class of %d and magic cancellation of %d as well.", enchname(objects[DUMMY_HELMET_F].oc_oprop), objects[DUMMY_HELMET_F].a_ac, objects[DUMMY_HELMET_F].a_can ); break;
			case DUMMY_HELMET_G: 
				pline("This helmet is not randomly generated and only appears under certain rare conditions, but it has properties anyway just in case one does generate (as seen here, since otherwise you wouldn't be reading this). Its main property is %s, but it gives armor class of %d and magic cancellation of %d as well.", enchname(objects[DUMMY_HELMET_G].oc_oprop), objects[DUMMY_HELMET_G].a_ac, objects[DUMMY_HELMET_G].a_can ); break;
			case DUMMY_HELMET_H: 
				pline("This helmet is not randomly generated and only appears under certain rare conditions, but it has properties anyway just in case one does generate (as seen here, since otherwise you wouldn't be reading this). Its main property is %s, but it gives armor class of %d and magic cancellation of %d as well.", enchname(objects[DUMMY_HELMET_H].oc_oprop), objects[DUMMY_HELMET_H].a_ac, objects[DUMMY_HELMET_H].a_can ); break;
			case DUMMY_HELMET_I: 
				pline("This helmet is not randomly generated and only appears under certain rare conditions, but it has properties anyway just in case one does generate (as seen here, since otherwise you wouldn't be reading this). Its main property is %s, but it gives armor class of %d and magic cancellation of %d as well.", enchname(objects[DUMMY_HELMET_I].oc_oprop), objects[DUMMY_HELMET_I].a_ac, objects[DUMMY_HELMET_I].a_can ); break;
			case DUMMY_HELMET_J: 
				pline("This helmet is not randomly generated and only appears under certain rare conditions, but it has properties anyway just in case one does generate (as seen here, since otherwise you wouldn't be reading this). Its main property is %s, but it gives armor class of %d and magic cancellation of %d as well.", enchname(objects[DUMMY_HELMET_J].oc_oprop), objects[DUMMY_HELMET_J].a_ac, objects[DUMMY_HELMET_J].a_can ); break;
			case DUMMY_HELMET_K: 
				pline("This helmet is not randomly generated and only appears under certain rare conditions, but it has properties anyway just in case one does generate (as seen here, since otherwise you wouldn't be reading this). Its main property is %s, but it gives armor class of %d and magic cancellation of %d as well.", enchname(objects[DUMMY_HELMET_K].oc_oprop), objects[DUMMY_HELMET_K].a_ac, objects[DUMMY_HELMET_K].a_can ); break;

			case DUMMY_GLOVES_A: 
				pline("This pair of gloves is not randomly generated and only appears under certain rare conditions, but it has properties anyway just in case one does generate (as seen here, since otherwise you wouldn't be reading this). Its main property is %s, but it gives armor class of %d and magic cancellation of %d as well.", enchname(objects[DUMMY_GLOVES_A].oc_oprop), objects[DUMMY_GLOVES_A].a_ac, objects[DUMMY_GLOVES_A].a_can ); break;
			case DUMMY_GLOVES_B: 
				pline("This pair of gloves is not randomly generated and only appears under certain rare conditions, but it has properties anyway just in case one does generate (as seen here, since otherwise you wouldn't be reading this). Its main property is %s, but it gives armor class of %d and magic cancellation of %d as well.", enchname(objects[DUMMY_GLOVES_B].oc_oprop), objects[DUMMY_GLOVES_B].a_ac, objects[DUMMY_GLOVES_B].a_can ); break;
			case DUMMY_GLOVES_C: 
				pline("This pair of gloves is not randomly generated and only appears under certain rare conditions, but it has properties anyway just in case one does generate (as seen here, since otherwise you wouldn't be reading this). Its main property is %s, but it gives armor class of %d and magic cancellation of %d as well.", enchname(objects[DUMMY_GLOVES_C].oc_oprop), objects[DUMMY_GLOVES_C].a_ac, objects[DUMMY_GLOVES_C].a_can ); break;
			case DUMMY_GLOVES_D: 
				pline("This pair of gloves is not randomly generated and only appears under certain rare conditions, but it has properties anyway just in case one does generate (as seen here, since otherwise you wouldn't be reading this). Its main property is %s, but it gives armor class of %d and magic cancellation of %d as well.", enchname(objects[DUMMY_GLOVES_D].oc_oprop), objects[DUMMY_GLOVES_D].a_ac, objects[DUMMY_GLOVES_D].a_can ); break;
			case DUMMY_GLOVES_E: 
				pline("This pair of gloves is not randomly generated and only appears under certain rare conditions, but it has properties anyway just in case one does generate (as seen here, since otherwise you wouldn't be reading this). Its main property is %s, but it gives armor class of %d and magic cancellation of %d as well.", enchname(objects[DUMMY_GLOVES_E].oc_oprop), objects[DUMMY_GLOVES_E].a_ac, objects[DUMMY_GLOVES_E].a_can ); break;
			case DUMMY_GLOVES_F: 
				pline("This pair of gloves is not randomly generated and only appears under certain rare conditions, but it has properties anyway just in case one does generate (as seen here, since otherwise you wouldn't be reading this). Its main property is %s, but it gives armor class of %d and magic cancellation of %d as well.", enchname(objects[DUMMY_GLOVES_F].oc_oprop), objects[DUMMY_GLOVES_F].a_ac, objects[DUMMY_GLOVES_F].a_can ); break;
			case DUMMY_GLOVES_G: 
				pline("This pair of gloves is not randomly generated and only appears under certain rare conditions, but it has properties anyway just in case one does generate (as seen here, since otherwise you wouldn't be reading this). Its main property is %s, but it gives armor class of %d and magic cancellation of %d as well.", enchname(objects[DUMMY_GLOVES_G].oc_oprop), objects[DUMMY_GLOVES_G].a_ac, objects[DUMMY_GLOVES_G].a_can ); break;
			case DUMMY_GLOVES_H: 
				pline("This pair of gloves is not randomly generated and only appears under certain rare conditions, but it has properties anyway just in case one does generate (as seen here, since otherwise you wouldn't be reading this). Its main property is %s, but it gives armor class of %d and magic cancellation of %d as well.", enchname(objects[DUMMY_GLOVES_H].oc_oprop), objects[DUMMY_GLOVES_H].a_ac, objects[DUMMY_GLOVES_H].a_can ); break;
			case DUMMY_GLOVES_I: 
				pline("This pair of gloves is not randomly generated and only appears under certain rare conditions, but it has properties anyway just in case one does generate (as seen here, since otherwise you wouldn't be reading this). Its main property is %s, but it gives armor class of %d and magic cancellation of %d as well.", enchname(objects[DUMMY_GLOVES_I].oc_oprop), objects[DUMMY_GLOVES_I].a_ac, objects[DUMMY_GLOVES_I].a_can ); break;

			case DUMMY_BOOTS_A: 
				pline("This pair of boots is not randomly generated and only appears under certain rare conditions, but it has properties anyway just in case one does generate (as seen here, since otherwise you wouldn't be reading this). Its main property is %s, but it gives armor class of %d and magic cancellation of %d as well.", enchname(objects[DUMMY_BOOTS_A].oc_oprop), objects[DUMMY_BOOTS_A].a_ac, objects[DUMMY_BOOTS_A].a_can ); break;
			case DUMMY_BOOTS_B: 
				pline("This pair of boots is not randomly generated and only appears under certain rare conditions, but it has properties anyway just in case one does generate (as seen here, since otherwise you wouldn't be reading this). Its main property is %s, but it gives armor class of %d and magic cancellation of %d as well.", enchname(objects[DUMMY_BOOTS_B].oc_oprop), objects[DUMMY_BOOTS_B].a_ac, objects[DUMMY_BOOTS_B].a_can ); break;
			case DUMMY_BOOTS_C: 
				pline("This pair of boots is not randomly generated and only appears under certain rare conditions, but it has properties anyway just in case one does generate (as seen here, since otherwise you wouldn't be reading this). Its main property is %s, but it gives armor class of %d and magic cancellation of %d as well.", enchname(objects[DUMMY_BOOTS_C].oc_oprop), objects[DUMMY_BOOTS_C].a_ac, objects[DUMMY_BOOTS_C].a_can ); break;
			case DUMMY_BOOTS_D: 
				pline("This pair of boots is not randomly generated and only appears under certain rare conditions, but it has properties anyway just in case one does generate (as seen here, since otherwise you wouldn't be reading this). Its main property is %s, but it gives armor class of %d and magic cancellation of %d as well.", enchname(objects[DUMMY_BOOTS_D].oc_oprop), objects[DUMMY_BOOTS_D].a_ac, objects[DUMMY_BOOTS_D].a_can ); break;
			case DUMMY_BOOTS_E: 
				pline("This pair of boots is not randomly generated and only appears under certain rare conditions, but it has properties anyway just in case one does generate (as seen here, since otherwise you wouldn't be reading this). Its main property is %s, but it gives armor class of %d and magic cancellation of %d as well.", enchname(objects[DUMMY_BOOTS_E].oc_oprop), objects[DUMMY_BOOTS_E].a_ac, objects[DUMMY_BOOTS_E].a_can ); break;
			case DUMMY_BOOTS_F: 
				pline("This pair of boots is not randomly generated and only appears under certain rare conditions, but it has properties anyway just in case one does generate (as seen here, since otherwise you wouldn't be reading this). Its main property is %s, but it gives armor class of %d and magic cancellation of %d as well.", enchname(objects[DUMMY_BOOTS_F].oc_oprop), objects[DUMMY_BOOTS_F].a_ac, objects[DUMMY_BOOTS_F].a_can ); break;
			case DUMMY_BOOTS_G: 
				pline("This pair of boots is not randomly generated and only appears under certain rare conditions, but it has properties anyway just in case one does generate (as seen here, since otherwise you wouldn't be reading this). Its main property is %s, but it gives armor class of %d and magic cancellation of %d as well.", enchname(objects[DUMMY_BOOTS_G].oc_oprop), objects[DUMMY_BOOTS_G].a_ac, objects[DUMMY_BOOTS_G].a_can ); break;
			case DUMMY_BOOTS_H: 
				pline("This pair of boots is not randomly generated and only appears under certain rare conditions, but it has properties anyway just in case one does generate (as seen here, since otherwise you wouldn't be reading this). Its main property is %s, but it gives armor class of %d and magic cancellation of %d as well.", enchname(objects[DUMMY_BOOTS_H].oc_oprop), objects[DUMMY_BOOTS_H].a_ac, objects[DUMMY_BOOTS_H].a_can ); break;
			case DUMMY_BOOTS_I: 
				pline("This pair of boots is not randomly generated and only appears under certain rare conditions, but it has properties anyway just in case one does generate (as seen here, since otherwise you wouldn't be reading this). Its main property is %s, but it gives armor class of %d and magic cancellation of %d as well.", enchname(objects[DUMMY_BOOTS_I].oc_oprop), objects[DUMMY_BOOTS_I].a_ac, objects[DUMMY_BOOTS_I].a_can ); break;
			case DUMMY_BOOTS_J: 
				pline("This pair of boots is not randomly generated and only appears under certain rare conditions, but it has properties anyway just in case one does generate (as seen here, since otherwise you wouldn't be reading this). Its main property is %s, but it gives armor class of %d and magic cancellation of %d as well.", enchname(objects[DUMMY_BOOTS_J].oc_oprop), objects[DUMMY_BOOTS_J].a_ac, objects[DUMMY_BOOTS_J].a_can ); break;
			case DUMMY_BOOTS_K: 
				pline("This pair of boots is not randomly generated and only appears under certain rare conditions, but it has properties anyway just in case one does generate (as seen here, since otherwise you wouldn't be reading this). Its main property is %s, but it gives armor class of %d and magic cancellation of %d as well.", enchname(objects[DUMMY_BOOTS_K].oc_oprop), objects[DUMMY_BOOTS_K].a_ac, objects[DUMMY_BOOTS_K].a_can ); break;
			case DUMMY_BOOTS_L: 
				pline("This pair of boots is not randomly generated and only appears under certain rare conditions, but it has properties anyway just in case one does generate (as seen here, since otherwise you wouldn't be reading this). Its main property is %s, but it gives armor class of %d and magic cancellation of %d as well.", enchname(objects[DUMMY_BOOTS_L].oc_oprop), objects[DUMMY_BOOTS_L].a_ac, objects[DUMMY_BOOTS_L].a_can ); break;

			case EVIL_DRAGON_SCALE_MAIL:
				pline("An extremely sturdy armor that can deflect a heck of a lot of attacks. It also does something very nasty when worn, though..."); break;
			case EVIL_DRAGON_SCALES:
				pline("These provide lots of protection, but also do something nasty when worn..."); break;
			case EVIL_DRAGON_SCALE_SHIELD:
				pline("It's a shield that gives a ton of armor class and also a high blocking rate, but it does something nasty while you wear it..."); break;
			case MAGIC_DRAGON_SCALE_MAIL:
				pline("A suit of armor that offers great protection and also usually spawns with a random enchantment."); break;
			case MAGIC_DRAGON_SCALES:
				pline("A suit of armor that offers good protection and also usually spawns with a random enchantment."); break;
			case MAGIC_DRAGON_SCALE_SHIELD:
				pline("This shield offers good protection and also usually spawns with a random enchantment."); break;
			case CHANTER_SHIRT:
				pline("A shirt that usually has a random enchantment. It can be read."); break;
			case BAD_SHIRT:
				pline("This shirt provides good armor class and medium magic cancellation but also does something evil when worn."); break;
			case DIFFICULT_SHIELD:
				pline("A shield that provides lots and lots of armor class! Too bad that wearing it has some evil side effect."); break;
			case MAGICAL_SHIELD:
				pline("This shield offers protection, but its more important effect is that it provides %s.", enchname(objects[MAGICAL_SHIELD].oc_oprop)); break;
			case SPECIAL_SHIELD:
				pline("A shield that offers some protection and also usually spawns with a random enchantment."); break;
			case EVIL_PLATE_MAIL:
				pline("This suit of armor gives more than double the protection of full plate mail. But that comes at a high price..."); break;
			case EVIL_LEATHER_ARMOR:
				pline("Lightweight, doesn't hinder spellcasting, provides the same amount of armor class as a plate mail, but very evil indeed."); break;
			case SPECIAL_LEATHER_ARMOR:
				pline("It's a rather weak suit of armor, but it always spawns with an enchantment."); break;
			case MAGE_PLATE_MAIL:
				pline("A very useful suit of armor that can be used by spellcasters without penalties, plus it often spawns with an enchantment."); break;

			default: pline("Missing item description (this is a bug). Please tell Amy about the item in question so she can add a description."); break;

			}

		}
		break;

		case RING_CLASS:
#ifdef PHANTOM_CRASH_BUG
		pline("%s - Color: %s. Material: %s. Appearance: %s.",xname(obj), obj->dknown ? c_obj_colors[objects[obj->otyp].oc_color] : "unknown", obj->dknown ? materialnm[objects[obj->otyp].oc_material] : "unknown", obj->dknown ? dn : "unknown");
#else
		pline("%s - This is a ring. Color: %s. Material: %s. Appearance: %s. You can wear a maximum of two rings; they will often have some sort of magical effect if worn. Every worn ring will cause you to go hungry a little bit faster. Dropping a ring on a sink will cause it to disappear while providing you with a clue to its nature.",xname(obj), obj->dknown ? c_obj_colors[objects[obj->otyp].oc_color] : "unknown", obj->dknown ? materialnm[objects[obj->otyp].oc_material] : "unknown", obj->dknown ? dn : "unknown");
#endif
		if (!nn) pline("Unfortunately you don't know more about it. You will gain more information if you identify this item.");
		else { switch (obj->otyp) {

			case RIN_ADORNMENT: 
				pline("If you wear this ring, you will feel more charismatic."); break;
			case RIN_HUNGER: 
				pline("Put this ring on if you no longer want to be satiated. It is usually generated cursed and increases your food consumption rate."); break;
			case RIN_DISARMING: 
				pline("You will drop your weapon if you wear this ring. It is usually generated cursed."); break;
			case RIN_DRAIN_RESISTANCE: 
				pline("Wear this! It grants level-drain resistance!"); break;
			case RIN_NUMBNESS: 
				pline("Wearing this ring will numb your limbs, which is a Bad Thing (TM). It is usually generated cursed."); break;
			case RIN_CURSE: 
				pline("While wearing this ring, your items will sometimes get cursed. Putting this ring on causes it to autocurse."); break;
			case RIN_HALLUCINATION: 
				pline("You will hallucinate as long as you wear this ring. Putting it on causes it to autocurse."); break;
			case RIN_INTRINSIC_LOSS: 
				pline("This ring can cause intrinsic loss."); break;
			case RIN_TRAP_REVEALING: 
				pline("A very rare ring that grants its wearer the ability to randomly detect traps on the current dungeon level."); break;
			case RIN_BLOOD_LOSS:
				pline("This ring causes bleedout."); break;
			case RIN_IMMUNITY_TO_DRAWBRIDGES:
				pline("The only thing this ring does it to prevent you from being instakilled by drawbridges."); break;
			case RIN_NASTINESS:
				pline("This ring has nasty effects."); break;
			case RIN_BAD_EFFECT:
				pline("This ring has bad effects."); break;
			case RIN_SUPERSCROLLING:
				pline("This ring causes the superscroller effect."); break;
			case RIN_ANTI_DROP:
				pline("This ring causes items to not drop."); break;
			case RIN_ENSNARING:
				pline("This ring causes traps to become invisible."); break;
			case RIN_DIARRHEA:
				pline("This ring causes diarrhea. It was invented by bhaak, who is also known as 'Schwebaeugler' and wants to kill Amy Bluescreenofdeath. :-)"); break;
			case RIN_DISENGRAVING:
				pline("This ring causes engravings to fail."); break;
			case RIN_AUTOCURSING: 
				pline("If you wear this ring, everything that you put on or wield will curse itself!"); break;
			case RIN_TIME_SPENDING: 
				pline("While wearing this ring, everything takes time, including opening your inventory or farlooking a monster."); break;
			case RIN_NO_SKILL:
				pline("This ring deactivates all of your skills."); break;
			case RIN_LOOTCUT:
				pline("This ring prevents you from claiming musable items from monsters."); break;
			case RIN_FORM_SHIFTING:
				pline("This ring randomly polymorphs you into werefoo."); break;
			case RIN_LAGGING:
				pline("This ring can cause the game to ignore your commands, simulating laggy online server conditions."); break;
			case RIN_BLESSCURSING:
				pline("This ring can cause blessed items to instantly become cursed."); break;
			case RIN_ILLITERACY:
				pline("This ring sometimes causes scrolls to disintegrate while you're trying to pick them up."); break;
			case RIN_LOW_STATS:
				pline("This ring greatly lowers all of your stats."); break;
			case RIN_FAILED_TRAINING:
				pline("This ring prevents you from training your skills."); break;
			case RIN_FAILED_EXERCISE:
				pline("This ring prevents you from exercising your stats."); break;
			case RIN_FAST_METABOLISM:
				pline("This ring causes you to burn nutrition much faster."); break;
			case RIN_MOOD:
				pline("A fairly useless ring that requires you to put it on, then read it to reveal a not-very-enlightening message."); break;
			case RIN_PROTECTION:
				pline("If it is enchanted, this ring will increase your armor class when worn."); break;
			case RIN_PROTECTION_FROM_SHAPE_CHAN:
				pline("Most shapeshifters are forced back into their natural form if you wear this ring, and they are prevented from changing form too."); break;
			case RIN_SLEEPING:
				pline("Wearing this ring causes you to fall asleep. It is usually generated cursed."); break;
			case RIN_ALACRITY:
				pline("Wanna be fast? Sure you do. But this ring makes you *very* fast, so it's even better!"); break;
			case RIN_DIMNESS:
				pline("A ring that causes dimness and is usually generated cursed."); break;
			case RIN_STEALTH:
				pline("You will make less noise if you wear this ring."); break;
			case RIN_MEMORY:
				pline("A ring that provides the otherwise unobtainable amnesia resistance."); break;
			case RIN_SUSTAIN_ABILITY: 
				pline("This ring locks your stats if worn, i.e. they can be neither increased nor decreased."); break;
			case RIN_WARNING: 
				pline("This ring allows you to detect monsters and get a difficulty rating indicating their strength."); break;
			case RIN_AGGRAVATE_MONSTER: 
				pline("If you wear this ring, monsters will chase you more aggressively and also wake up more quickly. It is usually generated cursed."); break;
			case RIN_COLD_RESISTANCE: 
				pline("You can resist cold if you wear this ring."); break;
			case RIN_FEAR_RESISTANCE: 
				pline("You can resist fear if you wear this ring."); break;
			case RIN_GAIN_CONSTITUTION: 
				pline("Wearing this ring increases your constitution by its enchantment value."); break;
			case RIN_GAIN_DEXTERITY: 
				pline("Wearing this ring increases your dexterity by its enchantment value."); break;
			case RIN_GAIN_INTELLIGENCE: 
				pline("Wearing this ring increases your intelligence by its enchantment value."); break;
			case RIN_GAIN_STRENGTH: 
				pline("Wearing this ring increases your strength by its enchantment value."); break;
			case RIN_GAIN_WISDOM: 
				pline("Wearing this ring increases your wisdom by its enchantment value."); break;
			case RIN_TIMELY_BACKUP: 
				pline("Wearing this ring causes you to feel absolutely safe, which doesn't actually do anything."); break;
			case RIN_INCREASE_ACCURACY: 
				pline("Wearing this ring increases your to-hit rate by its enchantment value."); break;
			case RIN_INCREASE_DAMAGE: 
				pline("Wearing this ring increases your attack damage by its enchantment value."); break;
			case RIN_SLOW_DIGESTION: 
#ifdef PHANTOM_CRASH_BUG
				pline("If you wear this ring, your natural food consumption rate is disabled. Ring hunger still applies though."); break;
#else
				pline("If you wear this ring, your natural food consumption rate is disabled. Keep in mind that ring hunger still applies, so wearing two of these actually causes you to consume more food than you would while wearing just one."); break;
#endif
			case RIN_INVISIBILITY: 
				pline("This powerful ring can be slipped on a finger to turn the wearer invisible."); break;
			case RIN_POISON_RESISTANCE: 
				pline("A ring that grants poison resistance when worn."); break;
			case RIN_SEE_INVISIBLE: 
				pline("If something is invisible, wear this ring and you can see it."); break;
			case RIN_INFRAVISION: 
				pline("You get the ability to see warm-blooded monsters in the dark while wearing this ring."); break;
			case RIN_SHOCK_RESISTANCE: 
				pline("You can resist shock if you wear this ring."); break;
			case RIN_HEAVY_ATTACK: 
				pline("If this ring is positively enchanted, it increases your damage and to-hit. A negatively enchanted one will decrease it instead."); break;
			case RIN_CONFUSION_RESISTANCE: 
				pline("You will be resistant (but not immune) to the bad effects of confusion while wearing this ring."); break;
			case RIN_PRACTICE: 
				pline("A ring that makes it much easier to practice your skills and exercise your stats."); break;
			case RIN_RESTRATION: 
				pline("This ring grants stun resistance, which is very hard to get otherwise and greatly reduces the odds of moving in the wrong direction."); break;
			case RIN_SICKNESS_RESISTANCE: 
				pline("A very powerful ring that allows you to eat tainted corpses and be hit by sickness attacks without actually getting sick."); break;
			case RIN_FIRE_RESISTANCE: 
				pline("You can resist fire if you wear this ring."); break;
			case RIN_FREE_ACTION: 
				pline("This ring protects you from paralysis and similar effects."); break;
			case RIN_DISCOUNT_ACTION:
				pline("This ring shortens the duration of paralysis effects. It also works on some kinds of paralysis that free action doesn't protect against."); break;
			case RIN_LEVITATION:
				pline("Wearing this ring allows you to float into the air. This prevents you from performing certain actions, e.g. picking up items or using a set of downstairs."); break;
			case RIN_REGENERATION:
				pline("Wear this ring to increase your HP regneration rate. It increases your food consumption rate."); break;
			case RIN_SEARCHING:
				pline("If you want automatic searching so you don't have to continuously press the S key, wear this."); break;
			case RIN_TELEPORTATION:
				pline("A ring that grants teleportitis when worn. It is usually generated cursed."); break;
			case RIN_CONFLICT:
				pline("Monsters will attack each other if you wear a ring of conflict. It greatly increases your food consumption rate."); break;
			case RIN_POLYMORPH:
				pline("A ring that grants polymorphitis when worn. It is usually generated cursed."); break;
			case RIN_POLYMORPH_CONTROL:
				pline("While wearing this ring, you can control your polymorphs and specify what monster you would like to become."); break;
			case RIN_TELEPORT_CONTROL:
				pline("While wearing this ring, you can control your teleports and specify where you want to go."); break;
			case RIN_DOOM:
				pline("This ring sets your luck to -13. It will autocurse when worn."); break;
			case RIN_ELEMENTS:
				pline("A rare ring that grants you the fire, cold and shock resistances."); break;
			case RIN_LIGHT:
				pline("Improve your sight by wearing this ring!"); break;
			case RIN_MAGIC_RESISTANCE:
				pline("If you want magic resistance, this is one way of getting it."); break;
			case RIN_MATERIAL_STABILITY:
				pline("Disintegration resistance is what this ring grants to the wearer."); break;
			case RIN_MIND_SHIELDING:
				pline("One of very few ways to get psi resistance is putting this baby on one of your fingers."); break;
			case RIN_RANDOM_EFFECTS:
				pline("A ring that grants the magical effect of %s.", enchname(objects[RIN_RANDOM_EFFECTS].oc_oprop) ); break;
			case RIN_SPECIAL_EFFECTS: 
				pline("A ring that grants the magical effect of %s.", enchname(objects[RIN_SPECIAL_EFFECTS].oc_oprop) ); break;
			case RIN_LEECH:
				pline("A ring that allows you to restore a bit of mana if you kill a monster."); break;
			case RIN_DANGER:
				pline("This ring is inscribed 'For you specially, %s. Sincerely, Arabella.'", plname); break;


 			default: pline("Missing item description (this is a bug). Please tell Amy about the item in question so she can add a description."); break;

			}

		}
		break;

		case IMPLANT_CLASS:

		pline("%s - This is an implant. Color: %s. Material: %s. Appearance: %s. It can be worn for some magical effect and armor class, but they're hard to identify and may autocurse when worn. Also, unless your skill is high enough, you might not be able to take them off even when they're uncursed. If you don't have hands, you get extra bonus AC and intrinsics from wearing one.",xname(obj), obj->dknown ? c_obj_colors[objects[obj->otyp].oc_color] : "unknown", obj->dknown ? materialnm[objects[obj->otyp].oc_material] : "unknown", obj->dknown ? dn : "unknown");

		if (nn && nohands(youmonst.data) && !Race_if(PM_TRANSFORMER) && (uimplant && obj == uimplant) ) pline("As long as you're in a form without hands, wearing this implant grants %s.", enchname(goodimplanteffect(uimplant)) );

		if (!nn) pline("Unfortunately you don't know more about it. You will gain more information if you identify this item.");
		else { switch (obj->otyp) {

			case IMPLANT_OF_ABSORPTION:
				pline("An implant that gives %d points of AC.", objects[IMPLANT_OF_ABSORPTION].a_ac); break;

			case IMPLANT_OF_PUNCTURING:
				pline("An implant that gives %d points of AC.", objects[IMPLANT_OF_PUNCTURING].a_ac); break;

			case IMPLANT_OF_CRAFTSMANSHIP:
				pline("An implant that gives %d points of AC.", objects[IMPLANT_OF_CRAFTSMANSHIP].a_ac); break;

			case IMPLANT_OF_PRECISION:
				pline("An implant that gives %d points of AC.", objects[IMPLANT_OF_PRECISION].a_ac); break;

			case IMPLANT_OF_VILENESS:
				pline("An implant that gives %d points of AC.", objects[IMPLANT_OF_VILENESS].a_ac); break;

			case IMPLANT_OF_REMEDY:
				pline("An implant that gives %d points of AC.", objects[IMPLANT_OF_REMEDY].a_ac); break;

			case IMPLANT_OF_STOICISM:
				pline("An implant that gives %d points of AC.", objects[IMPLANT_OF_STOICISM].a_ac); break;

			case IMPLANT_OF_AVARICE:
				pline("An implant that gives %d points of AC.", objects[IMPLANT_OF_AVARICE].a_ac); break;

			case IMPLANT_OF_CREMATION:
				pline("An implant that gives %d points of AC.", objects[IMPLANT_OF_CREMATION].a_ac); break;

			case IMPLANT_OF_SEARING:
				pline("An implant that gives %d points of AC.", objects[IMPLANT_OF_SEARING].a_ac); break;

			case IMPLANT_OF_REDEMPTION:
				pline("An implant that gives %d points of AC.", objects[IMPLANT_OF_REDEMPTION].a_ac); break;

			case IMPLANT_OF_EROSION:
				pline("An implant that gives %d points of AC.", objects[IMPLANT_OF_EROSION].a_ac); break;

			case IMPLANT_OF_JOY:
				pline("An implant that gives %d points of AC.", objects[IMPLANT_OF_JOY].a_ac); break;

			case IMPLANT_OF_CRUELTY:
				pline("An implant that gives %d points of AC.", objects[IMPLANT_OF_CRUELTY].a_ac); break;

			case IMPLANT_OF_BADNESS:
				pline("An implant that gives %d points of AC.", objects[IMPLANT_OF_BADNESS].a_ac); break;

			case IMPLANT_OF_PROPOGATION:
				pline("An implant that gives %d points of AC.", objects[IMPLANT_OF_PROPOGATION].a_ac); break;

			case IMPLANT_OF_PASSION:
				pline("An implant that gives %d points of AC.", objects[IMPLANT_OF_PASSION].a_ac); break;

			case IMPLANT_OF_WINTER:
				pline("An implant that gives %d points of AC.", objects[IMPLANT_OF_WINTER].a_ac); break;

			case IMPLANT_OF_ACCELERATION:
				pline("An implant that gives %d points of AC.", objects[IMPLANT_OF_ACCELERATION].a_ac); break;

			case IMPLANT_OF_PROSPERITY:
				pline("An implant that gives %d points of AC.", objects[IMPLANT_OF_PROSPERITY].a_ac); break;

			case IMPLANT_OF_QUICKENING:
				pline("An implant that gives %d points of AC and some unknown nasty trap effect.", objects[IMPLANT_OF_QUICKENING].a_ac); break;

			case IMPLANT_OF_KARMA:
				pline("An implant that gives %d points of AC and some unknown nasty trap effect.", objects[IMPLANT_OF_KARMA].a_ac); break;

			case IMPLANT_OF_FERVOR:
				pline("An implant that gives %d points of AC and some unknown nasty trap effect.", objects[IMPLANT_OF_FERVOR].a_ac); break;

			case IMPLANT_OF_TRANSCENDENCE:
				pline("An implant that gives %d points of AC and some unknown nasty trap effect.", objects[IMPLANT_OF_TRANSCENDENCE].a_ac); break;

			case IMPLANT_OF_ELUSION:
				pline("An implant that gives %d points of AC and some unknown nasty trap effect.", objects[IMPLANT_OF_ELUSION].a_ac); break;

			case IMPLANT_OF_STATURE:
				pline("An implant that gives %d points of AC and some unknown nasty trap effect.", objects[IMPLANT_OF_STATURE].a_ac); break;

			case IMPLANT_OF_SUFFERING:
				pline("An implant that gives %d points of AC and some unknown nasty trap effect.", objects[IMPLANT_OF_SUFFERING].a_ac); break;

			case IMPLANT_OF_BADASS:
				pline("An implant that gives %d points of AC and some unknown nasty trap effect.", objects[IMPLANT_OF_BADASS].a_ac); break;

			case IMPLANT_OF_FAST_REPAIR:
				pline("An implant that gives %d points of AC and some unknown nasty trap effect.", objects[IMPLANT_OF_FAST_REPAIR].a_ac); break;

			case IMPLANT_OF_PILFERING:
				pline("An implant that gives %d points of AC and some unknown nasty trap effect.", objects[IMPLANT_OF_PILFERING].a_ac); break;

			case IMPLANT_OF_REPLENISHING:
				pline("An implant that gives %d points of AC and some unknown nasty trap effect.", objects[IMPLANT_OF_REPLENISHING].a_ac); break;

			case IMPLANT_OF_HONOR:
				pline("An implant that gives %d points of AC and some unknown nasty trap effect.", objects[IMPLANT_OF_HONOR].a_ac); break;

			case IMPLANT_OF_CONTROL:
				pline("An implant that gives %d points of AC and some unknown nasty trap effect.", objects[IMPLANT_OF_CONTROL].a_ac); break;

			case IMPLANT_OF_CLUMSINESS:
				pline("An implant that gives %d points of AC and some unknown nasty trap effect.", objects[IMPLANT_OF_CLUMSINESS].a_ac); break;

			case IMPLANT_OF_INSULATION:
				pline("An implant that gives %d points of AC and some unknown nasty trap effect.", objects[IMPLANT_OF_INSULATION].a_ac); break;

			case IMPLANT_OF_FRAILTY:
				pline("An implant that gives %d points of AC and some unknown nasty trap effect.", objects[IMPLANT_OF_FRAILTY].a_ac); break;

			case IMPLANT_OF_KNOWLEDGE:
				pline("An implant that gives %d points of AC and some unknown nasty trap effect.", objects[IMPLANT_OF_KNOWLEDGE].a_ac); break;

			case IMPLANT_OF_VENGEANCE:
				pline("An implant that gives %d points of AC and some unknown nasty trap effect.", objects[IMPLANT_OF_VENGEANCE].a_ac); break;

			case IMPLANT_OF_BLISS:
				pline("An implant that gives %d points of AC and some unknown nasty trap effect.", objects[IMPLANT_OF_BLISS].a_ac); break;

			case IMPLANT_OF_BLITZEN:
				pline("An implant that gives %d points of AC and some unknown nasty trap effect.", objects[IMPLANT_OF_BLITZEN].a_ac); break;

			case IMPLANT_OF_IRE:
				pline("An implant that gives %d points of AC and the %s enchantment.", objects[IMPLANT_OF_IRE].a_ac, enchname(objects[IMPLANT_OF_IRE].oc_oprop)); break;

			case IMPLANT_OF_MALICE:
				pline("An implant that gives %d points of AC and the %s enchantment.", objects[IMPLANT_OF_MALICE].a_ac, enchname(objects[IMPLANT_OF_MALICE].oc_oprop)); break;

			case IMPLANT_OF_AGES:
				pline("An implant that gives %d points of AC and the %s enchantment.", objects[IMPLANT_OF_AGES].a_ac, enchname(objects[IMPLANT_OF_AGES].oc_oprop)); break;

			case IMPLANT_OF_SUSTENANCE:
				pline("An implant that gives %d points of AC and the %s enchantment.", objects[IMPLANT_OF_SUSTENANCE].a_ac, enchname(objects[IMPLANT_OF_SUSTENANCE].oc_oprop)); break;

			case IMPLANT_OF_TRUTH:
				pline("An implant that gives %d points of AC and the %s enchantment.", objects[IMPLANT_OF_TRUTH].a_ac, enchname(objects[IMPLANT_OF_TRUTH].oc_oprop)); break;

			case IMPLANT_OF_REMORSE:
				pline("An implant that gives %d points of AC and the %s enchantment.", objects[IMPLANT_OF_REMORSE].a_ac, enchname(objects[IMPLANT_OF_REMORSE].oc_oprop)); break;

			case IMPLANT_OF_GRACE:
				pline("An implant that gives %d points of AC and the %s enchantment.", objects[IMPLANT_OF_GRACE].a_ac, enchname(objects[IMPLANT_OF_GRACE].oc_oprop)); break;

			case IMPLANT_OF_WASTE:
				pline("An implant that gives %d points of AC and the %s enchantment.", objects[IMPLANT_OF_WASTE].a_ac, enchname(objects[IMPLANT_OF_WASTE].oc_oprop)); break;

			case IMPLANT_OF_COMBAT:
				pline("An implant that gives %d points of AC and the %s enchantment.", objects[IMPLANT_OF_COMBAT].a_ac, enchname(objects[IMPLANT_OF_COMBAT].oc_oprop)); break;

			case IMPLANT_OF_FAITH:
				pline("An implant that gives %d points of AC and the %s enchantment.", objects[IMPLANT_OF_FAITH].a_ac, enchname(objects[IMPLANT_OF_FAITH].oc_oprop)); break;

			case IMPLANT_OF_DISPATCH:
				pline("An implant that gives %d points of AC and the %s enchantment.", objects[IMPLANT_OF_DISPATCH].a_ac, enchname(objects[IMPLANT_OF_DISPATCH].oc_oprop)); break;

			case IMPLANT_OF_DREAD:
				pline("An implant that gives %d points of AC and the %s enchantment.", objects[IMPLANT_OF_DREAD].a_ac, enchname(objects[IMPLANT_OF_DREAD].oc_oprop)); break;

			case IMPLANT_OF_VITA:
				pline("An implant that gives %d points of AC and the %s enchantment.", objects[IMPLANT_OF_VITA].a_ac, enchname(objects[IMPLANT_OF_VITA].oc_oprop)); break;

			case IMPLANT_OF_MAGGOTS:
				pline("An implant that gives %d points of AC and the %s enchantment.", objects[IMPLANT_OF_MAGGOTS].a_ac, enchname(objects[IMPLANT_OF_MAGGOTS].oc_oprop)); break;

			case IMPLANT_OF_BEAUTY:
				pline("An implant that gives %d points of AC and the %s enchantment.", objects[IMPLANT_OF_BEAUTY].a_ac, enchname(objects[IMPLANT_OF_BEAUTY].oc_oprop)); break;

			case IMPLANT_OF_DUSK:
				pline("An implant that gives %d points of AC and the %s enchantment.", objects[IMPLANT_OF_DUSK].a_ac, enchname(objects[IMPLANT_OF_DUSK].oc_oprop)); break;

			case IMPLANT_OF_TRIBUTE:
				pline("An implant that gives %d points of AC and the %s enchantment.", objects[IMPLANT_OF_TRIBUTE].a_ac, enchname(objects[IMPLANT_OF_TRIBUTE].oc_oprop)); break;

			case IMPLANT_OF_INERTIA:
				pline("An implant that gives %d points of AC and the %s enchantment.", objects[IMPLANT_OF_INERTIA].a_ac, enchname(objects[IMPLANT_OF_INERTIA].oc_oprop)); break;

			case IMPLANT_OF_SWEETNESS:
				pline("An implant that gives %d points of AC and the %s enchantment.", objects[IMPLANT_OF_SWEETNESS].a_ac, enchname(objects[IMPLANT_OF_SWEETNESS].oc_oprop)); break;

			case IMPLANT_OF_IRRIGATION:
				pline("An implant that gives %d points of AC and the %s enchantment.", objects[IMPLANT_OF_IRRIGATION].a_ac, enchname(objects[IMPLANT_OF_IRRIGATION].oc_oprop)); break;

			case IMPLANT_OF_TWILIGHT:
				if (!PlayerCannotUseSkills && P_SKILL(P_IMPLANTS) >= P_GRAND_MASTER) pline("An implant that gives %d points of AC and the %s enchantment.", objects[IMPLANT_OF_TWILIGHT].a_ac, enchname(objects[IMPLANT_OF_TWILIGHT].oc_oprop));
				else pline("An implant that gives %d points of AC. It also gives an enchantment, but your skill isn't high enough to recognize it.", objects[IMPLANT_OF_TWILIGHT].a_ac); break;

			case IMPLANT_OF_MEMORY:
				if (!PlayerCannotUseSkills && P_SKILL(P_IMPLANTS) >= P_GRAND_MASTER) pline("An implant that gives %d points of AC and the %s enchantment.", objects[IMPLANT_OF_MEMORY].a_ac, enchname(objects[IMPLANT_OF_MEMORY].oc_oprop));
				else pline("An implant that gives %d points of AC. It also gives an enchantment, but your skill isn't high enough to recognize it.", objects[IMPLANT_OF_MEMORY].a_ac); break;

			case IMPLANT_OF_LOVE:
				if (!PlayerCannotUseSkills && P_SKILL(P_IMPLANTS) >= P_GRAND_MASTER) pline("An implant that gives %d points of AC and the %s enchantment.", objects[IMPLANT_OF_LOVE].a_ac, enchname(objects[IMPLANT_OF_LOVE].oc_oprop));
				else pline("An implant that gives %d points of AC. It also gives an enchantment, but your skill isn't high enough to recognize it.", objects[IMPLANT_OF_LOVE].a_ac); break;

			case IMPLANT_OF_VINES:
				if (!PlayerCannotUseSkills && P_SKILL(P_IMPLANTS) >= P_GRAND_MASTER) pline("An implant that gives %d points of AC and the %s enchantment.", objects[IMPLANT_OF_VINES].a_ac, enchname(objects[IMPLANT_OF_VINES].oc_oprop));
				else pline("An implant that gives %d points of AC. It also gives an enchantment, but your skill isn't high enough to recognize it.", objects[IMPLANT_OF_VINES].a_ac); break;

			case IMPLANT_OF_ANIMA:
				if (!PlayerCannotUseSkills && P_SKILL(P_IMPLANTS) >= P_GRAND_MASTER) pline("An implant that gives %d points of AC and the %s enchantment.", objects[IMPLANT_OF_ANIMA].a_ac, enchname(objects[IMPLANT_OF_ANIMA].oc_oprop));
				else pline("An implant that gives %d points of AC. It also gives an enchantment, but your skill isn't high enough to recognize it.", objects[IMPLANT_OF_ANIMA].a_ac); break;

			case IMPLANT_OF_LINES:
				if (!PlayerCannotUseSkills && P_SKILL(P_IMPLANTS) >= P_GRAND_MASTER) pline("An implant that gives %d points of AC and the %s enchantment.", objects[IMPLANT_OF_LINES].a_ac, enchname(objects[IMPLANT_OF_LINES].oc_oprop));
				else pline("An implant that gives %d points of AC. It also gives an enchantment, but your skill isn't high enough to recognize it.", objects[IMPLANT_OF_LINES].a_ac); break;

			case IMPLANT_OF_THAWING:
				if (!PlayerCannotUseSkills && P_SKILL(P_IMPLANTS) >= P_GRAND_MASTER) pline("An implant that gives %d points of AC and the %s enchantment.", objects[IMPLANT_OF_THAWING].a_ac, enchname(objects[IMPLANT_OF_THAWING].oc_oprop));
				else pline("An implant that gives %d points of AC. It also gives an enchantment, but your skill isn't high enough to recognize it.", objects[IMPLANT_OF_THAWING].a_ac); break;

			case IMPLANT_OF_DESIRE:
				if (!PlayerCannotUseSkills && P_SKILL(P_IMPLANTS) >= P_GRAND_MASTER) pline("An implant that gives %d points of AC and the %s enchantment.", objects[IMPLANT_OF_DESIRE].a_ac, enchname(objects[IMPLANT_OF_DESIRE].oc_oprop));
				else pline("An implant that gives %d points of AC. It also gives an enchantment, but your skill isn't high enough to recognize it.", objects[IMPLANT_OF_DESIRE].a_ac); break;

			case IMPLANT_OF_PAIN:
				if (!PlayerCannotUseSkills && P_SKILL(P_IMPLANTS) >= P_GRAND_MASTER) pline("An implant that gives %d points of AC and the %s enchantment.", objects[IMPLANT_OF_PAIN].a_ac, enchname(objects[IMPLANT_OF_PAIN].oc_oprop));
				else pline("An implant that gives %d points of AC. It also gives an enchantment, but your skill isn't high enough to recognize it.", objects[IMPLANT_OF_PAIN].a_ac); break;

			case IMPLANT_OF_DARING:
				if (!PlayerCannotUseSkills && P_SKILL(P_IMPLANTS) >= P_GRAND_MASTER) pline("An implant that gives %d points of AC and the %s enchantment.", objects[IMPLANT_OF_DARING].a_ac, enchname(objects[IMPLANT_OF_DARING].oc_oprop));
				else pline("An implant that gives %d points of AC. It also gives an enchantment, but your skill isn't high enough to recognize it.", objects[IMPLANT_OF_DARING].a_ac); break;

			case IMPLANT_OF_CORRUPTION:
				if (!PlayerCannotUseSkills && P_SKILL(P_IMPLANTS) >= P_GRAND_MASTER) pline("An implant that gives %d points of AC and the %s enchantment.", objects[IMPLANT_OF_CORRUPTION].a_ac, enchname(objects[IMPLANT_OF_CORRUPTION].oc_oprop));
				else pline("An implant that gives %d points of AC. It also gives an enchantment, but your skill isn't high enough to recognize it.", objects[IMPLANT_OF_CORRUPTION].a_ac); break;

			case IMPLANT_OF_EVISCERATION:
				if (!PlayerCannotUseSkills && P_SKILL(P_IMPLANTS) >= P_GRAND_MASTER) pline("An implant that gives %d points of AC and the %s enchantment.", objects[IMPLANT_OF_EVISCERATION].a_ac, enchname(objects[IMPLANT_OF_EVISCERATION].oc_oprop));
				else pline("An implant that gives %d points of AC. It also gives an enchantment, but your skill isn't high enough to recognize it.", objects[IMPLANT_OF_EVISCERATION].a_ac); break;

			case IMPLANT_OF_TRAVELING:
				if (!PlayerCannotUseSkills && P_SKILL(P_IMPLANTS) >= P_GRAND_MASTER) pline("An implant that gives %d points of AC and the %s enchantment.", objects[IMPLANT_OF_TRAVELING].a_ac, enchname(objects[IMPLANT_OF_TRAVELING].oc_oprop));
				else pline("An implant that gives %d points of AC. It also gives an enchantment, but your skill isn't high enough to recognize it.", objects[IMPLANT_OF_TRAVELING].a_ac); break;

			case IMPLANT_OF_CHEATING:
				if (!PlayerCannotUseSkills && P_SKILL(P_IMPLANTS) >= P_GRAND_MASTER) pline("An implant that gives %d points of AC and the %s enchantment.", objects[IMPLANT_OF_CHEATING].a_ac, enchname(objects[IMPLANT_OF_CHEATING].oc_oprop));
				else pline("An implant that gives %d points of AC. It also gives an enchantment, but your skill isn't high enough to recognize it.", objects[IMPLANT_OF_CHEATING].a_ac); break;

			case IMPLANT_OF_ANTHRAX:
				if (!PlayerCannotUseSkills && P_SKILL(P_IMPLANTS) >= P_GRAND_MASTER) pline("An implant that gives %d points of AC and the %s enchantment.", objects[IMPLANT_OF_ANTHRAX].a_ac, enchname(objects[IMPLANT_OF_ANTHRAX].oc_oprop));
				else pline("An implant that gives %d points of AC. It also gives an enchantment, but your skill isn't high enough to recognize it.", objects[IMPLANT_OF_ANTHRAX].a_ac); break;

			case IMPLANT_OF_ATTRITION:
				if (!PlayerCannotUseSkills && P_SKILL(P_IMPLANTS) >= P_GRAND_MASTER) pline("An implant that gives %d points of AC and the %s enchantment.", objects[IMPLANT_OF_ATTRITION].a_ac, enchname(objects[IMPLANT_OF_ATTRITION].oc_oprop));
				else pline("An implant that gives %d points of AC. It also gives an enchantment, but your skill isn't high enough to recognize it.", objects[IMPLANT_OF_ATTRITION].a_ac); break;

			case IMPLANT_OF_HACKING:
				if (!PlayerCannotUseSkills && P_SKILL(P_IMPLANTS) >= P_GRAND_MASTER) pline("An implant that gives %d points of AC and the %s enchantment.", objects[IMPLANT_OF_HACKING].a_ac, enchname(objects[IMPLANT_OF_HACKING].oc_oprop));
				else pline("An implant that gives %d points of AC. It also gives an enchantment, but your skill isn't high enough to recognize it.", objects[IMPLANT_OF_HACKING].a_ac); break;

			case IMPLANT_OF_PROSPERING:
				if (!PlayerCannotUseSkills && P_SKILL(P_IMPLANTS) >= P_GRAND_MASTER) pline("An implant that gives %d points of AC and the %s enchantment.", objects[IMPLANT_OF_PROSPERING].a_ac, enchname(objects[IMPLANT_OF_PROSPERING].oc_oprop));
				else pline("An implant that gives %d points of AC. It also gives an enchantment, but your skill isn't high enough to recognize it.", objects[IMPLANT_OF_PROSPERING].a_ac); break;

			case IMPLANT_OF_VALHALLA:
				if (!PlayerCannotUseSkills && P_SKILL(P_IMPLANTS) >= P_GRAND_MASTER) pline("An implant that gives %d points of AC and the %s enchantment.", objects[IMPLANT_OF_VALHALLA].a_ac, enchname(objects[IMPLANT_OF_VALHALLA].oc_oprop));
				else pline("An implant that gives %d points of AC. It also gives an enchantment, but your skill isn't high enough to recognize it.", objects[IMPLANT_OF_VALHALLA].a_ac); break;

			case IMPLANT_OF_DECEPTION:
				if (!PlayerCannotUseSkills && P_SKILL(P_IMPLANTS) >= P_GRAND_MASTER) pline("An implant that gives %d points of AC and the %s enchantment.", objects[IMPLANT_OF_DECEPTION].a_ac, enchname(objects[IMPLANT_OF_DECEPTION].oc_oprop));
				else pline("An implant that gives %d points of AC. It also gives an enchantment, but your skill isn't high enough to recognize it.", objects[IMPLANT_OF_DECEPTION].a_ac); break;

			case IMPLANT_OF_BUTCHERY:
				if (!PlayerCannotUseSkills && P_SKILL(P_IMPLANTS) >= P_GRAND_MASTER) pline("An implant that gives %d points of AC and the %s enchantment.", objects[IMPLANT_OF_BUTCHERY].a_ac, enchname(objects[IMPLANT_OF_BUTCHERY].oc_oprop));
				else pline("An implant that gives %d points of AC. It also gives an enchantment, but your skill isn't high enough to recognize it.", objects[IMPLANT_OF_BUTCHERY].a_ac); break;

			case IMPLANT_OF_BLIZZARD:
				if (!PlayerCannotUseSkills && P_SKILL(P_IMPLANTS) >= P_GRAND_MASTER) pline("An implant that gives %d points of AC and the %s enchantment.", objects[IMPLANT_OF_BLIZZARD].a_ac, enchname(objects[IMPLANT_OF_BLIZZARD].oc_oprop));
				else pline("An implant that gives %d points of AC. It also gives an enchantment, but your skill isn't high enough to recognize it.", objects[IMPLANT_OF_BLIZZARD].a_ac); break;

			case IMPLANT_OF_TERROR:
				if (!PlayerCannotUseSkills && P_SKILL(P_IMPLANTS) >= P_GRAND_MASTER) pline("An implant that gives %d points of AC and the %s enchantment.", objects[IMPLANT_OF_TERROR].a_ac, enchname(objects[IMPLANT_OF_TERROR].oc_oprop));
				else pline("An implant that gives %d points of AC. It also gives an enchantment, but your skill isn't high enough to recognize it.", objects[IMPLANT_OF_TERROR].a_ac); break;

			case IMPLANT_OF_DAWN:
				if (!PlayerCannotUseSkills && P_SKILL(P_IMPLANTS) >= P_GRAND_MASTER) pline("An implant that gives %d points of AC and the %s enchantment.", objects[IMPLANT_OF_DAWN].a_ac, enchname(objects[IMPLANT_OF_DAWN].oc_oprop));
				else pline("An implant that gives %d points of AC. It also gives an enchantment, but your skill isn't high enough to recognize it.", objects[IMPLANT_OF_DAWN].a_ac); break;

			case IMPLANT_OF_BILE:
				if (!PlayerCannotUseSkills && P_SKILL(P_IMPLANTS) >= P_GRAND_MASTER) pline("An implant that gives %d points of AC and the %s enchantment.", objects[IMPLANT_OF_BILE].a_ac, enchname(objects[IMPLANT_OF_BILE].oc_oprop));
				else pline("An implant that gives %d points of AC. It also gives an enchantment, but your skill isn't high enough to recognize it.", objects[IMPLANT_OF_BILE].a_ac); break;

			case IMPLANT_OF_CREDIT:
				if (!PlayerCannotUseSkills && P_SKILL(P_IMPLANTS) >= P_GRAND_MASTER) pline("An implant that gives %d points of AC and the %s enchantment.", objects[IMPLANT_OF_CREDIT].a_ac, enchname(objects[IMPLANT_OF_CREDIT].oc_oprop));
				else pline("An implant that gives %d points of AC. It also gives an enchantment, but your skill isn't high enough to recognize it.", objects[IMPLANT_OF_CREDIT].a_ac); break;

			case IMPLANT_OF_QUOTA:
				if (!PlayerCannotUseSkills && P_SKILL(P_IMPLANTS) >= P_GRAND_MASTER) pline("An implant that gives %d points of AC and the %s enchantment.", objects[IMPLANT_OF_QUOTA].a_ac, enchname(objects[IMPLANT_OF_QUOTA].oc_oprop));
				else pline("An implant that gives %d points of AC. It also gives an enchantment, but your skill isn't high enough to recognize it.", objects[IMPLANT_OF_QUOTA].a_ac); break;

			case IMPLANT_OF_VIRILITY:
				if (!PlayerCannotUseSkills && P_SKILL(P_IMPLANTS) >= P_GRAND_MASTER) pline("An implant that gives %d points of AC and the %s enchantment.", objects[IMPLANT_OF_VIRILITY].a_ac, enchname(objects[IMPLANT_OF_VIRILITY].oc_oprop));
				else pline("An implant that gives %d points of AC. It also gives an enchantment, but your skill isn't high enough to recognize it.", objects[IMPLANT_OF_VIRILITY].a_ac); break;

			case IMPLANT_OF_VANILLA:
				if (!PlayerCannotUseSkills && P_SKILL(P_IMPLANTS) >= P_GRAND_MASTER) pline("An implant that gives %d points of AC and the %s enchantment.", objects[IMPLANT_OF_VANILLA].a_ac, enchname(objects[IMPLANT_OF_VANILLA].oc_oprop));
				else pline("An implant that gives %d points of AC. It also gives an enchantment, but your skill isn't high enough to recognize it.", objects[IMPLANT_OF_VANILLA].a_ac); break;

			case IMPLANT_OF_HOPE:
				if (!PlayerCannotUseSkills && P_SKILL(P_IMPLANTS) >= P_GRAND_MASTER) pline("An implant that gives %d points of AC and the %s enchantment.", objects[IMPLANT_OF_HOPE].a_ac, enchname(objects[IMPLANT_OF_HOPE].oc_oprop));
				else pline("An implant that gives %d points of AC. It also gives an enchantment, but your skill isn't high enough to recognize it.", objects[IMPLANT_OF_HOPE].a_ac); break;

			case IMPLANT_OF_ABRASION:
				if (!PlayerCannotUseSkills && P_SKILL(P_IMPLANTS) >= P_GRAND_MASTER) pline("An implant that gives %d points of AC and the %s enchantment.", objects[IMPLANT_OF_ABRASION].a_ac, enchname(objects[IMPLANT_OF_ABRASION].oc_oprop));
				else pline("An implant that gives %d points of AC. It also gives an enchantment, but your skill isn't high enough to recognize it.", objects[IMPLANT_OF_ABRASION].a_ac); break;

			case IMPLANT_OF_OSMOSIS:
				if (!PlayerCannotUseSkills && P_SKILL(P_IMPLANTS) >= P_GRAND_MASTER) pline("An implant that gives %d points of AC and the %s enchantment.", objects[IMPLANT_OF_OSMOSIS].a_ac, enchname(objects[IMPLANT_OF_OSMOSIS].oc_oprop));
				else pline("An implant that gives %d points of AC. It also gives an enchantment, but your skill isn't high enough to recognize it.", objects[IMPLANT_OF_OSMOSIS].a_ac); break;

			case IMPLANT_OF_NIRVANA:
				if (!PlayerCannotUseSkills && P_SKILL(P_IMPLANTS) >= P_GRAND_MASTER) pline("An implant that gives %d points of AC and the %s enchantment.", objects[IMPLANT_OF_NIRVANA].a_ac, enchname(objects[IMPLANT_OF_NIRVANA].oc_oprop));
				else pline("An implant that gives %d points of AC. It also gives an enchantment, but your skill isn't high enough to recognize it.", objects[IMPLANT_OF_NIRVANA].a_ac); break;

			case IMPLANT_OF_ENVY:
				if (!PlayerCannotUseSkills && P_SKILL(P_IMPLANTS) >= P_GRAND_MASTER) pline("An implant that gives %d points of AC and the %s enchantment.", objects[IMPLANT_OF_ENVY].a_ac, enchname(objects[IMPLANT_OF_ENVY].oc_oprop));
				else pline("An implant that gives %d points of AC. It also gives an enchantment, but your skill isn't high enough to recognize it.", objects[IMPLANT_OF_ENVY].a_ac); break;

			case IMPLANT_OF_ENNUI:
				if (!PlayerCannotUseSkills && P_SKILL(P_IMPLANTS) >= P_GRAND_MASTER) pline("An implant that gives %d points of AC and the %s enchantment.", objects[IMPLANT_OF_ENNUI].a_ac, enchname(objects[IMPLANT_OF_ENNUI].oc_oprop));
				else pline("An implant that gives %d points of AC. It also gives an enchantment, but your skill isn't high enough to recognize it.", objects[IMPLANT_OF_ENNUI].a_ac); break;

			case IMPLANT_OF_IMPOSSIBILITY:
				if (!PlayerCannotUseSkills && P_SKILL(P_IMPLANTS) >= P_GRAND_MASTER) pline("An implant that gives %d points of AC and the %s enchantment.", objects[IMPLANT_OF_IMPOSSIBILITY].a_ac, enchname(objects[IMPLANT_OF_IMPOSSIBILITY].oc_oprop));
				else pline("An implant that gives %d points of AC. It also gives an enchantment, but your skill isn't high enough to recognize it.", objects[IMPLANT_OF_IMPOSSIBILITY].a_ac); break;

			case IMPLANT_OF_ADMIRATION:
				if (!PlayerCannotUseSkills && P_SKILL(P_IMPLANTS) >= P_GRAND_MASTER) pline("An implant that gives %d points of AC and the %s enchantment.", objects[IMPLANT_OF_ADMIRATION].a_ac, enchname(objects[IMPLANT_OF_ADMIRATION].oc_oprop));
				else pline("An implant that gives %d points of AC. It also gives an enchantment, but your skill isn't high enough to recognize it.", objects[IMPLANT_OF_ADMIRATION].a_ac); break;

			case IMPLANT_OF_SUNLIGHT:
				if (!PlayerCannotUseSkills && P_SKILL(P_IMPLANTS) >= P_GRAND_MASTER) pline("An implant that gives %d points of AC and the %s enchantment.", objects[IMPLANT_OF_SUNLIGHT].a_ac, enchname(objects[IMPLANT_OF_SUNLIGHT].oc_oprop));
				else pline("An implant that gives %d points of AC. It also gives an enchantment, but your skill isn't high enough to recognize it.", objects[IMPLANT_OF_SUNLIGHT].a_ac); break;

			case IMPLANT_OF_TSUNAMI:
				if (!PlayerCannotUseSkills && P_SKILL(P_IMPLANTS) >= P_GRAND_MASTER) pline("An implant that gives %d points of AC and the %s enchantment.", objects[IMPLANT_OF_TSUNAMI].a_ac, enchname(objects[IMPLANT_OF_TSUNAMI].oc_oprop));
				else pline("An implant that gives %d points of AC. It also gives an enchantment, but your skill isn't high enough to recognize it.", objects[IMPLANT_OF_TSUNAMI].a_ac); break;

			case IMPLANT_OF_FREEDOM:
				if (!PlayerCannotUseSkills && P_SKILL(P_IMPLANTS) >= P_GRAND_MASTER) pline("An implant that gives %d points of AC and the %s enchantment.", objects[IMPLANT_OF_FREEDOM].a_ac, enchname(objects[IMPLANT_OF_FREEDOM].oc_oprop));
				else pline("An implant that gives %d points of AC. It also gives an enchantment, but your skill isn't high enough to recognize it.", objects[IMPLANT_OF_FREEDOM].a_ac); break;


			default: pline("Missing item description (this is a bug). Please tell Amy about the item in question so she can add a description."); break;
			}
		}

		break;

		case AMULET_CLASS:


		if (obj->otyp == FAKE_AMULET_OF_YENDOR || obj->otyp == AMULET_OF_YENDOR) {
		pline("This is the amulet of Yendor, a very powerful talisman that radiates power. In order to ascend with it, you need to fully imbue it, which is done in the Yendorian Tower, accessed by a portal from Moloch's Sanctum. Find and use all three special staircases in the Yendorian Tower to imbue the Amulet."); break;

		} else {
#ifdef PHANTOM_CRASH_BUG
		pline("%s - Color: %s. Material: %s. Appearance: %s.",xname(obj), obj->dknown ? c_obj_colors[objects[obj->otyp].oc_color] : "unknown", obj->dknown ? materialnm[objects[obj->otyp].oc_material] : "unknown", obj->dknown ? dn : "unknown");
#else
		pline("%s - This is an amulet. Color: %s. Material: %s. Appearance: %s. It can be worn for some magical effect, but you will go hungry a little bit faster if you are wearing an amulet.",xname(obj), obj->dknown ? c_obj_colors[objects[obj->otyp].oc_color] : "unknown", obj->dknown ? materialnm[objects[obj->otyp].oc_material] : "unknown", obj->dknown ? dn : "unknown");
#endif
		}

		if (!nn) pline("Unfortunately you don't know more about it. You will gain more information if you identify this item.");
		else { switch (obj->otyp) {

			case AMULET_OF_CHANGE:
				pline("Wearing this amulet causes you to become female if you were male, and in reverse. The amulet will then disintegrate."); break;
			case AMULET_OF_POLYMORPH:
				pline("Wearing this amulet causes you to polymorph. The amulet will then disintegrate."); break;
			case AMULET_OF_TECHNICALITY:
				pline("While this amulet is worn, your effective technique levels will be 33%% higher and +3, making them much more powerful."); break;
			case AMULET_OF_STONE:
				pline("This amulet turns you to stone if you wear it."); break;
			case AMULET_OF_MAP_AMNESIA:
				pline("While you have this amulet around your neck, you're unable to remember the current level map."); break;
			case AMULET_OF_DEPRESSION:
				pline("Putting on this amulet turns positive luck and alignment record negative."); break;
			case AMULET_OF_DRAIN_RESISTANCE:
				pline("This amulet gives level-drain resistance if worn."); break;
			case AMULET_OF_ESP:
				pline("An amulet of extra-sensory perception, a.k.a. telepathy."); break;
			case AMULET_OF_UNDEAD_WARNING:
				pline("If you wear this amulet, you can detect the presence of undead."); break;
			case AMULET_OF_OWN_RACE_WARNING:
				pline("If you wear this amulet, you can detect the presence of monsters that are the same race as you."); break;
			case AMULET_OF_POISON_WARNING:
				pline("If you wear this amulet, you can detect the presence of poisonous monsters."); break;
			case AMULET_OF_COVETOUS_WARNING:
				pline("If you wear this amulet, you can detect the presence of covetous monsters."); break;
			case AMULET_OF_FLYING:
				pline("Wearing this amulet allows the wearer to fly."); break;
			case AMULET_OF_LIFE_SAVING:
				pline("You can survive death once if you wear this amulet. It will disintegrate if it saves your life."); break;
			case AMULET_OF_MAGICAL_BREATHING:
				pline("An amulet that allows you to survive without air if you wear it."); break;
			case AMULET_OF_REFLECTION:
				pline("This amulet can reflect beams and other nasty things while worn."); break;
			case AMULET_OF_PRISM:
				pline("A reflecting amulet that makes beams hitting you bounce off in a 90-degree angle."); break;
			case AMULET_OF_SPEED:
				pline("This wonderful amulet grants you more actions per turn."); break;
			case AMULET_OF_TELEPORT_CONTROL:
				pline("You can control the destination of your teleports while wearing this."); break;
			case AMULET_OF_POLYMORPH_CONTROL:
				pline("Wear this, and you will be able to control your polymorphs."); break;
			case AMULET_OF_WARP_DIMENSION:
				pline("This amulet reflects beams in a completely random direction."); break;
			case AMULET_OF_D_TYPE_EQUIPMENT:
				pline("A useful amulet that grants fire resistance and also allows you to swim in lava."); break;
			case AMULET_VERSUS_DEATH_SPELL:
				pline("It's weaker than magic resistance, but this amulet protects you from death rays and the touch of death."); break;
			case AMULET_OF_QUICK_ATTACK:
				pline("You'll attack twice per round while wearing this amulet. It also slows you down to two thirds of your normal movement speed."); break;
			case AMULET_OF_QUADRUPLE_ATTACK:
				pline("If you want killing power at the expense of movement speed, this amulet is for you: it allows you to attack four times as often but your speed is halved."); break;
			case PENDANT:
				pline("Unlike Castle of the Winds, this amulet does not give you cold resistance when worn. In fact, wearing it doesn't do anything at all."); break;
			case NECKLACE:
				pline("A plain amulet that doesn't have any special use."); break;
			case AMULET_OF_RESTFUL_SLEEP:
				pline("You will fall asleep if you wear this amulet. It is usually generated cursed."); break;
			case AMULET_OF_NECK_BRACE:
				pline("Wearing this amulet protects you from Vorpal Blade and other decapitating artifacts."); break;
			case AMULET_OF_BLINDNESS:
				pline("Wearing this amulet prevents you from seeing. It is usually generated cursed."); break;
			case AMULET_OF_STRANGULATION:
				pline("If you wear this amulet, you only have 5 turns to live before it kills you. It is usually generated cursed."); break;
			case AMULET_OF_PREMATURE_DEATH:
				pline("Wanna die? Wear this! :-P"); break;
			case AMULET_VERSUS_CURSES:
				pline("This amulet, when worn, is one of very few ways to resist the generic 'curse items' effect. It has been invented by Chris_ANG."); break;
			case AMULET_OF_UNCHANGING:
				pline("This amulet prevents you from changing form. If something tries to force you out of a polymorphed form while wearing this amulet, you might die instantly."); break;
			case AMULET_VERSUS_POISON:
				pline("An amulet that grants poison resistance when worn."); break;
			case AMULET_VERSUS_STONE:
				pline("Wearing this amulet can save you from petrification, but every time it does, it will degrade."); break;
			case AMULET_OF_DEPETRIFY:
				pline("If you wear this amulet, you will be petrification resistant."); break;
			case AMULET_OF_MAGIC_RESISTANCE:
				pline("A very useful amulet that grants magic resistance to the wearer."); break;
			case AMULET_OF_SICKNESS_RESISTANCE:
				pline("You will be immune to sickness as long as you wear this amulet."); break;
			case AMULET_OF_SWIMMING:
				pline("Wear this amulet if you want to be able to swim in water."); break;
			case AMULET_OF_RMB_LOSS:
				pline("This amulet causes your right mouse button to stop working."); break;
			case AMULET_OF_PEACE:
				pline("While wearing this amulet, peaceful monsters have a green background so you can instantly see that they're not hostile."); break;
			case AMULET_OF_EXPLOSION:
				pline("This amulet causes devices to explode."); break;
			case AMULET_OF_WRONG_SEEING:
				pline("This amulet causes books to be read incorrectly."); break;
			case AMULET_OF_WEAK_MAGIC:
				pline("This amulet weakens some magical effects used by you."); break;
			case AMULET_OF_DIRECTIONAL_SWAP:
				pline("This amulet causes totter."); break;
			case AMULET_OF_SUDDEN_CURSE:
				pline("This amulet causes items to autocurse whenever you drop them."); break;
			case AMULET_OF_ANTI_EXPERIENCE:
				pline("This amulet makes you lose the ability to gain experience."); break;
			case AMULET_OF_HOSTILITY:
				pline("This amulet causes all newly generated monsters to be hostile."); break;
			case AMULET_OF_EVIL_CRAFTING:
				pline("This amulet can put evil artifacts into your inventory and force you to equip them."); break;
			case AMULET_OF_EDIBILITY:
				pline("This amulet allows all monsters to eat any and all items they walk over."); break;
			case AMULET_OF_WAKING:
				pline("This amulet can cause peaceful monsters to spontaneously become hostile."); break;
			case AMULET_OF_TRASH:
				pline("This amulet decreases the enchantment value of your equipment every time you put something on."); break;
			case AMULET_OF_UNDRESSING:
				pline("This amulet randomly causes you to take off items."); break;
			case AMULET_OF_STARLIGHT:
				pline("This amulet makes it impossible to tell what monsters are."); break;
			case AMULET_OF_SCREWY_INTERFACE:
				pline("This amulet causes an incredibly nasty interface screw where you need to press Ctrl-R to see what actually happened in-game."); break;
			case AMULET_OF_BONES:
				pline("This amulet makes it much more likely that you find bones files, provided some exist at all. If you die while wearing it and are on an eligible level, you will always leave a bones file too."); break;
			case AMULET_OF_SPELL_FORGETTING:
				pline("This amulet makes your spells lose memory 10 times faster."); break;
			case AMULET_OF_ANTI_TELEPORTATION:
				pline("This amulet blocks all of your attempts to self-teleport."); break;
			case AMULET_OF_ITEM_TELEPORTATION:
				pline("This amulet causes items to teleport out of your inventory."); break;
			case AMULET_OF_DISINTEGRATION_RESIS:
				pline("This amulet grants disintegration resistance while worn."); break;
			case AMULET_OF_ACID_RESISTANCE:
				pline("Wearing this amulet causes you to be resistant to acid. This resistance doesn't protect your inventory from acid damage though."); break;
			case AMULET_OF_REGENERATION:
				pline("An amulet that increases your hit point regeneration when worn. It increases your food consumption rate."); break;
			case AMULET_OF_CONFLICT:
				pline("As long as you wear this amulet, monsters may sometimes attack each other. It greatly increases your food consumption rate."); break;
			case AMULET_OF_FUMBLING:
				pline("Wearing this amulet causes you to fumble. It is usually generated cursed."); break;
			case AMULET_OF_VULNERABILITY:
				pline("Don't wear this amulet unless you want to take much more damage by everything. It will autocurse when worn."); break;
			case AMULET_OF_SECOND_CHANCE:
				pline("A weaker version of the amulet of life saving that allows you to survive a deadly hit without restoring you to full hit points."); break;
			case AMULET_OF_DATA_STORAGE:
				pline("This amulet does nothing when worn."); break;
			case AMULET_OF_WATERWALKING:
				pline("You can walk on water if you wear this amulet."); break;
			case AMULET_OF_HUNGER:
				pline("This amulet increases your food consumption when worn. It is usually generated cursed."); break;
			case AMULET_OF_POWER:
				pline("A magical amulet that grants energy regeneration if you wear it."); break;
			case AMULET_OF_INSOMNIA:
				pline("Considering that this amulet conveys sleep resistance (which is rather mundane), it's quite rare."); break;
			case AMULET_OF_MENTAL_STABILITY:
				pline("Put on this amulet to get confusion resistance."); break;
			case AMULET_OF_CONTAMINATION_RESIST:
				pline("This rare amulet greatly reduces the effect of contamination on you."); break;

			case AMULET_OF_INFINITY: 
				pline("This is a special amulet, because nobody knows the effect in advance - but you now know that it is %s!", enchname(objects[AMULET_OF_INFINITY].oc_oprop) ); break;
			case AMULET_OF_THE_RNG: 
				pline("This is a special amulet, because nobody knows the effect in advance - but you now know that it is %s!", enchname(objects[AMULET_OF_THE_RNG].oc_oprop) ); break;
			case AMULET_OF_LEECH:
				pline("Wear this amulet to get manaleech!"); break;
			case AMULET_OF_DANGER:
				pline("Team Nastytrap made this amulet specially for you. Wear it at your own peril."); break;

 			default: pline("Missing item description (this is a bug). Please tell Amy about the item in question so she can add a description."); break;

			}

		}
		break;

		case TOOL_CLASS:
#ifdef PHANTOM_CRASH_BUG
		pline("%s - Color: %s. Material: %s. Appearance: %s.",xname(obj), obj->dknown ? c_obj_colors[objects[obj->otyp].oc_color] : "unknown", (nn && obj->dknown) ? materialnm[objects[obj->otyp].oc_material] : "unknown", obj->dknown ? dn : "unknown");
#else
		pline("%s - This is a tool. Color: %s. Material: %s. Appearance: %s. Most tools can be applied for an effect; some are also useful when wielded.",xname(obj), obj->dknown ? c_obj_colors[objects[obj->otyp].oc_color] : "unknown", (nn && obj->dknown) ? materialnm[objects[obj->otyp].oc_material] : "unknown", obj->dknown ? dn : "unknown");
#endif
		if (!nn) pline("Unfortunately you don't know more about it. You will gain more information if you identify this item.");
		else { switch (obj->otyp) {

			case LARGE_BOX: 
				pline("A big container that can hold items."); break;
			case TREASURE_CHEST:
				pline("It is usually generated with a lot of contents, but it's also really heavy. How did you manage to pick it up?"); break;
			case LARGE_BOX_OF_DIGESTION: 
				pline("Looks like an ordinary large box, until you open it to discover that all the contents have just disappeared!"); break;
			case CHEST: 
				pline("A treasure chest that may be filled with loot."); break;
			case CHEST_OF_HOLDING: 
				pline("This special treasure chest can decrease the weight of stuff in it. There are a few items that you may not put in though, or it will explode."); break;
			case ICE_BOX: 
				pline("This container has the unique ability to keep corpses fresh if they're put in."); break;
			case ICE_BOX_OF_HOLDING: 
				pline("A container that keeps corpses fresh and also makes them weigh less. Careful, certain items may not be put into it or it will explode."); break;
			case ICE_BOX_OF_WATERPROOFING: 
				pline("You can store corpses in this container, but also other items. The contents will not get wet if you fall into the water either."); break;
			case ICE_BOX_OF_DIGESTION: 
				pline("This box keeps corpses fresh, but also eats them sometimes. Other items can also disappear if you put them into it."); break;
			case SECRET_KEY:
				pline("It can be applied to fiddle with locks."); break;
			case HAIRCLIP:
				pline("A tool that you can apply to open things that are locked, or close them if they have an open lock."); break;
			case DATA_CHIP:
				pline("You can use this tool to open locks, but you cannot lock them again with it."); break;
			case GRASS_WHISTLE:
				pline("Applying it will wake up monsters and tell your pets to follow you."); break;
			case FOG_HORN:
				pline("Applying it will wake up and scare monsters, although they can resist the latter, and it also causes negative status effects to you."); break;
			case CONGLOMERATE_PICK:
				pline("This pick-axe can be applied to dig through walls, boulders or the floor, and it does a little more damage than a regular pick-axe."); break;
			case BRONZE_PICK:
				pline("This one-handed pick-axe does relatively good damage for its class, but its real use is to dig through walls or other obstacles, which is done by applying it."); break;
			case GUITAR:
				pline("A heavy two-handed weapon that uses the unicorn horn skill. Applying it will play music."); break;
			case PIANO:
				pline("Wielding this thing with two hands allows you to deal great damage to enemies. It uses the unicorn horn skill, but unlike an actual unicorn horn it plays music rather than curing status effects. Maybe it'll allow you to open the drawbridge."); break;
			case RADIOGLASSES:
				pline("These lenses will help your searching abilities while worn, and also display random rumors from time to time."); break;
			case EYECLOSER:
				pline("It can be applied to cause blindness. While you are wearing it, you will also be stealthy."); break;
			case BOSS_VISOR:
				pline("Wearing these lenses will not only improve your chances to find something when using the search command, it also displays covetous monsters!"); break;
			case DRAGON_EYEPATCH:
				pline("A blindfold that grants reflection when worn. Put it on to blind yourself, and take it off to stop the blindness."); break;
			case SOFT_CHASTITY_BELT:
				pline("This condome keeps your penis or other sexual organ safe while having a sexual encounter. It also reduces the amount of physical damage you take."); break;
			case BINNING_KIT:
				pline("If this tool has charges, you can apply it to dispose of corpses. Into the trash it goes! :D"); break;
			case BUDO_NO_SASU:
				pline("Wielding this tool allows you to open tins more quickly, and if you're a supermarket cashier, you might want to use it as a weapon."); break;
			case LUBRICANT_CAN:
				pline("A charged tool that can be applied to grease your stuff. Careful, it's difficult to handle and you will occasionally hurt yourself."); break;
			case SACK: 
				pline("This is a basic container that can be used to store items."); break;
			case OILSKIN_SACK:
				pline("A useful container that protects its contents from water."); break;
			case BAG_OF_HOLDING: 
				pline("Items that are in this container have an altered weight. Be careful - nesting bags of holding will cause them to explode, and there are certain items that may not be put in either."); break;
			case BAG_OF_DIGESTION: 
				pline("If you want to get rid of unneccessary items, put them into this container and they may disappear."); break;
			case BAG_OF_TRICKS: 
				pline("A bag that cannot be used for storing items. Instead, it creates monsters when applied."); break;
			case SKELETON_KEY: 
				pline("A key that can be used for locking and unlocking doors as well as certain containers."); break;
			case LOCK_PICK: 
				pline("This tool can be used on locks to open them."); break;
			case CREDIT_CARD: 
				pline("Using this card on a lock has a chance to open it."); break;
			case TALLOW_CANDLE: 
				pline("A light source that will burn up after a certain amount of time."); break;
			case WAX_CANDLE: 
				pline("This candle can be lit to provide some light radius for a while."); break;
			case JAPAN_WAX_CANDLE:
				pline("A candle that can be lit."); break;
			case OIL_CANDLE:
				pline("Yet another candle that you can light to, well, get light."); break;
			case UNAFFECTED_CANDLE:
				pline("It's just a standard candle with a nonstandard name."); break;
			case SPECIFIC_CANDLE:
				pline("This candle isn't all that specific actually, and will burn for a while if you light it."); break;
			case __CANDLE:
				pline("Don't ask why it's called that. It is functionally identical to the other non-magical candles."); break;
			case GENERAL_CANDLE: 
				pline("This candle is rare but not really different from other types of candle - light it to see in the dark."); break;
			case NATURAL_CANDLE: 
				pline("A candle made of 100%% natural materials that you can light."); break;
			case UNSPECIFIED_CANDLE: 
				pline("No one really knows what this candle is made of but it can be lit."); break;
			case MAGIC_CANDLE: 
				pline("A permanent light source that might be useful in dark areas."); break;
			case OIL_LAMP: 
				pline("This lamp can be lit to provide a big radius of light for a while. Oil runs out after some time but can be refilled."); break;
			case BRASS_LANTERN: 
				pline("A mobile light source that lasts for quite a while."); break;
			case MAGIC_LAMP: 
				pline("This lamp won't ever go out, and according to certain fairy tales, it might be inhabited by a genie."); break;
			case TIN_WHISTLE: 
				pline("Supposed to make your pets follow more closely, but it rarely does anything."); break;
			case MAGIC_WHISTLE: 
				pline("One blow of this powerful whistle will instantly summon all your pets."); break;
			case DARK_MAGIC_WHISTLE: 
				pline("An evil whistle that sends your pets away from you, but it only works on those adjacent to you."); break;
			case WOODEN_FLUTE: 
				pline("If you're good enough at it, you may use this instrument to calm snakes."); break;
			case MAGIC_FLUTE: 
				pline("An instrument that generates charming sounds to lull your enemies into sleeping."); break;
			case TOOLED_HORN: 
				pline("A noisy instrument that will wake up monsters. Sometimes it will scare them, too. But it will always cause nasty side effects for you so be careful!"); break;
			case FROST_HORN: 
				pline("This instrument can shoot bolts of ice."); break;
			case TEMPEST_HORN: 
				pline("This instrument can shoot bolts of electricity."); break;
			case FIRE_HORN: 
				pline("This instrument can shoot bolts of fire."); break;
			case HORN_OF_PLENTY: 
				pline("A magic horn that generates food."); break;
			case WOODEN_HARP: 
				pline("You may be able to charm nymphs by playing this harp."); break;
			case MAGIC_HARP: 
				pline("This powerful instrument can be played to tame adjacent monsters."); break;
			case BELL: 
				pline("A non-tonal instrument that can be used to make some noise."); break;
			case BUGLE:
				pline("This instrument can be played to wake up soldiers."); break;
			case LEATHER_DRUM: 
				pline("Using this instrument causes nearby monsters to wake up. It also makes noise that causes you to become deaf for a while."); break;
			case DRUM_OF_EARTHQUAKE: 
				pline("A magic drum that causes the entire dungeon level to shake violently, creating lots of pits."); break;
			case LAND_MINE: 
				pline("A portable trap that can be set to explode if an enemy steps on it."); break;
			case BEARTRAP: 
				pline("A portable trap that can be set to prevent enemies from moving."); break;
			case SPOON: 
				pline("This tool is also a weapon that can be thrown. It uses the dart skill."); break;
			case PICK_AXE: 
				pline("A tool that can also be used as a weapon. It can be applied for digging."); break;
			case FISHING_POLE: 
				pline("This polearm weapon-tool can be applied to catch fish."); break;
			case GRAPPLING_HOOK: 
				pline("A flail-type weapon-tool that can be used to pull objects and monsters toward you."); break;
			case UNICORN_HORN: 
				pline("The unicorn horn can be used as a two-handed melee weapon that uses its own skill, and applying it can cure a variety of bad effects."); break;
			case TORCH: 
				pline("A tool that counts as a club for in-game purposes; unfortunately a lit torch must be wielded in order to work, which makes it a very useless item."); break;
			case GREEN_LIGHTSABER: 
				pline("This lightsaber does solid damage, but it needs to be turned on in order to work. An activated lightsaber is good for engraving."); break;
			case BLUE_LIGHTSABER: 
				pline("This lightsaber does good damage, but it needs to be turned on in order to work. An activated lightsaber is good for engraving."); break;
			case RED_LIGHTSABER: 
				pline("This lightsaber does random damage, but it needs to be turned on in order to work. An activated lightsaber is good for engraving."); break;
			case RED_DOUBLE_LIGHTSABER: 
				pline("A two-handed lightsaber that can be set to double mode in order to do even more damage. It needs to be turned on in order to work, and while activated it is also useful for engraving. It's especially effective against large monsters."); break;
			case YELLOW_LIGHTSABER: 
				pline("This lightsaber does randomized damage, but it needs to be turned on in order to work. An activated lightsaber is good for engraving."); break;
			case VIOLET_LIGHTSABER: 
				pline("This lightsaber does steady damage, but it needs to be turned on in order to work. An activated lightsaber is good for engraving."); break;
			case WHITE_LIGHTSABER: 
				pline("This lightsaber does RNG-determined damage, but it needs to be turned on in order to work. An activated lightsaber is good for engraving."); break;
			case WHITE_DOUBLE_LIGHTSABER: 
				pline("A two-handed lightsaber that can be set to double mode in order to do even more damage. It needs to be turned on in order to work, and while activated it is also useful for engraving. It's especially effective against small monsters."); break;
			case LASER_SWATTER: 
				pline("A laser-based fly swatter that uses the paddle skill. If you turn it on, it will do great damage to small monsters."); break;
			case EXPENSIVE_CAMERA: 
				pline("A tool that can be used to photograph monsters."); break;
			case MIRROR: 
				pline("Some monsters may be scared if you apply a mirror at them, and gaze-based attacks may be reflected."); break;
			case CRYSTAL_BALL: 
				pline("Applying a crystal ball can be dangerous, but if it works, you may search for a glyph."); break;
			case LENSES: 
				pline("A weird tool that can be put on to improve the player's ability to search for things."); break;
			case BLINDFOLD: 
				pline("Putting on this tool prevents you from seeing."); break;
			case CONDOME: 
				pline("Wear this to prevent diseases from sexual encounters!"); break;
			case TOWEL: 
#ifdef PHANTOM_CRASH_BUG
				pline("Possible uses include: covering your eyes, wiping your hands, throwing it at a monster or wielding it as a melee weapon."); break;
#else
				pline("According to Douglas Adams, you can do a lot of stuff with a towel. Possible uses include: covering your eyes, wiping your hands, throwing it at a monster or wielding it as a melee weapon. See for yourself if you find any of these useful. :-)"); break;
#endif
			case LEATHER_SADDLE: 
				pline("Applying this at a tame monster may allow you to ride it. The more tame a monster is, the more likely you are to succeed in saddling it."); break;
			case INKA_SADDLE: 
				pline("This saddle allows for easy riding of tame monsters if you apply it. Careful: getting off your steed will have negative consequences."); break;
			case UNSTABLE_STETHOSCOPE: 
				pline("A stethoscope that occasionally reveals more information."); break;
			case LEATHER_LEASH: 
				pline("This tool can be applied at a tame monster to force it to follow you. Or that's what it *should* do, if pets weren't so goddamn stupid."); break;
			case INKA_LEASH: 
				pline("This tool can be applied at a tame monster to force it to follow you. If the pet falls behind, it attempts to teleport to you."); break;
			case STETHOSCOPE: 
				pline("This useful tool can be applied at monsters, objects and other things to find out more about them."); break;
			case TINNING_KIT: 
#ifdef PHANTOM_CRASH_BUG
				pline("Turns corpses into tins that you can eat. It neutralizes some of the corpse's effects while keeping some of the other ones."); break;
#else
				pline("If you want to get rid of corpses, apply this tool. It will also generate tins containing some of the monster's remains, neutralizing some bad effects like rotting or poison, but a tin of cockatrice meat will still turn you to stone."); break;
#endif
			case MEDICAL_KIT: 
#ifdef PHANTOM_CRASH_BUG
				pline("Apply it to swallow a pill. The draw blood and surgery techniques work better if you have this item."); break;
#else
				pline("A bag filled with medical tools. Applying it will cause you to swallow a pill and feel deathly sick, or sometimes you can get other effects as well. Some techniques, e.g. draw blood and surgery, will work better if you have this item."); break;
#endif
			case TIN_OPENER: 
				pline("A tool that must be wielded in order to work. It allows you to open tins more quickly. Some players use it to kill Vlad but that's not a good idea in Slash'EM Extended."); break;
			case CAN_OF_GREASE: 
#ifdef PHANTOM_CRASH_BUG
				pline("You can grease your items with this tool, and every item can have up to three layers of grease."); break;
#else
				pline("Despite seeming so mundane, this tool is actually rare and valuable as it allows you to grease your items. However, grease will wear off quickly and needs to be applied again. You can apply up to three layers of grease to a single item."); break;
#endif
			case FIGURINE: 
				pline("Apply this at an empty location to transform it into a living monster. Please don't apply a figurine at a square containing a monster; doing so will just cause the figurine to break and do nothing!"); break;
			case MAGIC_MARKER: 
#ifdef PHANTOM_CRASH_BUG
				pline("You can engrave with this tool, or attempt to write scrolls or spellbooks if you have blank ones. Writing items that you know is guaranteed to work."); break;
#else
				pline("The magic marker is actually a sort of pen that can be used for engraving. If you have blank scrolls or spellbooks, you can also attempt to write something on them; for a better chance of success, try to write an item that you know."); break;
#endif
			case FELT_TIP_MARKER: 
				pline("A marker that is useful for writing graffiti on the floor."); break;
			case SWITCHER:
				pline("This metal box has a switch that can be pulled. What may happen if you do so?"); break;
			case INTELLIGENCE_PACK:
				pline("Using this tool will boost your intelligence by one point (make sure you're not wearing an item that gives sustain ability). If you're of the sustainer race, it boosts your wisdom instead."); break;
			case MATERIAL_KIT:
				pline("A tool that has a randomized material stored inside. Using it on an item will change the material of ALL instances of that base item, so if you e.g. use a leather material kit on a dagger, all daggers in the game will then be made of leather. Of course, using it on the 'dagger' base item only affects that particular item type and not e.g. the 'orcish dagger' base item. If it successfully changes an item's material, the kit is used up."); break;
			case CHARGER:
				pline("You can apply this tool to charge one item (wand, ring, chargeable tool). It always gives the uncursed charging effect, regardless of the charger's BUC, and can only be used once."); break;
			case HITCHHIKER_S_GUIDE_TO_THE_GALA: 
				pline("A very complicated-looking device. Better not mess around with it..."); break;
			case DIODE: 
				pline("It's a two-wired piece of metal. Nobody knows if it's good for anything."); break;
			case TRANSISTOR: 
				pline("It's a three-wired piece of metal. Nobody knows if it's good for anything."); break;
			case IC: 
				pline("It's a many-wired piece of metal. Nobody knows if it's good for anything."); break;
			case PACK_OF_FLOPPIES: 
				pline("Only characters who have a lot of knowledge about computers may be able to use this item."); break;
			case GOD_O_METER: 
				pline("Using this device can give you a clue about your current standing with your god."); break;
			case RELAY: 
				pline("It's a four-wired piece of metal. Nobody knows if it's good for anything."); break;
			case BOTTLE: 
				pline("An empty bottle that can be filled if you have a chemistry set."); break;
			case CHEMISTRY_SET:
				pline("You can try to create your own potions with this. It requires an empty bottle to work; having the chemistry spell helps, too."); break;
			case BANDAGE:
				pline("A pseudo tool that actually can't exist outside of medical kits. It is used for the surgery technique."); break;
			case PHIAL:
				pline("A pseudo tool that actually can't exist outside of medical kits. It is used for the draw blood technique."); break;
			case CANDELABRUM_OF_INVOCATION:
				pline("Also called a menorah. This candelabrum can hold several candles. But without an imbued silver bell it doesn't work."); break;
			case BELL_OF_OPENING:
				pline("It's a silver bell that you can ring. But it only works if you imbued it, and for that you need to take it into the Bell Caves, the entrance to that is found in the Subquest, which itself is a branch accessed from the regular Quest."); break;

 			default: pline("Missing item description (this is a bug). Please tell Amy about the item in question so she can add a description."); break;

			}

		}
		break;

		case FOOD_CLASS:
#ifdef PHANTOM_CRASH_BUG
		pline("%s - Color: %s. Material: %s. Appearance: %s.",xname(obj), obj->dknown ? c_obj_colors[objects[obj->otyp].oc_color] : "unknown", obj->dknown ? materialnm[objects[obj->otyp].oc_material] : "unknown", obj->dknown ? dn : "unknown");
#else
		pline("%s - This is a comestible. Color: %s. Material: %s. Appearance: %s. It can be eaten.",xname(obj), obj->dknown ? c_obj_colors[objects[obj->otyp].oc_color] : "unknown", obj->dknown ? materialnm[objects[obj->otyp].oc_material] : "unknown", obj->dknown ? dn : "unknown");
#endif
		if (!nn) pline("Unfortunately you don't know more about it. You will gain more information if you identify this item.");
		else { switch (obj->otyp) {

			case TRIPE_RATION: 
				pline("A ration of dog food that's meant to be eaten by carnivorous pets."); break;
			case CORPSE: 
				pline("Corpses can be eaten, but it's not always a good idea to do so. Depending on the type of monster and the age of a corpse, different effects can occur."); break;
			case EGG: 
				pline("Eggs can be eaten, but some of them can also hatch after a while. Eating a stale egg causes vomiting."); break;
			case MEATBALL: 
				pline("These provide very little nutrition but can be used for training dogs."); break;
			case MEAT_STICK: 
				pline("A snack made of meat. Carnivorous pets like to eat these."); break;
			case HUGE_CHUNK_OF_MEAT: 
				pline("This is one of the most satiating comestibles in the game that provides lots of nutrition."); break;
			case MEAT_RING: 
				pline("A rarely seen type of comestible that doesn't actually do anything special."); break;
			case EYEBALL: 
				pline("You don't really want to eat this..."); break;
			case SEVERED_HAND: 
				pline("You don't really want to eat this..."); break;
			case KELP_FROND: 
				pline("A vegetarian food item that can be thrown to petty monsters in order to tame them."); break;
			case EUCALYPTUS_LEAF: 
				pline("Eating this item cures sickness and vomiting."); break;
			case CLOVE_OF_GARLIC: 
				pline("This is a type of food that can be eaten by vegetarians, and it can also be used to keep vampires away from you."); break;
			case SPRIG_OF_WOLFSBANE: 
				pline("If you're having trouble with lycanthropes infecting you, eating this vegetarian piece of food will cure you."); break;
			case APPLE: 
				pline("A vegetarian type of food that cures numbness."); break;
			case CARROT: 
				pline("A vegetarian type of food that cures blindness."); break;
			case PEAR: 
				pline("A vegetarian type of food that cures stunning."); break;
			case ASIAN_PEAR: 
				pline("A vegetarian type of food that cures stunning and confusion."); break;
			case LEMON: 
				pline("A vegetarian type of food that cures fear."); break;
			case BANANA: 
				pline("A vegetarian type of food that cures hallucination."); break;
			case ORANGE: 
				pline("A vegetarian type of food that cures freezing."); break;
			case MUSHROOM: 
				pline("Sometimes, you can get random effects from eating this vegetarian food item."); break;
			case MELON: 
				pline("A vegetarian type of food that cures confusion."); break;
			case SLIME_MOLD: 
				pline("This type of vegetarian food provides good nutrition and can be renamed. Default name is 'slime mold'."); break;
			case PEANUT_BAG: 
				pline("This vegetarian food item provides lots of nutrition."); break;
			case LUMP_OF_ROYAL_JELLY: 
				pline("Eating this vegetarian food item can increase your strength."); break;
			case CREAM_PIE: 
				pline("A vegetarian type of food that cures burns and dimness. It can also be thrown to blind enemies."); break;
			case SANDWICH: 
				pline("A meaty food item that provides moderate amounts of nutrition."); break;
			case CANDY_BAR: 
				pline("Eating this provides moderate amounts of nutrition without violating vegetarian conduct."); break;
			case FORTUNE_COOKIE: 
				pline("You may get a message if you eat this vegetarian food item."); break;
			case PANCAKE: 
				pline("This vegetarian type of food provides relatively good nutrition."); break;
			case UGH_MEMORY_TO_CREATE_INVENTORY: 
				pline("An edible item with an unknown effect. It might not be a good idea to eat it."); break;
			case TORTILLA: 
				pline("A rarely seen vegetarian food item that provides relatively little nutrition."); break;
			case TWELVE_COURSE_DINNER: 
				pline("A very filling meal, but it also takes a long time to consume."); break;
			case CHEESE: 
				pline("Can be used to tame rats. It can be eaten by vegetarians but not by vegans. Not that anyone is likely to care."); break;
			case PILL: 
				pline("Swallowing this thing is like playing russian roulette. You may get lucky and experience a good effect but you might also get something really bad instead."); break;
			case HOLY_WAFER: 
				pline("A vegetarian type of food that is relatively filling and can cure certain negative effects."); break;
			case LEMBAS_WAFER: 
				pline("A type of 'elven' bread that is more filling than any real-world vegetarian food can ever be."); break;
			case CRAM_RATION: 
				pline("A bland ration of vegetarian food."); break;
			case FOOD_RATION: 
				pline("A very filling ration of food. For some weird reason this counts as vegetarian food even though we all know that in real life, only food containing meat can ever satiate your stomach."); break;
			case HACKER_S_FOOD: 
				pline("This vegetarian food ration can be eaten in one turn."); break;
			case K_RATION: 
				pline("Soldiers often carry these rations that can be eaten in one turn. For some reason they contain no meat - if I were a soldier in real life I'd be pissed if they won't serve meat!"); break;
			case C_RATION: 
				pline("Soldiers often carry these rations that can be eaten in one turn. For some reason they contain no meat - how can any real-life soldiers even concentrate on their tasks if they ain't getting no real food?"); break;
			case TIN: 
#ifdef PHANTOM_CRASH_BUG
				pline("Open it to see its contents, then decide whether you really want to eat it. They have variable amounts of nutrition."); break;
#else
				pline("A tin that may contain some type of food. If you wield a tin opener, you can open it more quickly; after a tin has been opened, you can decide whether you really want to eat it. The nutritional value of a tin is randomized."); break;
#endif

			case SHEAF_OF_STRAW: 
				pline("This food tastes better if you're a herbivore."); break;
			case COTTON: 
				pline("A food type that's suitable for herbivores."); break;
			case ONION: 
				pline("A tasty vegetarian food that currently does not make you blind."); break;
			case WELSH_ONION: 
				pline("It's some kind of special onion that can be eaten without breaking vegetarian conduct."); break;
			case WATERMELON: 
				pline("A bland type of vegetarian food."); break;
			case WHITE_PEACH: 
				pline("Careful, not everyone can eat this vegetarian food without problems."); break;
			case SENTOU: 
				pline("It's some japanese type of food that does not contain meat."); break;
			case BEAN: 
				pline("A vegetable that should be eaten only if you're not undead."); break;
			case SENZU: 
				pline("Hell if I know what a 'senzu' is supposed to be... might be a vegetable? And undead don't like to eat it? That's all I know!"); break;
			case PARFAIT: 
				pline("A sweet type of vegetarian food."); break;
			case X_MAS_CAKE: 
				pline("Jingle bells, jingle bells... or do you like to listen to Wham's Last Christmas instead? It's a big filling cake!"); break;
			case BUNNY_CAKE: 
				pline("Eating this vegetarian food can make you feel more experienced."); break;
			case BAKED_SWEET_POTATO: 
				pline("Yet another vegetable food item."); break;
			case BREAD: 
				pline("An alternative to the food ration."); break;
			case PASTA: 
				pline("If Italians didn't exist, they'd have to be invented. But thankfully they do exist, and they brought us this great food!"); break;
			case CHARRED_BREAD: 
				pline("This bread's no good! Eating it is not advised."); break;
			case SLICE_OF_PIZZA: 
				pline("Sure, a whole pizza might be a lot more filling, but it certainly tastes good anyway. Eating it will break vegan but not vegetarian conduct."); break;
			case WHITE_SWEETS: 
				pline("Some white sweets which are supposed to have a color-specific effect but someone forgot to code that part into the game."); break;
			case BROWN_SWEETS: 
				pline("Some brown sweets which are supposed to have a color-specific effect but someone forgot to code that part into the game."); break;
			case GREEN_SWEETS: 
				pline("Some green sweets which are supposed to have a color-specific effect but someone forgot to code that part into the game."); break;
			case PINK_SWEETS: 
				pline("Some pink sweets which are supposed to have a color-specific effect but someone forgot to code that part into the game."); break;
			case BLACK_SWEETS: 
				pline("Some black sweets which are supposed to have a color-specific effect but someone forgot to code that part into the game."); break;
			case YELLOW_SWEETS: 
				pline("Some yellow sweets which are supposed to have a color-specific effect but someone forgot to code that part into the game."); break;
			case BOTA_MOCHI: 
				pline("A Japanese type of food that's supposed to display messages."); break;
			case KIBI_DANGO: 
				pline("Someone has written messages for eating this type of food but they haven't been translated yet."); break;
			case SAKURA_MOCHI: 
				pline("There are messages associated to eating this item, but they're in Japanese..."); break;
			case KOUHAKU_MANJYUU: 
				pline("This vegetarian food can display a few messages if you eat it."); break;
			case YOUKAN: 
				pline("Eating this produces YAFM."); break;
			case CHOCOLATE: 
				pline("A piece of chocolate. Sometimes it may have a different taste."); break;
			case CHOCOEGG: 
				pline("Eating this chocolate egg can have special effects sometimes."); break;
			case WAKAME: 
				pline("Some weird japanese food."); break;
			case MAGIC_BANANA: 
				pline("Eating this can cure a wide variety of ailments."); break;
			case LUNCH_OF_BOILED_EGG: 
				pline("This food somehow counts as meat and doesn't give a lot of nutrition."); break;
			case PIZZA: 
#ifdef PHANTOM_CRASH_BUG
				pline("It probably isn't a doener pizza, since eating it doesn't break vegetarian conduct."); break;
#else
				pline("It probably isn't a doener pizza, since eating it doesn't break vegetarian conduct. Best served with french fries of course. Good appetite! Oh wait, that was a mistranslation. What's it you say in English? I forgot..."); break;
#endif

 			default: pline("Missing item description (this is a bug). Please tell Amy about the item in question so she can add a description."); break;

			}

		}
		break;

		case POTION_CLASS:
#ifdef PHANTOM_CRASH_BUG
		pline("%s - Color: %s. Material: %s. Appearance: %s.",xname(obj), obj->dknown ? c_obj_colors[objects[obj->otyp].oc_color] : "unknown", obj->dknown ? materialnm[objects[obj->otyp].oc_material] : "unknown", obj->dknown ? dn : "unknown");
#else
		pline("%s - This is a potion. Color: %s. Material: %s. Appearance: %s. You can quaff it to experience its effects, but it's also possible to throw potions at monsters or bash them with it in melee.",xname(obj), obj->dknown ? c_obj_colors[objects[obj->otyp].oc_color] : "unknown", obj->dknown ? materialnm[objects[obj->otyp].oc_material] : "unknown", obj->dknown ? dn : "unknown");
#endif
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && ( (!strcmp(OBJ_DESCR(objects[obj->otyp]), "milky") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "ghostly") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "hallowed") || !strcmp(OBJ_DESCR(objects[obj->otyp]), "spiritual"))))
			pline("Careful, sometimes a ghost may come out if you quaff this potion.");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && (!strcmp(OBJ_DESCR(objects[obj->otyp]), "smoky")))
			pline("A djinni may live in this potion.");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && (!strcmp(OBJ_DESCR(objects[obj->otyp]), "vapor")))
			pline("A dao may live in this potion.");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && (!strcmp(OBJ_DESCR(objects[obj->otyp]), "sizzling")))
			pline("An efreeti may live in this potion.");
		if (OBJ_DESCR(objects[obj->otyp]) && obj->dknown && (!strcmp(OBJ_DESCR(objects[obj->otyp]), "fuming")))
			pline("A marid may live in this potion.");

		if (!nn) pline("Unfortunately you don't know more about it. You will gain more information if you identify this item.");
		else { switch (obj->otyp) {

			case POT_BOOZE:
				pline("Wanna get high? Quaff this! It causes confusion and gives a little bit of nutrition."); break;
			case POT_FRUIT_JUICE:
				pline("A potion that provides some nutrition when quaffed."); break;
			case POT_CURE_WOUNDS:
				pline("Not stolen from Angband at all, this potion restores some health when quaffed."); break;
			case POT_CURE_SERIOUS_WOUNDS:
				pline("Not stolen from Angband at all, this potion restores a significant amount of health when quaffed."); break;
			case POT_CURE_CRITICAL_WOUNDS:
				pline("Not stolen from Angband at all, this potion restores a very large amount of health when quaffed."); break;
			case POT_PORTER:
				pline("Grants teleportitis."); break;
			case POT_WONDER:
				pline("This potion behaves like a random potion when quaffed."); break;
			case POT_TERCES_DLU:
				pline("A very weird potion, the effect of quaffing it can never be predicted."); break;
			case POT_HIDING:
				pline("A fake potion. Drinking it is usually a bad idea."); break;
			case POT_DECOY_MAKING:
				pline("It looks like a potion containing a liquid..."); break;
			case POT_DOWN_LEVEL:
				pline("Quaffing this potion can reduce your level."); break;
			case POT_KEEN_MEMORY:
				pline("You'll get temporary resistance to amnesia by quaffing this."); break;
			case POT_NIGHT_VISION:
				pline("Increases your sight radius for a period of time."); break;
			case POT_RESISTANCE:
				pline("Another potion stolen from Angband, this one grants resistances to fire, cold and shock for a while."); break;
			case POT_POISON:
				pline("The potion of poison is functionally very similar to a potion of sickness - don't drink it!"); break;
			case POT_COFFEE:
				pline("Provides a bit of nutrition and temporary sleep resistance."); break;
			case POT_RED_TEA:
				pline("Provides a bit of nutrition and temporary fire resistance."); break;
			case POT_OOLONG_TEA:
				pline("Provides a bit of nutrition and temporary shock resistance."); break;
			case POT_GREEN_TEA:
				pline("Provides a bit of nutrition and temporary cold resistance."); break;
			case POT_COCOA:
				pline("Provides a bit of nutrition and temporary disintegration resistance."); break;
			case POT_GREEN_MATE:
				pline("Provides a bit of nutrition and temporary poison resistance."); break;
			case POT_TERERE:
				pline("This drink provides good nutrition but also confuses you."); break;
			case POT_AOJIRU:
				pline("This drink provides good nutrition but also stuns you."); break;
			case POT_WINE:
				pline("Another form of booze. Quaff it for some minor nutrition, but it also causes confusion."); break;
			case POT_ULTIMATE_TSUYOSHI_SPECIAL:
				pline("You will become very fast and invulnerable for a few turns but also hallucinate for a while when quaffing this potion."); break;
			case POT_MEHOHO_BURUSASAN_G:
				pline("A poisonous japanese drink that probably contains tetrodotoxin. Only a fool would drink it."); break;
			case POT_TRAINING:
				pline("This very rare potion allows you to pick a skill and double the amount of training in it. It can even allow you to bypass the RNG-decreed limits, but it won't take you over the actual max of your skill (seen in the #enhance screen)."); break;
			case POT_BENEFICIAL_EFFECT:
				pline("Quaffing this potion gives a random good effect that can also be gotten from eating a corpse."); break;
			case POT_RANDOM_INTRINSIC:
				pline("If you quaff this potion, you will either gain or lose a random intrinsic, and the intrinsic in question can be good or bad. Unlike FIQhack's potion of wonder, this potion works the same regardless of bless/curse status, so you don't need to waste holy water on it."); break;
			case POT_SEE_INVISIBLE:
				pline("You can see invisible monsters and items if you quaff this."); break;
			case POT_SICKNESS:
				pline("A potion that should not be quaffed. Instead, dip weapons into it to poison them, or throw it at a monster."); break;
			case POT_SLEEPING:
				pline("Attacking a monster with this potion puts it to sleep. You can also quaff it yourself if you want to sleep for some reason."); break;
			case POT_CLAIRVOYANCE:
				pline("A potion that grants temporary clairvoyance if you quaff it."); break;
			case POT_CONFUSION:
				pline("You can drink this potion if you want to get confused, or hurl it at a monster instead."); break;
			case POT_HALLUCINATION:
				pline("Don't quaff this unless you want to hallucinate for hundreds of turns. Instead, throwing it at a monster will cause it to hallucinate for a while which might be much more useful."); break;
			case POT_HEALING:
				pline("A basic healing potion that restores a low amount of hit points. If the amount restored exceeds your maximum hit points, they will be increased."); break;
			case POT_EXTRA_HEALING:
				pline("A good healing potion that restores a medium amount of hit points and sometimes fixes other troubles as well. If the amount restored exceeds your maximum hit points, they will be increased."); break;
			case POT_RESTORE_ABILITY:
				pline("Quaffing this potion can restore lost attribute points."); break;
			case POT_BLINDNESS:
				pline("A potion that causes you to become blind if you quaff it. You can also fling it at opponents to blind them instead."); break;
			case POT_ESP:
				pline("This potion grants extra-sensory perception, a.k.a. telepathy if you quaff it."); break;
			case POT_GAIN_ENERGY:
				pline("A potion of mana that will also increase your maximum amount of mana. If the amount of mana restored exceeds the maximum, your maximum mana will go up even more."); break;
			case POT_GAIN_HEALTH:
				pline("This potion can be used for healing, but its main use is increasing your maximum health when quaffed."); break;
			case POT_BANISHING_FEAR:
				pline("A potion that will cure fear when quaffed."); break;
			case POT_FIRE_RESISTANCE:
				pline("This potion temporarily makes you resistant to fire."); break;
			case POT_DIMNESS:
				pline("Quaffing this potion causes dimness. You might want to throw it at a monster instead."); break;
			case POT_ICE:
				pline("You will freeze solid if you quaff this potion, which is usually a bad thing. Better use it as a missile to slow down enemies."); break;
			case POT_FEAR:
				pline("Anyone who breathes the fumes of this potion will become afraid of its surroundings."); break;
			case POT_FIRE:
				pline("If you quaff this potion, you'll suffer from burns. This means it's better used as a thrown potion to burn enemies."); break;
			case POT_STUNNING:
				pline("Drinking this potion will stun you. If it hits a monster, the monster will be stunned too."); break;
			case POT_NUMBNESS:
				pline("Your limbs will be numbed from quaffing this potion, so you're probably better off using it against a monster instead."); break;
			case POT_URINE:
				pline("Quaffing this potion is only fatal if it's more than 50 turns old."); break;
			case POT_SLIME:
				pline("You will slowly turn into a green slime if you quaff this potion."); break;
			case POT_CANCELLATION:
				pline("Similar to an amnesia potion, but this potion will cancel your items or remove your intrinsics."); break;
			case POT_INVISIBILITY:
				pline("This potion can make the one quaffing it invisible."); break;
			case POT_MONSTER_DETECTION:
				pline("Quaffing this potion shows all monsters on the current dungeon level to you."); break;
			case POT_OBJECT_DETECTION:
				pline("A potion that reveals objects on the current level if quaffed."); break;
			case POT_ENLIGHTENMENT:
				pline("When quaffed, this potion displays a lot of information about your character, including whether you can pray to your god."); break;
			case POT_FULL_HEALING:
				pline("The best healing potion in the game that always restores at least 400 hit points, in addition to fixing several other troubles. It's very likely to increase your maximum hit points too."); break;
			case POT_LEVITATION:
				pline("When quaffed, this potion causes you to levitate for a period of time."); break;
			case POT_POLYMORPH:
				pline("Quaffing this potion will polymorph you into a random monster, which may be a good or bad thing. You may also want to use this as a throwing 'weapon' in order to polymorph monsters."); break;
			case POT_MUTATION:
				pline("Quaffing this potion will polymorph you into a random monster, which may be a good or bad thing. If monsters quaff or get hit by it, they will gain mutations."); break;
			case POT_SPEED:
				pline("When quaffed, this potion makes you move much faster for a period of time."); break;
			case POT_ACID:
				pline("A very useful potion that cures petrification when quaffed. It has a variety of other uses too."); break;
			case POT_OIL:
				pline("You can quaff this potion, but it can also be used to refill oil lamps, disarm certain types of traps and more."); break;
			case POT_SALT_WATER:
				pline("This water is extremely salty. If you quaff it, you'll probably vomit, so it's best used if you are sick from food poisoning."); break;
			case POT_GAIN_ABILITY:
				pline("A beneficial potion that can increase your stats if you quaff it."); break;
			case POT_GAIN_LEVEL:
				pline("You will gain a level if you quaff this potion."); break;
			case POT_INVULNERABILITY:
				pline("A very powerful potion that makes you immune to damage for a short while if you quaff it."); break;
			case POT_PARALYSIS:
				pline("Quaffing this potion is probably a bad idea because it will paralyze you for some turns. The same can happen to monsters that are subjected to its vapors in some way."); break;
			case POT_EXTREME_POWER:
				pline("A potion that can increase your maximum amount of hit points when quaffed."); break;
			case POT_RECOVERY:
				pline("This healing potion instantly restores your health to its maximum."); break;
			case POT_HEROISM:
				pline("A potion that makes you super-powerful for a short while, but you might be blinded for a few turns if you quaff it."); break;
			case POT_CYANIDE:
				pline("Drinking this potion is a very bad idea indeed. Use it as a thrown weapon instead."); break;
			case POT_RADIUM:
				pline("You'll get sick if you try quaffing this liquid, but so might your enemies if you expose them to this potion's vapors."); break;
			case POT_JOLT_COLA:
				pline("A hacker's beverage that can make you feel a little better, in a variety of ways."); break;
			case POT_PAN_GALACTIC_GARGLE_BLASTE:
				pline("If you quaff this, you will feel like having your brain smashed out by a slice of lemon wrapped around a large gold brick."); break;
			case POT_WATER:
#ifdef PHANTOM_CRASH_BUG
				pline("Blessed = holy, cursed = unholy; dipping items in them changes their BUC status. Plain water also has some marginal uses."); break;
#else
				pline("Water potions behave differently if they are blessed (holy water) or cursed (unholy water). Quaffing (un)holy water has a variety of effects, and it will alter the blessed/cursed/uncursed status of items dipped into it. Plain water also has some marginal uses."); break;
#endif
			case POT_BLOOD:
				pline("A red liquid that is meant to be quaffed by vampires."); break;
			case POT_VAMPIRE_BLOOD:
				pline("Vampires love the taste of this potion, as it gives them nutrition and heals them. Non-vampires may become a vampire if they drink it."); break;
			case POT_AMNESIA:
#ifdef PHANTOM_CRASH_BUG
				pline("This potion causes you or monsters to forget things; for the latter, throw it at them."); break;
#else
				pline("The best item in the entire game. Throwing it at monsters can make them forget things, and quaffing a blessed one can cure your sickness and lycanthropy. Be careful though, as you might get hit by a nasty amnesia effect that wipes your memory."); break;
#endif

 			default: pline("Missing item description (this is a bug). Please tell Amy about the item in question so she can add a description."); break;

			}

		}
		break;

		case SCROLL_CLASS:
#ifdef PHANTOM_CRASH_BUG
		pline("%s - Color: %s. Material: %s. Appearance: %s.",xname(obj), obj->dknown ? c_obj_colors[objects[obj->otyp].oc_color] : "unknown", obj->dknown ? materialnm[objects[obj->otyp].oc_material] : "unknown", obj->dknown ? dn : "unknown");
#else
		pline("%s - This is a scroll. Color: %s. Material: %s. Appearance: %s. Reading it has a magic effect and uses up the scroll; some scroll effects are different if they are read while you are confused.",xname(obj), obj->dknown ? c_obj_colors[objects[obj->otyp].oc_color] : "unknown", obj->dknown ? materialnm[objects[obj->otyp].oc_material] : "unknown", obj->dknown ? dn : "unknown");
#endif
		if (!nn) pline("Unfortunately you don't know more about it. You will gain more information if you identify this item.");
		else { switch (obj->otyp) {

			case SCR_CREATE_MONSTER: 
				pline("Reading this scroll summons some monsters for you to fight."); break;
			case SCR_CREATE_VICTIM: 
				pline("Reading this scroll summons some monsters randomly on the level."); break;
			case SCR_CREATE_FAMILIAR: 
				pline("Reading this scroll summons a tame monster that will help you out."); break;
			case SCR_SUMMON_UNDEAD: 
				pline("This scroll summons undead monsters if read."); break;
			case SCR_TAMING: 
				pline("If you read this scroll, it tries to tame all adjacent monsters. Certain monsters may resist, and some boss monsters are outright immune to this effect."); break;
			case SCR_LIGHT: 
				pline("Illuminates the area around you."); break;
			case SCR_FOOD_DETECTION: 
				pline("Reading this scroll allows you to detect comestibles on the current level. It also fills your stomach a little."); break;
			case SCR_GOLD_DETECTION: 
				pline("All piles of gold on the entire level are revealed to you if you read this."); break;
			case SCR_IDENTIFY: 
				pline("You may identify one or more objects if you use this scroll."); break;
			case SCR_INVENTORY_ID: 
				pline("A powerful identify scroll that always identifies everything in your main inventory. Pick up as much as you can before reading it, and move your container's contents in your main inventory too!"); break;
			case SCR_MAGIC_MAPPING: 
				pline("This scroll can reveal the map of your current dungeon level. However, certain levels are unmappable."); break;
			case SCR_FLOOD: 
				pline("A dangerous scroll that creates water, possibly drowning you."); break;

			case SCR_FLOOD_TIDE: 
				pline("This scroll will flood the entire level if you read it."); break;
			case SCR_EBB_TIDE: 
				pline("In order to get rid of annoying water on the level, read this scroll."); break;
			case SCR_COPYING: 
				pline("This scroll copies the effect of any other random scroll when read."); break;
			case SCR_CREATE_FACILITY: 
				pline("Creates random terrain features on the level when read."); break;
			case SCR_ERASURE: 
				pline("A powerful scroll that can erase monsters on the current level."); break;
			case SCR_CURE_BLINDNESS: 
				pline("This scroll can be read to cure blindness, as ironic as that may sound."); break;
			case SCR_SKILL_UP: 
				pline("Grants the ability to learn new skills if you're lucky."); break;
			case SCR_GEOLYSIS:
				pline("Allows you to eat through rock for a while, transforming it into floor but also occasionally other types of terrain."); break;
			case SCR_DETECT_WATER: 
				pline("Detects all sources of water on the current dungeon level."); break;
			case SCR_FROST: 
				pline("A scroll that creates ice tiles on the level."); break;
			case SCR_CREATE_ALTAR: 
				pline("Creates an altar to Moloch underneath you. It only works if you're standing on a room or corridor square."); break;
			case SCR_CREATE_SINK: 
				pline("Creates a sink underneath you. It won't work if you're not standing on a room or corridor tile."); break;
			case SCR_SYMMETRY: 
#ifdef PHANTOM_CRASH_BUG
				pline("It will reverse your movement key's directions PERMANENTLY. You have to read another one if you want to reverse its effect."); break;
#else
				pline("This is a very dangerous scroll that will reverse your movement key's directions PERMANENTLY. The only way to reverse its effect is to read another one, so you should be glad I'm not allowing monsters to read them!"); break;
#endif
			case SCR_CREATE_CREATE_SCROLL: 
				pline("Fairly useless; reading this scroll will recreate it on the ground beneath you. Makes you wonder what the devs were smoking when they invented this item."); break;
			case SCR_ANTIMAGIC: 
				pline("This scroll makes you magic resistant for a while. For the entire duration no one can cast spells."); break;
			case SCR_RESISTANCE: 
				pline("Grants temporary resistances to fire, cold, shock, acid and sleep."); break;

			case SCR_GAIN_MANA: 
				pline("Reading this scroll will increase your max mana."); break;
			case SCR_CONFUSE_MONSTER: 
				pline("Your melee attacks have a chance to confuse monsters after reading this scroll. Also, it grants temporary confusion resistance."); break;
			case SCR_SCARE_MONSTER: 
				pline("Reading this scroll is a waste. Its real purpose is to lie on the ground, keeping monsters away from it. However, it degrades every time you pick it up."); break;
			case SCR_INSTANT_AMNESIA: 
				pline("You somehow managed to get this scroll into your inventory. Congratulations. Why don't you read it then? Maybe you'll think of Maud and forget everything else! :-)"); break;
			case SCR_ENCHANT_WEAPON: 
				pline("Your wielded weapon's enchantment goes up if you read this scroll. Beware, if the weapon's enchantment is +6 or higher, the weapon may blow up."); break;
			case SCR_ENCHANT_ARMOR: 
#ifdef PHANTOM_CRASH_BUG
				pline("Select an armor to enchant. Armors at +4 or higher may evaporate (elven armors will evaporate at +6 or higher)."); break;
#else
				pline("You may select one of your worn pieces of armor to increase its enchantment. Most pieces of armor have a chance to evaporate if they're already enchanted to +4 or higher. Elven armors won't evaporate unless they're at least +6 though."); break;
#endif
			case SCR_REMOVE_CURSE: 
				pline("This scroll can uncurse some of the items in your inventory if you read it."); break;
			case SCR_ALTER_REALITY: 
				pline("The game's rules will never be the same again..."); break;
			case SCR_TELEPORTATION: 
				pline("A scroll meant to be used in emergency situations that teleports you to a random empty location on the current dungeon level. Beware, some special levels inhibit teleportation!"); break;
			case SCR_TELE_LEVEL: 
				pline("This scroll will get you out of most sticky situations by warping you to another dungeon level."); break;
			case SCR_WARPING: 
				pline("You will warp to any random dungeon level if you read this scroll. It may deposit you at some fairly dangerous place, too."); break;
			case SCR_FIRE: 
#ifdef PHANTOM_CRASH_BUG
				pline("Reading it will burn you and adjacent monsters a little."); break;
#else
				pline("The best scroll in the game. You will need this to cure the sliming condition, which is difficult to cure otherwise. It can also damage monsters standing next to you, with the side effect of burning you a little."); break;
#endif
			case SCR_EARTH: 
				pline("Summons some boulders if read. Beware, they might hit your head and damage you."); break;
			case SCR_DESTROY_ARMOR: 
#ifdef PHANTOM_CRASH_BUG
				pline("It randomly destroys a worn piece of armor when read."); break;
#else
				pline("A scroll that can be used if you are wearing a cursed piece of armor and want to get rid of it. You can't select the affected piece of armor yourself though; rather, the game randomly destroys one of your worn armor items."); break;
#endif
			case SCR_DESTROY_WEAPON: 
				pline("Wanna get rid of your weapon? Now you can do so."); break;
			case SCR_AMNESIA: 
				pline("You will forget some of your spells as well as the current level's layout if you read this scroll."); break;
			case SCR_BAD_EFFECT: 
				pline("Causes a randomly selected bad effect if read."); break;
			case SCR_WARD: 
				pline("You will become temporarily resistant to physical damage when reading this scroll."); break;
			case SCR_WARDING: 
				pline("You will become temporarily resistant to spell damage when reading this scroll."); break;
			case SCR_WONDER: 
				pline("Reading this scroll teaches a random spell. If it rolls one that you already know, its spell memory is increased."); break;
			case SCR_HEALING:
				pline("A standard healing scroll that behaves similar to healing potions in other role-playing games by restoring some lost hit points. Don't bother trying to blank, cancel or polymorph this scroll, as that doesn't work."); break;
			case SCR_EXTRA_HEALING:
				pline("A powerful healing scroll that restores a large amount of lost hit points. Don't bother trying to blank, cancel or polymorph this scroll, as that doesn't work."); break;
			case SCR_POWER_HEALING:
				pline("This scroll completely restores hit points."); break;
			case SCR_MANA:
				pline("A standard mana scroll that behaves similar to mana potions in other role-playing games by restoring some of your mana. Don't bother trying to blank, cancel or polymorph this scroll, as that doesn't work."); break;
			case SCR_GREATER_MANA_RESTORATION:
				pline("A powerful mana scroll that restores a lot of your mana. Don't bother trying to blank, cancel or polymorph this scroll, as that doesn't work."); break;
			case SCR_CURE:
				pline("A powerful curing scroll that will fix the following status effects: sickness, sliming, stoning, confusion, blindness, stun, numbness, freezing, burn, fear, dimness and hallucination. Don't bother trying to blank, cancel or polymorph this scroll, as that doesn't work."); break;
			case SCR_PHASE_DOOR:
				pline("Using this scroll will teleport you over a short distance. Of course it doesn't work if you're on a no-teleport level. Don't bother trying to blank, cancel or polymorph this scroll, as that doesn't work."); break;
			case SCR_TRAP_DISARMING:
				pline("If you read this scroll, all traps in a 3x3 radius centered on you will be removed."); break;
			case SCR_STANDARD_ID:
				pline("Reading this scroll allows you to identify exactly one item in your main inventory. Don't bother trying to blank, cancel or polymorph this scroll, as that doesn't work."); break;
			case SCR_GROUP_SUMMONING:
				pline("Summons a group of themed monsters for you to fight."); break;
			case SCR_WORLD_FALL:
				pline("Also known as 'apocalypse' or 'cataclysm', this scroll wipes out all monsters on the level whose level is lower than yours, and ones with a higher level can sometimes be removed too."); break;
			case SCR_RESURRECTION:
				pline("Reading this scroll grants you an extra life, i.e. you come back to life after death!"); break;
			case SCR_SUMMON_GHOST:
				pline("You will summon a player ghost by reading this, which can be extremely dangerous."); break;
			case SCR_MEGALOAD:
				pline("A nasty scroll that puts a loadstone in your inventory if anyone reads it."); break;
			case SCR_VILENESS: 
				pline("If this is scroll is read, regardless of who is doing the reading, an evil artifact will be put into your inventory. You will then automatically equip it, and if the artifact didn't autocurse anyway, it will get cursed."); break;
			case SCR_ENRAGE: 
				pline("Peaceful monsters become hostile, and sometimes your tame pets too, should anyone read this scroll."); break;
			case SCR_ANTIMATTER: 
				pline("This scroll was put in the game by an evil developer and will damage your entire inventory."); break;
			case SCR_SUMMON_ELM: 
				pline("A scroll that can summon a divine minion. Unfortunately, the minion will attack you."); break;
			case SCR_RELOCATION: 
				pline("Useful for no-teleport levels, this scroll teleports you to an empty location on the level regardless of things that would normally prevent teleportation."); break;
			case SCR_IMMOBILITY: 
				pline("This scroll surrounds you with immobile monsters when read."); break;
			case SCR_FLOODING: 
				pline("Turns large parts of the current level into a lake of water and lava, with associated sea monsters and stuff."); break;
			case SCR_EGOISM: 
				pline("You will face a bunch of egotype monsters when reading this."); break;
			case SCR_RUMOR: 
				pline("A scroll that has a rumor written on it."); break;
			case SCR_ARTIFACT_JACKPOT:
				pline("This very rare scroll grants you an artifact and then randomizes the base item type of that artifact, so you might end up with something really awesome."); break;
			case SCR_BOSS_COMPANION:
				pline("Reading this scroll summons a boss monster. A tame one. Yes, you read that right. You'll get your personal boss monster that fights alongside you."); break;
			case SCR_MESSAGE: 
				pline("A scroll that can trigger messages if you read it."); break;
			case SCR_SIN: 
				pline("Don't read it. That is, unless you want to be hit by a 'deadly sin' effect which is likely to screw your character in some way or another."); break;
			case SCR_CHARGING: 
#ifdef PHANTOM_CRASH_BUG
				pline("You can recharge a chargeable item by reading this. Be careful, recharging an item too many times may cause it to explode."); break;
#else
				pline("This scroll can be read to charge an object, which must be in your main inventory and of an item type that can be charged, e.g. a wand. Be careful, recharging an item too many times may cause it to explode."); break;
#endif
			case SCR_RANDOM_ENCHANTMENT: 
#ifdef PHANTOM_CRASH_BUG
				pline("Pick an item to randomly enchant. For best results, use it on a +0 one. Line length restrictions prevent me from elaborating so just trust me on that one."); break;
#else
				pline("Using this scroll will allow you to pick an item that you want to have randomly enchanted. The item in question might get a positive or negative enchantment. However, if the item had a positive enchantment before it will first be set to +0 and get enchanted afterwards, so it's probably better to use it on items that are already +0 or worse."); break;
#endif
			case SCR_GENOCIDE: 
				pline("A powerful magic scroll that can be read to permanently get rid of a monster type and also prevent any more of them to spawn. Not all monster types can be genocided though."); break;
			case SCR_PUNISHMENT: 
				pline("If you read this scroll, you receive a heavy iron ball that is heavy and will cause you to move slower if you don't pick it up, but the ball can be wielded as a weapon."); break;
			case SCR_STINKING_CLOUD: 
				pline("This scroll prompts you for a location to release a cloud of gas. However, you can't place the cloud in an unlit area or too far away from you."); break;
			case SCR_TRAP_DETECTION: 
				pline("A scroll that shows traps on your current dungeon level if read."); break;
			case SCR_ACQUIREMENT: 
				pline("You may wish for an object type if you read this."); break;
			case SCR_PROOF_ARMOR: 
				pline("A random worn armor-class item is made erosionproof if you read this."); break;
			case SCR_PROOF_WEAPON: 
				pline("If you wield a weapon and read this scroll, that weapon will become erosionproof."); break;
			case SCR_MASS_MURDER: 
				pline("A weaker version of the genocide scroll that only eliminates living monsters of the specified type."); break;
			case SCR_UNDO_GENOCIDE: 
				pline("If you already genocided a monster type, you can use this scroll to re-enable it to spawn."); break;
			case SCR_REVERSE_IDENTIFY: 
				pline("A quirky special version of the identify scroll, this thing prompts you to enter the name or description of an object which is then identified."); break;
			case SCR_WISHING: 
				pline("Allows you to wish for an object."); break;
			case SCR_ARTIFACT_CREATION: 
				pline("This scroll generates an artifact at your feet. You do want an artifact, right? Read it!"); break;
			case SCR_CONSECRATION: 
				pline("You must be standing in a room for this scroll to work. If you do, it will create an altar underneath you."); break;
			case SCR_ENTHRONIZATION: 
				pline("This scroll works only if you're in a room, which causes it to create a throne at your current location."); break;
			case SCR_FOUNTAIN_BUILDING: 
				pline("If you read this scroll while in a room, a fountain appears below you. Otherwise, nothing happens."); break;
			case SCR_SINKING: 
				pline("Doesn't work outside of a room. What it does is building a sink on your current tile."); break;
			case SCR_WC: 
				pline("Builds a toilet on your square, but only if that square is in a room."); break;
			case SCR_LAVA: 
				pline("Reading this scroll turns some ordinary floor squares into lava."); break;
			case SCR_STONING: 
				pline("Read this scroll if you want to become a statue."); break;
			case SCR_GROWTH: 
				pline("You will create lots of trees if you read this scroll."); break;
			case SCR_ICE: 
				pline("Normal floor becomes icy in a radius around you if you read this."); break;
			case SCR_CLOUDS: 
				pline("A scroll that can be read to create clouds around you."); break;
			case SCR_BARRHING: 
				pline("This scroll creates iron bars on empty floor squares in your vicinity."); break;
			case SCR_LOCKOUT: 
				pline("Corridors near you turn into solid rock walls and doors automatically repair and lock themselves if you read this scroll."); break;
			case SCR_ROOT_PASSWORD_DETECTION: 
				pline("This scroll has the computer's root password written on it, which you can read. It's likely to be useless anyway."); break;
			case SCR_TRAP_CREATION: 
				pline("A dangerous scroll that creates traps around you."); break;
			case SCR_CREATE_TRAP: 
				pline("Reading this scroll causes a trap to be created underneath you which then triggers."); break;
			case SCR_NASTINESS: 
				pline("Don't allow this scroll to be read unless you absolutely want to be hit by nasty trap effects!"); break;
			case SCR_DEMONOLOGY: 
				pline("This scroll summons greater demons."); break;
			case SCR_ELEMENTALISM: 
				pline("Some creatures from the elemental planes will be summoned if you read this scroll."); break;
			case SCR_GIRLINESS: 
				pline("The dungeon will become more female if you read this scroll. :D"); break;
			case SCR_SLEEP: 
				pline("Reading this scroll puts you to sleep, leaving you open to monsters attacking you."); break;
			case SCR_CHAOS_TERRAIN: 
				pline("Read this scroll to generate chaotic terrain around you."); break;
			case SCR_WOUNDS: 
				pline("The opposite of the scroll of healing, reading this thing will damage you."); break;
			case SCR_BULLSHIT: 
				pline("Reading this scroll causes invisible dogs to shit all over the place."); break;
			case SCR_REPAIR_ITEM: 
				pline("If you read this scroll, you may pick an item that will be repaired."); break;
			case SCR_SUMMON_BOSS: 
				pline("Summons a boss monster when read."); break;
			case SCR_ITEM_GENOCIDE: 
				pline("A very powerful scroll that prevents a type of item from being generated. If you use it again on another item, the previously genocided one will be able to generate again."); break;
			case SCR_BLANK_PAPER: 
				pline("A scroll that doesn't have a magic formula written on it. You may use a magic marker to turn it into another type of scroll."); break;
			case SCR_ARMOR_SPECIALIZATION: 
				pline("Read this scroll, then select a worn piece of armor to add an egotype to it! However, know that it won't work if the armor piece in question already has an egotype."); break;
			case SCR_SECURE_IDENTIFY: 
				pline("Annoyed that regular identify scrolls fail so often? With this scroll, you can identify an object without any chance of it resisting the identification attempt!"); break;

 			default: pline("Missing item description (this is a bug). Please tell Amy about the item in question so she can add a description."); break;

			}

		}
		break;

		case SPBOOK_CLASS:
#ifdef PHANTOM_CRASH_BUG
		pline("%s - Color: %s. Material: %s. Appearance: %s. Spell level: %d.",xname(obj), obj->dknown ? c_obj_colors[objects[obj->otyp].oc_color] : "unknown", obj->dknown ? materialnm[objects[obj->otyp].oc_material] : "unknown", obj->dknown ? dn : "unknown", nn ? objects[obj->otyp].oc_level : 0);
#else
		pline("%s - This is a spellbook. Color: %s. Material: %s. Appearance: %s. Spell level: %d. Reading it allows you to learn a new spell permanently, or refresh your memory if you already know the spell.",xname(obj), obj->dknown ? c_obj_colors[objects[obj->otyp].oc_color] : "unknown", obj->dknown ? materialnm[objects[obj->otyp].oc_material] : "unknown", obj->dknown ? dn : "unknown", nn ? objects[obj->otyp].oc_level : 0);
#endif
		if (!nn) pline("Unfortunately you don't know more about it. You will gain more information if you identify this item.");
		else { switch (obj->otyp) {

			case SPE_FORCE_BOLT:
				pline("A spell that fires an invisible beam. It can damage monsters, items and certain dungeon features."); break;
			case SPE_CREATE_MONSTER:
				pline("Casting this spell summons random monsters. Beware, it might also create a trap on the current dungeon level."); break;
			case SPE_DRAIN_LIFE:
				pline("This spell drains the life force out of monsters, reducing their level. It also reduces the enchantment of objects it hits."); break;
			case SPE_COMMAND_UNDEAD:
				pline("A spell that attempts to tame all adjacent undead monsters. They have a chance of resisting, and very rarely they may instead enter a state of frenzy, becoming immune to further taming attempts."); break;
			case SPE_SUMMON_UNDEAD:
				pline("Summons an undead monster. Rarely, it also makes a trap on the level."); break;
			case SPE_STONE_TO_FLESH:
				pline("This spell can be cast at items, dungeon features and monsters that are made of stone, turning them into meat."); break;
			case SPE_HEALING:
				pline("A basic healing spell that can be used on yourself or on a monster to heal them."); break;
			case SPE_CURE_BLINDNESS:
				pline("Casting this spell cures blindness."); break;
			case SPE_CURE_DIM:
				pline("If you are hit by the dim status effect, this spell will remove the condition."); break;
			case SPE_CURE_SICKNESS:
				pline("A powerful spell that cures any food poisoning and illness you might be suffering from."); break;
			case SPE_CURE_HALLUCINATION:
				pline("If you successfully cast this spell, your hallucinations will end."); break;
			case SPE_CURE_CONFUSION:
				pline("A spell that can be successfully cast even while confused, and that's also the reason why one would cast it in the first place since it cures the confusion status."); break;
			case SPE_CURE_STUN:
				pline("By casting this spell, you can get rid of the stun condition."); break;
			case SPE_GENOCIDE:
#ifdef PHANTOM_CRASH_BUG
				pline("Casting this spell might allow you to genocide some monster species. However, it often fails."); break;
#else
				pline("Yes, this is not a joke. Casting this spell might allow you to genocide some monster. However, it often fails, and even on the off chance it doesn't, you will only be able to genocide a single monster species."); break;
#endif
			case SPE_EXTRA_HEALING:
				pline("A more powerful healing spell that can heal more hit points than the standard healing spell. Can be cast at yourself or at a monster."); break;
			case SPE_FULL_HEALING:
				pline("This spell can be aimed at yourself or a monster to heal a large amount of hit points."); break;
			case SPE_RESTORE_ABILITY:
				pline("If your attributes have been damaged, this spell may gradually restore them. Occasionally it fails though."); break;
			case SPE_CREATE_FAMILIAR:
				pline("Casting this spell sometimes summons a monster that fights on your side. It has a high chance of summoning a hostile creature instead, so beware..."); break;
			case SPE_LIGHT:
				pline("A spell that lights up dark areas."); break;
			case SPE_DETECT_MONSTERS:
				pline("Allows you to see some of the monsters on the current dungeon level."); break;
			case SPE_DETECT_FOOD:
				pline("This spell shows you the food items on the current level."); break;
			case SPE_CLAIRVOYANCE:
				pline("You can see part of your surroundings by casting this."); break;
			case SPE_DETECT_UNSEEN:
				pline("A spell that may detect hidden things close by, e.g. traps or invisible monsters."); break;
			case SPE_IDENTIFY:
				pline("Casting this spell allows you to identify some objects in your inventory. Careful: very rarely this spell can backlash, causing random bad effects or occasionally amnesia!"); break;
			case SPE_DETECT_TREASURE:
				pline("This spell detects some of the objects on the current level."); break;
			case SPE_MAGIC_MAPPING:
				pline("A spell that reveals fragments of what the current dungeon level looks like, unless it's a non-mappable special level."); break;
			case SPE_ENTRAPPING:
				pline("Casting this spell allows you to detect traps on the level, but each cast only reveals a few of them at once."); break;
			case SPE_FINGER:
				pline("This spell fires an invisible beam that shows you the attributes of monsters hit by it."); break;
			case SPE_CHEMISTRY:
				pline("Casting this spell has no effect, but knowing it increases the likelihood of the chemistry set actually working."); break;
			case SPE_DETECT_FOOT:
#ifdef PHANTOM_CRASH_BUG
				pline("This spell makes enemies fall over unconscious."); break;
#else
				pline("According to the Sporkhack creator, this spell is supposed to be useless. But this is Slash'EM Extended, where it allows you to make enemies fall over unconscious by... well, just see it for yourself. :D"); break;
#endif
			case SPE_CONFUSE_MONSTER:
				pline("Your melee attacks can confuse monsters if you cast this spell."); break;
			case SPE_FORBIDDEN_KNOWLEDGE:
#ifdef PHANTOM_CRASH_BUG
				pline("Learn this spell or cast it and your deity becomes very angry. The latter also grants temporary damage and spell resistance."); break;
#else
				pline("Learning this spell causes your deity to become very angry. Casting it angers your deity even more, but grants resistance to damage and spells for a while. The appearance and level of this book are random."); break;
#endif
			case SPE_SLOW_MONSTER:
				pline("This spell fires an invisible beam that slows targets."); break;
			case SPE_CAUSE_FEAR:
				pline("Use this spell to make monsters flee from you. Occasionally it backlashes, afflicting you with a standard status effect."); break;
			case SPE_CHARM_MONSTER:
#ifdef PHANTOM_CRASH_BUG
				pline("This powerful spell can sometimes charm adjacent monsters, but they resist often so you may have to cast it repeatedly. If you're unlucky, the monster will enter a frenzied state instead."); break;
#else
				pline("A very powerful spell that tries to tame adjacent monsters. Their magic resistance prevents this from working sometimes, and since the spell used to be totally unbalanced, it can also fail if the monster isn't resistant at all. But even that's not enough, and therefore Amy added another failure effect: if you're unlucky, the monster will be frenzied, which means it cannot be tamed at all anymore."); break;
#endif
			case SPE_ENCHANT_WEAPON:
				pline("This spell rarely works, but if it does, it tries to enchant your wielded weapon. Beware, if the weapon in question already has a very high enchantment, it might blow up."); break;
			case SPE_ENCHANT_ARMOR:
				pline("This spell rarely works, but if it does, it tries to enchant a user-selected worn piece of armor. Beware, if the armor in question already has a very high enchantment, it might blow up."); break;
			case SPE_CHARGING:
				pline("Cast this spell if you want to recharge your objects. Beware, though; occasionally, doing so can have nasty side effects, because otherwise this spell would be too overpowered."); break;
			case SPE_PROTECTION:
				pline("A spell that temporarily improves your armor class."); break;
			case SPE_RESIST_POISON:
				pline("This spell provides temporary poison resistance when cast."); break;
			case SPE_RESIST_SLEEP:
				pline("This spell provides temporary sleep resistance when cast."); break;
			case SPE_ENDURE_COLD:
				pline("This spell provides temporary cold resistance when cast."); break;
			case SPE_ENDURE_HEAT:
				pline("This spell provides temporary fire resistance when cast."); break;
			case SPE_INSULATE:
				pline("This spell provides temporary shock resistance when cast."); break;
			case SPE_REMOVE_CURSE:
				pline("A spell that might uncurse some of your cursed items. It only affects items in your main inventory, and usually worn ones only. Occasionally it will backfire."); break;
			case SPE_TURN_UNDEAD:
				pline("Fires an invisible beam that makes undead monsters flee and revives dead monsters."); break;
			case SPE_ANTI_DISINTEGRATION:
				pline("This spell provides temporary disintegration resistance when cast."); break;
			case SPE_BOTOX_RESIST:
				pline("This spell provides temporary sickness resistance when cast."); break;
			case SPE_ACIDSHIELD:
				pline("This spell provides temporary acid resistance when cast."); break;
			case SPE_GODMODE:
				pline("Exactly what it says on the tin. However, the invulnerability granted by casting this spell will wear off after a few turns."); break;
			case SPE_RESIST_PETRIFICATION:
				pline("This spell provides temporary petrification resistance when cast."); break;
			case SPE_JUMPING:
				pline("Casting this spell allows you to jump to a nearby empty location. There are some rules to consider though, e.g. the square must be lit and there needs to be a clear path between you and your location."); break;
			case SPE_HASTE_SELF:
				pline("This spell allows you to move at very fast speed for a period of time."); break;
			case SPE_ENLIGHTEN:
				pline("A spell that displays your current resistances, whether it is safe to pray, etc. It only ever displays a small fraction of info though, so you may need to cast it repeatedly."); break;
			case SPE_INVISIBILITY:
				pline("Use this spell if you want to turn invisible for a period of time."); break;
			case SPE_DISINTEGRATION_BEAM:
				pline("Awesome power - this spell fires disintegration beams that can instakill monsters!"); break;
			case SPE_FIRE_BOLT:
				pline("Shoots a stream of fire at monsters."); break;
			case SPE_FLYING:
				pline("Wanna fly for a while? Then cast this, and you'll be able to pass over water and lava for a period of time while still being able to pick up stuff."); break;
			case SPE_CHROMATIC_BEAM:
				pline("The RNG will determine the type of beam you fire with this spell, every time you fire it."); break;
			case SPE_FUMBLING:
				pline("If for some obscure reason you want to fumble, you can achieve it by casting this spell."); break;
			case SPE_MAKE_VISIBLE:
				pline("Any monster or item hit by the invisible beam of this spell will lose its invisibility; no effect on things that are already visible."); break;
			case SPE_WARPING:
				pline("Wanna get out of a sticky situation? This is a possible way, although you won't be able to control your destination. This spell also backlashes fairly frequently, causing undesirable effects."); break;
			case SPE_TRAP_CREATION:
				pline("You will create some traps around you by casting this spell."); break;
			case SPE_STUN_MONSTER:
				pline("A spell that fires invisible beams to stun monsters."); break;
			case SPE_CURSE_ITEMS:
				pline("Some of your items will become cursed (or unblessed) if you cast this spell."); break;
			case SPE_CHARACTER_RECURSION:
#ifdef PHANTOM_CRASH_BUG
				pline("Permanently transforms your character into another one, deletes your inventory and removes all spells. Be wise! Drop your inventory first!"); break;
#else
				pline("DANGER: Casting this spell will transform your character into another one *permanently*. It also deletes your inventory (so you should probably drop it first) and your entire spell list. Be sure you really want this, because it cannot be reverted once done!"); break;
#endif
			case SPE_CLONE_MONSTER:
				pline("Everybody's dream spell, you can now multiply monsters if you want to! Have fun!"); break;
			case SPE_DESTROY_ARMOR:
				pline("A useful spell if, and only if, you put on some terribly cursed armor and need to get rid of it."); break;
			case SPE_INERTIA:
				pline("Powerful spell that you can fire at enemies to slow them down."); break;
			case SPE_TIME:
				pline("You can 'clock back' enemies with this spell, draining their health and level permanently."); break;
			case SPE_LEVITATION:
				pline("This spell allows you to levitate for a while."); break;
			case SPE_TELEPORT_AWAY:
				pline("A spell that can be used to fire teleport beams at yourself, monsters and objects. It occasionally backfires though, causing nasty side effects."); break;
			case SPE_PASSWALL:
				pline("Casting this spell allows you to walk through walls for a limited amount of time. Beware, certain special levels have walls that resist this ability. Also, casting it can sometimes backfire."); break;
			case SPE_POLYMORPH:
				pline("A spell that can be cast at stuff to polymorph it. It occasionally backfires though, causing nasty side effects."); break;
			case SPE_MUTATION:
				pline("A spell that has several uses. Zapping yourself will polymorph you, zapping objects will polymorph them, and if you zap a monster, it will gain mutations. Backlashes occasionally."); break;
			case SPE_KNOCK:
				pline("This spell opens things like locked doors or chests."); break;
			case SPE_FLAME_SPHERE:
				pline("Conjures a flaming sphere that attacks your enemies by exploding and doing some fire damage."); break;
			case SPE_FREEZE_SPHERE:
				pline("Conjures a freezing sphere that attacks your enemies by exploding and doing some cold damage."); break;
			case SPE_SHOCKING_SPHERE:
				pline("Conjures a shocking sphere that attacks your enemies by exploding and doing some shock damage."); break;
			case SPE_ACID_SPHERE:
				pline("Conjures an acidic sphere that attacks your enemies by exploding and doing some acid damage."); break;
			case SPE_WIZARD_LOCK:
				pline("A spell that fires invisible locking beams, which have an effect only if they hit something lockable. It can also repair broken doors."); break;
			case SPE_DIG:
				pline("Casting this can dig through walls and other obstacles."); break;
			case SPE_CANCELLATION:
				pline("This spell can be cast at objects and monsters to cancel them. However, it occasionally backfires."); break;
			case SPE_REFLECTION:
				pline("You can reflect beams and similar attacks for a limited amount of time if you cast this spell."); break;
			case SPE_REPAIR_ARMOR:
				pline("Casting this spell repairs some of your armor. You may choose which item to repair."); break;
			case SPE_REPAIR_WEAPON:
				pline("Casting this spell repairs your weapon."); break;
			case SPE_PSYBEAM:
				pline("A spell that zaps monsters with psybeams, dealing damage and confusing everything hit by them. Psi-using monsters are immune."); break;
			case SPE_HYPER_BEAM:
				pline("Fires a more powerful magic missile."); break;
			case SPE_MAGIC_MISSILE:
				pline("A spell that fires a blue ray to do some damage to an enemy."); break;
			case SPE_FIREBALL:
				pline("This spell blasts monsters with a bolt of fire."); break;
			case SPE_CONE_OF_COLD:
				pline("Cast this spell in order to fire a blast of cold."); break;
			case SPE_SLEEP:
				pline("A spell that fires sleep rays at monsters."); break;
			case SPE_FINGER_OF_DEATH:
				pline("This spell shoots death rays that can instantly kill enemies."); break;
			case SPE_LIGHTNING:
				pline("A spell that projects a beam of lightning in a direction of your choice."); break;
			case SPE_POISON_BLAST:
				pline("This spell fires poison beams at enemies."); break;
			case SPE_ACID_STREAM:
				pline("A spell that does acid damage by hitting monsters with a ray."); break;
			case SPE_SOLAR_BEAM:
				pline("Fires a beam of pure solar energy that does a ton and a half of damage to enemies."); break;
			case SPE_BLANK_PAPER:
				pline("This spellbook is blank. You may be able to write on it with a magic marker, turning it into another spellbook."); break;
			case SPE_BOOK_OF_THE_DEAD:
				pline("An arcane book that can be read. Reciting the eldritch formulas contained therein may raise the dead, so be careful. Reading it without an imbued silver bell doesn't work at all, though."); break;
			case SPE_DARKNESS:
				pline("Cast this spell if you want to turn lit areas into unlit ones."); break;
			case SPE_AMNESIA:
				pline("This spellbook causes you to forget stuff."); break;
			case SPE_KNOW_ENCHANTMENT:
				pline("Your entire inventory may have their enchantment revealed, so pack as much stuff as you can before casting this spell. However, only 1 in 4 items will actually be affected, and it's predetermined which ones they are, so don't bother trying repeatedly."); break;
			case SPE_MAGICTORCH:
				pline("A spell that increases your field of view for a period of time."); break;
			case SPE_DISPLACEMENT:
				pline("If you want to have intrinsic displacement for a while, cast this spell."); break;
			case SPE_MASS_HEALING:
				pline("Heals monsters around you."); break;
			case SPE_ALTER_REALITY:
				pline("A truly powerful spell that re-initializes some environment variables which had been initialized when you started your adventures."); break;
			case SPE_TIME_SHIFT:
				pline("Casting this spell increases the turn counter by 5. Don't do it if you're doing a speedrun."); break;
			case SPE_DETECT_ARMOR_ENCHANTMENT:
				pline("This spell tries to detect the enchantment value of all armor items in your main inventory. It only affects roughly one third of all items though, and repeated casting won't change which ones they are. Either they're revealed on the first cast or this spell doesn't reveal them at all."); break;
			case SPE_CONFUSE_SELF:
				pline("Want to confuse yourself? Cast this spell!"); break;
			case SPE_STUN_SELF:
				pline("You can cast this spell to be stunned on purpose, should you wish to do so for some weird reason."); break;
			case SPE_BLIND_SELF:
				pline("A spell that blinds you for a period of time. This is best used in conjunction with telepathy."); break;
			case SPE_CORRODE_METAL:
				pline("This spell corrodes metal items in your main inventory, causing them to degrade."); break;
			case SPE_DISSOLVE_FOOD:
				pline("Want to get rid of your food without eating it? Then cast this spell while having food in your main inventory!"); break;
			case SPE_AGGRAVATE_MONSTER:
				pline("This spell will aggravate monsters if you cast it."); break;
			case SPE_REMOVE_BLESSING:
				pline("A spell that turns all blessed items in your main inventory into uncursed ones."); break;

			case SPE_DISINTEGRATION:
				pline("This very powerful spell will fire invisible disintegration beams that can be used to instakill monsters."); break;
			case SPE_PETRIFY:
				pline("Cast this spell at monsters to turn them to stone."); break;
			case SPE_PARALYSIS:
				pline("You can shoot paralysis beams by casting this spell."); break;
			case SPE_LEVELPORT:
				pline("If you cast this spell, you will teleport to a random dungeon level in your current branch. However, it always backlashes after teleporting you, causing nasty side effects! Beware!"); break;
			case SPE_BANISHING_FEAR:
				pline("A spell that cures the 'fear' status conditions. At higher spell levels it allows you to resist fear for a period of time."); break;
			case SPE_CURE_FREEZE:
				pline("Got hit by the 'freeze' status effect? Cast this spell and it will go away!"); break;
			case SPE_CURE_BURN:
				pline("This spell will cure you of burns."); break;
			case SPE_CURE_NUMBNESS:
				pline("A spell that can be cast to cure numbness."); break;
			case SPE_TIME_STOP:
				pline("This very powerful spell will stop the flow of time for a brief period."); break;
			case SPE_STINKING_CLOUD:
				pline("Casting this spell allows you to place a stinking cloud on a nearby visible location."); break;
			case SPE_GAIN_LEVEL:
				pline("An absurdly powerful spell that may increase your character level. However, it often fails."); break;
			case SPE_MAP_LEVEL:
				pline("This spell fails most of the time, but if it doesn't, it will reveal the map of the entire level and show most objects as well as traps."); break;
			case SPE_INFERNO:
				pline("Fire spell that severely burns enemies, afflicting them with blindness and damaging them in a way that cannot be easily healed."); break;
			case SPE_ICE_BEAM:
				pline("This spell shoots cold ice at enemies to damage and slow them."); break;
			case SPE_THUNDER:
				pline("You can cast thunderous bolts of lightning if you master this spell. The enemy can occasionally be paralyzed or numbed, too."); break;
			case SPE_SLUDGE:
				pline("Very powerful ranged acid attack that can also drain levels from the target."); break;
			case SPE_TOXIC:
				pline("Fires superpoisonous gas at enemies, making quick work of those that don't resist."); break;
			case SPE_NETHER_BEAM:
				pline("A spell that shoots very powerful psychic blasts at enemies."); break;
			case SPE_AURORA_BEAM:
				pline("You can irradiate monsters with pure light by zapping this beam spell at them. Sometimes it cancels the target monster."); break;
			case SPE_GRAVITY_BEAM:
				pline("There's no way for monsters to resist this spell, and it deals great damage to big monsters."); break;
			case SPE_CHLOROFORM:
				pline("A sleep ray spell that also damages the monster."); break;
			case SPE_DREAM_EATER:
				pline("You can fire invisible beams at monsters by casting this spell. It only damages sleeping monsters, but those will take a huge amount of damage!"); break;
			case SPE_BUBBLEBEAM:
				pline("This spell does damage to monsters that cannot survive underwater, and it also occasionally turns floor into water."); break;
			case SPE_GOOD_NIGHT:
				pline("A spell that fires invisible beams of pure darkness, making areas unlit and doing great damage to lawful-aligned monsters."); break;
			case SPE_FIXING:
				pline("The cure-all spell that fixes just about every status condition there is."); break;
			case SPE_CHAOS_TERRAIN:
				pline("Creates chaotic terrain around you. This spell is very straining and will damage your maximum mana."); break;
			case SPE_RANDOM_SPEED:
				pline("You can cast this spell to fire invisible beams that randomly either speed up or slow down the target."); break;
			case SPE_VANISHING:
				pline("A spell that will, with equal chance, teleport the target away, make it invisible or cancel it. Rarely, this spell will backfire."); break;
			case SPE_WISHING:
				pline("The ultimate spell, only the best spellcasters will be able to use it, and it will drain up to 500 max HP and Pw and permanently reduce all of your stats by up to 5. Decide for yourself if getting a free wish is worth it."); break;
			case SPE_ACQUIREMENT:
				pline("A powerful spell that allows you to acquire an item; you can choose the class, but not the exact item. It will drain up to 100 max HP and Pw and permanently reduce some of your stats by 1 though."); break;
			case SPE_CHAOS_BOLT:
				pline("Offensive spell that deals damage and has a chance of polymorphing the monster. However, you may be afflicted with hallucination after you cast it."); break;
			case SPE_HELLISH_BOLT:
				pline("This very powerful spell not only damages opponents, it will also polymorph, paralyze or level-drain them. However, there is a sizable chance that casting it will cause you to hallucinate for a while."); break;
			case SPE_EARTHQUAKE:
				pline("Casting this earth-shaking spell produces lots of pits, but it can also temporarily deactivate your intrinsics."); break;
			case SPE_LYCANTHROPY:
				pline("Afflicts you with random lycanthropy."); break;
			case SPE_BUC_RANDOMIZATION:
				pline("This spell can affect uncursed items in your inventory, turning them into blessed or cursed ones at random."); break;
			case SPE_LOCK_MANIPULATION:
				pline("Cast this spell at doors and chests to manipulate their 'locked' status."); break;
			case SPE_POLYFORM:
				pline("A completely random polymorph will affect you if you cast this. It can occasionally backfire, and often the duration of this polymorph is not very long."); break;
			case SPE_MESSAGE:
				pline("Displays a random message when cast."); break;
			case SPE_RUMOR:
				pline("Displays a random rumor when cast."); break;
			case SPE_CURE_RANDOM_STATUS:
				pline("Can be cast even while confused. It will randomly pick one of these status afflictions to cure, regardless of whether you actually have it: sickness/sliming, hallucination, confusion, stun, burn, freezing, numbness, blindness, dimness or fear."); break;
			case SPE_RESIST_RANDOM_ELEMENT:
				pline("Gain a random resistance temporarily by casting this spell!"); break;
			case SPE_RUSSIAN_ROULETTE:
				pline("Normally this spell will cause a bad effect, but sometimes it also increases your maximum health. There is a tiny chance to lose a large amount of maximum health and mana though."); break;
			case SPE_POSSESSION:
				pline("Requires a corpse, which has to be either in your inventory or at your feet, and which will be used up; you transform into the depicted monster. Occasionally the power can go out of control though, causing you to become another random monster, having your maximum mana reduced or being subjected to other negative effects."); break;
			case SPE_TOTEM_SUMMONING:
				pline("Requires a corpse, which has to be either in your inventory or at your feet, and which will be used up; it will come alive, and has a chance of being tame. You will take up to 100 points of damage though, and sometimes it backfires, summoning hostile monsters and reducing your maximum mana."); break;
			case SPE_MIMICRY:
				pline("Turns you into a zorkmid or an orange."); break;
			case SPE_HORRIFY:
				pline("This spell fires beams that cause monsters to run away from you."); break;
			case SPE_TERROR:
				pline("Attempts to make all monsters on the level flee from you for a while. However, this spell may rarely cause backlash."); break;
			case SPE_PHASE_DOOR:
				pline("Teleports you over a short distance, but also backlashes occasionally."); break;
			case SPE_TRAP_DISARMING:
				pline("This is a powerful spell that removes all traps adjacent to you, but such power comes at a price: you will lose some maximum mana every time and be hit by a random bad effect."); break;
			case SPE_NEXUS_FIELD:
				pline("Tries to teleport all monsters adjacent to you away, although they have a chance to resist. It will occasionally backlash by reducing one of your attributes, permanently."); break;
			case SPE_COMMAND_DEMON:
				pline("Use this spell if you're surrounded by demons, and they have a chance of becoming peaceful or even tame!"); break;
			case SPE_FIRE_GOLEM:
				pline("This spell requires you to have a torch to create the golem from, which will be used up. The fire golem will then fight alongside you, and you get experience and credit for its kills, but beware: usually it will turn hostile after a while!"); break;
			case SPE_DISRUPTION_SHIELD:
				pline("A spell that allows you to use your mana as health for a while: for the duration of the effect, any damage you take will be subtracted from your mana instead - as long as you have enough mana, that is."); break;
			case SPE_SPELLBINDER:
				pline("Casting this spell allows you to cast several spells in the same turn. Every spell that you have can only be cast once per turn, and of course you cannot use those extra castings for another spellbinder, because things need to be sane. :-)"); break;
			case SPE_TRACKER:
				pline("Teleports you to the last monster that entered the current dungeon level, provided that nothing prevents you from teleporting and there is an empty space next to the monster in question. It will occasionally backfire."); break;
			case SPE_INERTIA_CONTROL:
				pline("This spell allows you to choose any of your other known spells, which will be inertia controlled for a while. That way you can automatically cast it while doing other things. Don't move the spell around in your spellcasting menu while inertia control is in effect though, or it will end prematurely!"); break;
			case SPE_CODE_EDITING:
				pline("Allows you to edit the code of the game ;), err, not really. It detonates all corpses on the current dungeon level, generating fiery explosions that deal damage depending on the dead monster's level."); break;
			case SPE_FORGOTTEN_SPELL:
				pline("Simulates the effects of casting a forgotten spell, so you might e.g. become confused or stunned."); break;
			case SPE_FLOOD:
				pline("Creates some water tiles around you, and reduces your maximum mana by up to 3 points."); break;
			case SPE_LAVA:
				pline("Casting this spell will create a few lava tiles close to you, and reduce your maximum mana by up to 3 points."); break;
			case SPE_IRON_PRISON:
				pline("This spell surrounds you with randomly placed iron bars, and reduces your maximum mana by up to 4 points."); break;
			case SPE_LOCKOUT:
				pline("A spell that generates walls and locks doors on random nearby squares, and reduces your maximum mana by up to 5 points."); break;
			case SPE_CLOUDS:
				pline("This spell creates some clouds near your current location, and occasionally reduces your maximum mana by one point."); break;
			case SPE_ICE:
				pline("Generates some ice tiles in your proximity, and reduces your maximum mana by up to 2 points."); break;
			case SPE_GROW_TREES:
				pline("It's a powerful spell that lets trees grow near you, but it also reduces your maximum mana by up to 4 points."); break;
			case SPE_DRIPPING_TREAD:
				pline("You can drip the elements for a while if you cast this, although it also costs up to 3 maximum HP. Walking around while the effect is active will generate water, lava, ice or cloud terrain underneath you; at higher skill levels you will be able to control the type of terrain created."); break;
			case SPE_GEOLYSIS:
				pline("This spell grants you the ability to eat through solid rock for a while, dealing up to 2 points of damage to your maximum health in the process. Eating a wall this way will occasionally turn it into ice, water or clouds."); break;
			case SPE_ELEMENTAL_BEAM:
				pline("Offensive spell that randomly shoots a beam of fire, cold, lightning or poison."); break;
			case SPE_STERILIZE:
				pline("It prevents multiplying monsters from doing their thing, and eggs cannot hatch for the duration of this spell. Furthermore, it reduces the monster respawning frequency."); break;
			case SPE_WIND:
				pline("You can fire jets of wind with this spell to push monsters away from you. However, you will be pushed around as well and take a bit of damage."); break;
			case SPE_FIRE:
				pline("Causes a fiery explosion on top of you."); break;
			case SPE_ELEMENTAL_MINION:
				pline("At the cost of some maximum health, this spell tries to create a tame elemental being to fight alongside you."); break;
			case SPE_WATER_BOLT:
				pline("The invisible beam fired by this spell does not damage anyone or anything, but creates water tiles that may drown monsters and objects. It will cause you to be confused and stunned, so beware lest you fall into your own pools! And it also backfires occasionally."); break;
			case SPE_AIR_CURRENT:
				pline("Air current noises are sexy! :-) This spell will push you around a bit."); break;
			case SPE_DASHING:
				pline("Allows you to dash in a direction of your choice. You will always try to move at least 2 squares; at higher skill levels, you will be able to choose the distance."); break;
			case SPE_MELTDOWN:
				pline("It's a spell that allows you to melt away iron bars next to you, but you also lose some maximum health and mana."); break;
			case SPE_POISON_BRAND:
				pline("You need to be holding a weapon to use this, and the weapon in question must be of a type that can be poisoned, which is what will be done to it. However, you will also be poisoned and may lose attribute points."); break;
			case SPE_STEAM_VENOM:
				pline("This spell causes a poisonous explosion centered on you."); break;
			case SPE_HOLD_AIR:
				pline("Allows you to survive without air for a while."); break;
			case SPE_SWIMMING:
				pline("This spell grants you the ability to swim for a period of time."); break;
			case SPE_VOLT_ROCK:
				pline("Attempts to damage a monster adjacent to you. Only monsters that either aren't wearing a helmet or aren't resistant to shock will be damaged by it."); break;
			case SPE_WATER_FLAME:
				pline("It fires watery flames that deal extra damage to monsters that are cold or fire resistant. If the target resists both, it will take maximum damage."); break;
			case SPE_AVALANCHE:
				pline("This dangerous spells will bury you and monsters adjacent to you in a shower of rocks and boulders. You will lose some maximum health and mana and be paralyzed for a while, so be careful!"); break;
			case SPE_MANA_BOLT:
				pline("A low-damage attack spell that cannot be resisted."); break;
			case SPE_ENERGY_BOLT:
				pline("A medium-damage attack spell that cannot be resisted."); break;
			case SPE_ACID_INGESTION:
				pline("This spell creates acid in your mouth, which you will then swallow, resulting in damage unless you're acid resistant. Use it to cure petrification."); break;
			case SPE_INDUCE_VOMITING:
				pline("If you want to vomit for some reason, you can cast this spell."); break;
			case SPE_REBOOT:
				pline("Casting this spell recalculates your attributes and may also change your experience level."); break;
			case SPE_HOLY_SHIELD:
				pline("For the duration of this spell, your shield has a higher chance of blocking attacks."); break;

			case SPE_FROST:
				pline("Tries to slow down all monsters adjacent to you, but they can resist."); break;
			case SPE_TRUE_SIGHT:
				pline("A spell that temporarily enables you to see invisible things."); break;
			case SPE_BERSERK:
				pline("Allows you to go berserk, which greatly increases your damage output for a while, but after it times out you will be heavily confused and stunned. You also cannot block and have greatly lowered armor class while the spell is active. Do not try to cast it again while it's already active!"); break;
			case SPE_BLINDING_RAY:
				pline("Shoots invisible rays that can blind monsters."); break;
			case SPE_MAGIC_SHIELD:
				pline("Casting this spell will increase your magic cancellation for a while."); break;
			case SPE_WORLD_FALL:
				pline("Cataclysm/apocalypse. It kills all monsters whose level is lower than yours, and can also sometimes kill higher-level ones, but you lose quite a lot of maximum health and mana for casting it and are hit by several bad effects as well as long-lasting nasty trap effects."); break;
			case SPE_ESP:
				pline("Temporary ESP (telepathy)."); break;
			case SPE_RADAR:
				pline("Turning on your radar allows you to be warned of monsters for a period of time."); break;
			case SPE_SEARCHING:
				pline("A spell that grants automatic searching for a while."); break;
			case SPE_INFRAVISION:
				pline("Grants infravision if you don't have it already, but it doesn't last very long."); break;
			case SPE_STEALTH:
				pline("You will have temporary stealth if you cast this."); break;
			case SPE_CONFLICT:
				pline("A spell that allows you to cause conflict; you cannot forcibly turn it off though, but will have to wait until it times out."); break;
			case SPE_REGENERATION:
				pline("Your wounds will heal much faster for a period of time after casting this spell."); break;
			case SPE_FREE_ACTION:
				pline("Grants temporary free action, i.e. paralysis resistance."); break;
			case SPE_MULTIBEAM:
				pline("This spell blasts monsters with cold, fire and lightning."); break;
			case SPE_NO_EFFECT:
				pline("Well, if you cast this spell, there will be no effect."); break;
			case SPE_SELFDESTRUCT:
				pline("Casting this spell causes you to blow yourself up and die, dealing great damage to monsters in a 7x7 radius. Better be wearing a source of life saving if you do."); break;
			case SPE_THUNDER_WAVE:
				pline("Monsters adjacent to you are shocked and may be paralyzed, but they get a resistance check."); break;
			case SPE_BATTERING_RAM:
				pline("A very powerful spell that deals huge irresistible damage to a single monster standing right next to you, and will also try to push it back."); break;
			case SPE_BURROW:
				pline("Casting this spell causes you to burrow yourself into the ground, becoming immobile until you dig yourself out, but your armor class is also greatly increased as long as you're still burrowed."); break;
			case SPE_GAIN_CORRUPTION:
				pline("You will ruin your character with a permanent (well, there are cures, but they're very rare) bad effect if you are foolish enough to cast this."); break;
			case SPE_SWITCHEROO:
				pline("Cures all nasty trap effects, but then also causes various bad effects and gives you intrinsic nastiness, meaning the nasty trap effects will come back to haunt you some more after a while!"); break;
			case SPE_THRONE_GAMBLE:
				pline("Replicates the effects of sitting on a throne, although it cannot give a wish of course. This spell can occasionally backfire."); break;
			case SPE_BACKFIRE:
				pline("Well, you probably should not cast this."); break;
			case SPE_DEMEMORIZE:
				pline("Asks for a spell and sets its memory to 0%%."); break;
			case SPE_CALL_THE_ELEMENTS:
				pline("A very powerful offensive spell that shoots highly damaging fire, ice and lightning bolts at the enemy, which can also cause a variety of status effects for the target monster."); break;
			case SPE_NATURE_BEAM:
				pline("This spell fires powerful rays of fire, cold, lightning or poison (randomly chosen)."); break;
			case SPE_WHISPERS_FROM_BEYOND:
				pline("Tries to identify your entire inventory, because unlike ToME, there is no *Identify* effect in this game. There is no 'sanity' stat either, so it permanently reduces your INT and WIS by one or two instead, and if any of those go below 3, you die instantly!"); break;
			case SPE_STASIS:
				pline("Both you and all monsters will be frozen in time until the spell effect ends. Protip: cast it to wait out annoying status effects or regenerate your health without being interrupted."); break;
			case SPE_CRYOGENICS:
				pline("This spell lengthens the timeout of decaying/reviving corpses and hatching eggs, so you will have more time to e.g. eat them instead. Don't get your hopes up: it does not elongate the timer that determines whether corpses can still be offered."); break;
			case SPE_REDEMPTION:
				pline("Attachs a rot timeout to all corpses on the ground on the current dungeon level. If the corpse in question had a revive timeout, it will be stopped. Guess which monster class suddenly became much easier to get rid of."); break;
			case SPE_HYPERSPACE_SUMMON:
				pline("Summons a bunch of vortices, with a high chance of them being tame. It reduces your maximum health and mana a bit, though."); break;
			case SPE_SATISFY_HUNGER:
				pline("Fills your stomach a bit. Do not cast it while oversatiated - you will not get a warning!"); break;
			case SPE_RAIN_CLOUD:
				pline("Causes rain to pour down from the dungeon's ceiling, hitting all squares adjacent to you. Lava tiles will turn into water, and fire-based monsters will take damage. However, the rain will also slow you down for a while."); break;
			case SPE_POWER_FAILURE:
				pline("Causes a power outage in a 5x5 area centered on you, damaging all electrically based monsters and also blinding or stunning them. Very rarely, this spell may also vaporize iron bars. But the power outage effect will apply to you too, so you cannot apply tools or zap wands for a while, and certain methods of monster detection will also no longer work!"); break;
			case SPE_VAPORIZE:
				pline("Removes water squares next to you, and monsters that were swimming in there will take quite some damage and may be paralyzed. However, the vapors will also heavily burn you."); break;
			case SPE_TUNNELIZATION:
				pline("All monsters on the entire level that are currently in wall tiles will take lots of damage and have their walls removed if possible, but you are also heavily blinded and have your AC greatly reduced for a while, plus every monster that you 'disturb' reduces your alignment by 25 points."); break;
			case SPE_BOMBING:
				pline("Summons a tame spell being that will use a kamikaze attack on the first hostile monster it finds. Caution: you are responsible for it, so if it kills a peaceful creature, you might be dinged for murder!"); break;
			case SPE_DRAGON_BLOOD:
				pline("This powerful spell temporarily allows you to resist level-draining attacks."); break;
			case SPE_ANTI_MAGIC_FIELD:
				pline("Temporary magic resistance. It's not as good as permanent magic resistance from an item, but if you don't have such an item, you can continuously cast this."); break;
			case SPE_ANTI_MAGIC_SHELL:
				pline("After casting this spell, neither you nor any monster will be able to cast any spells for a period of time."); break;
			case SPE_CURE_WOUNDED_LEGS:
				pline("One of very few methods to cure your legs is this spell."); break;
			case SPE_ANGER_PEACEFUL_MONSTER:
				pline("If you want to avoid penalties for angering a monster by attacking it, you can stand next to it and cast this spell."); break;
			case SPE_UNTAME_MONSTER:
				pline("Cast this in order to get rid of useless pets, allowing you to kill them! You must stand right next to the pet in question though."); break;
			case SPE_UNLEVITATE:
				pline("Probably borderline useless, this spell allows you to stop levitation if it's from a temporary source. It does not get cursed rings or boots of levitation off you, though."); break;
			case SPE_DETECT_WATER:
				pline("Tries to detect water sources on the level."); break;
			case SPE_APPLY_NAIL_POLISH:
				pline("A spell that allows you to do your nails! Hopefully you're a woman, because then you'll really like this :-). But even if your character is male, it's just as useful. The more nails you do, the more damage your unarmed attacks can cause, but only if you're not wearing gloves. Being polymorphed into something with claw attacks also allows you to do extra damage."); break;
			case SPE_ENCHANT:
				pline("A short-duration spell that adds fire damage to your melee weapon. It does not work if you're fighting unarmed."); break;
			case SPE_DRY_UP_FOUNTAIN:
				pline("Good luck finding a use for this spell. Yes, the only thing it does is removing fountains that are right next to you, turning them into normal floor tiles."); break;
			case SPE_TAKE_SELFIE:
				pline("Causes you to stop for a few turns while you take a selfie. It can sometimes blind you too."); break;
			case SPE_SNIPER_BEAM:
				pline("This offensive spell can hit monsters standing very far away, but the damage is rather low. On the bright side, nothing can resist it."); break;
			case SPE_CURE_GLIB:
				pline("If you don't have a towel, you can cast this spell to cure your glibbery hands."); break;
			case SPE_CURE_MONSTER:
				pline("If there are monsters adjacent to you that are suffering from confusion, paralysis or other status effects, casting this will cure their afflictions."); break;
			case SPE_MANA_BATTERY:
				pline("Deletes all wands in your open inventory (so you should make sure you put away all those that you want to keep), and recovers 10 mana for every charge. The idea is from Elona. Yep. Elona introduced this first. What, you say ADOM did? Nope! ADOM stole it from Elona, of course! :P"); break;
			case SPE_THORNS:
				pline("This spell makes it so that monsters meleeing you will automatically take damage, however the spell effect times out rather quickly."); break;
			case SPE_REROLL_ARTIFACT:
				pline("Read the description slowly before doing anything: It allows you to pick an artifact in your main inventory and rerolls its base item. You can only pick weapons, armors, rings or amulets, and the item in question may not be worn or wielded! For example, an artifact leather armor may become an artifact silver dragon scale mail. And of course there's a bullshit downside, which is in this case a loss of some maximum HP and Pw."); break;
			case SPE_FINAL_EXPLOSION:
				pline("Caution: If you cast this spell, you die instantly. However, if you're capable of coming back to life, it might still be worth casting since all monsters in a huge 11x11 radius will take massive damage."); break;
			case SPE_CUTTING:
				pline("Eek! Please don't! Forget this spell before you make the mistake of casting it!"); break;

 			default: pline("Missing item description (this is a bug). Please tell Amy about the item in question so she can add a description."); break;

			}

		}
		break;

		case WAND_CLASS:
#ifdef PHANTOM_CRASH_BUG
		pline("%s - Color: %s. Material: %s. Appearance: %s.",xname(obj), obj->dknown ? c_obj_colors[objects[obj->otyp].oc_color] : "unknown", obj->dknown ? materialnm[objects[obj->otyp].oc_material] : "unknown", obj->dknown ? dn : "unknown");
#else
		pline("%s - This is a wand. Color: %s. Material: %s. Appearance: %s. It can be zapped for an effect; some wands will have a direction that you may choose. However, you can also apply wands to break them and release the energy contained therein, which has effects similar to what the wand normally does, or engrave with them which may give you some clues about what the wand actually does.",xname(obj), obj->dknown ? c_obj_colors[objects[obj->otyp].oc_color] : "unknown", obj->dknown ? materialnm[objects[obj->otyp].oc_material] : "unknown", obj->dknown ? dn : "unknown");
#endif
		if (!nn) pline("Unfortunately you don't know more about it. You will gain more information if you identify this item.");
		else { switch (obj->otyp) {

			case WAN_LIGHT: 
				pline("Zapping this wand will illuminate your surroundings."); break;
			case WAN_NOTHING: 
				pline("A wand that does nothing at all."); break;
			case WAN_SHARE_PAIN: 
				pline("This wand can be zapped at monsters to deal fractional damage to them, but beware! You will take the same amount of damage and you *can* die if it's too high!"); break;
			case WAN_ENLIGHTENMENT: 
				pline("If you zap this wand, you can see some clues about your status, e.g. alignment, whether it is safe to pray and if your luck is positive or negative."); break;
			case WAN_HEALING: 
				pline("Zapping this wand at a living creature will heal it. You can also zap yourself with it."); break;
			case WAN_LOCKING: 
				pline("If you zap this wand at something that can be locked, e.g. a door or chest, it will be locked. It can also transform broken doorways into fully functional locked doors and open floor into walls."); break;
			case WAN_MAKE_INVISIBLE: 
				pline("Zap this wand at something to make that 'something' invisible!"); break;
			case WAN_MAKE_VISIBLE: 
				pline("A wand that can be zapped to remove invisibility from monsters and items."); break;
			case WAN_IDENTIFY: 
				pline("If you zap this wand, you may identify some of your inventory items."); break;
			case WAN_REMOVE_CURSE: 
				pline("Zapping this wand may uncurse some items in your inventory."); break;
			case WAN_PUNISHMENT: 
				pline("A wand that chains you to a heavy iron ball if you zap it, or if you're already punished, your iron ball gets heavier."); break;
			case WAN_OPENING: 
				pline("This wand fires invisible beams that can open locks and remove walls."); break;
			case WAN_PROBING: 
				pline("Zapping this wand at a monster shows information about it, including its inventory contents."); break;
			case WAN_SECRET_DOOR_DETECTION: 
				pline("This wand can be zapped to detect secret doors close by."); break;
			case WAN_TRAP_DISARMING: 
				pline("Zap this wand if you want to get rid of traps on your square and the eight squares surrounding you."); break;
			case WAN_ENTRAPPING: 
				pline("A wand that allows you to detect traps."); break;
			case WAN_TELE_LEVEL: 
				pline("Zap this wand to land on another dungeon level!"); break;
			case WAN_TIME_STOP: 
				pline("This powerful wand is capable of stopping monsters from moving around."); break;
			case WAN_GENOCIDE: 
				pline("You can zap this wand to genocide a species of monsters."); break;
			case WAN_STINKING_CLOUD: 
				pline("With every zap of this wand, you can place a stinking cloud on a nearby visible square."); break;
			case WAN_TRAP_CREATION: 
				pline("Zapping this wand will surround you with randomly generated traps."); break;
			case WAN_SUMMON_SEXY_GIRL: 
				pline("This wand summons a sexy girl when zapped. Enjoy! :-)"); break;
			case WAN_DARKNESS: 
				pline("A wand that makes the area surrounding you unlit."); break;
			case WAN_MAGIC_MAPPING: 
				pline("Zap this wand if you want an idea of what the current level layout looks like!"); break;
			case WAN_DETECT_MONSTERS:
				pline("A wand that allows you to detect monsters wherever they are on the current level."); break;
			case WAN_OBJECTION:
				pline("Detects objects on the current dungeon level."); break;
			case WAN_SLOW_MONSTER:
				pline("Zapping this at a monster causes it to move slower."); break;
			case WAN_SPEED_MONSTER:
				pline("Zapping yourself with this wand gives you permanent intrinsic speed. It can also be zapped at monsters to speed them up."); break;
			case WAN_HASTE_MONSTER:
				pline("Zapping yourself with this wand will make you very fast for a while. It can also be zapped at monsters to speed them up."); break;
			case WAN_STRIKING:
				pline("A wand that shoots invisible bolts of force to damage enemies, break doors and otherwise interact with the dungeon."); break;
			case WAN_UNDEAD_TURNING:
				pline("This wand can be zapped at living undead monsters to make them flee, or you can zap corpses with it to reanimate them. Unfortunately you can't zap yourself after you die."); break;
			case WAN_DRAINING:
				pline("A wand that can be zapped at monsters and objects to drain their level."); break;
			case WAN_REDUCE_MAX_HITPOINTS:
				pline("A wand that can be zapped at monsters to reduce their maximum amount of hit points. Zapping it at objects drains their enchantment."); break;
			case WAN_INCREASE_MAX_HITPOINTS:
				pline("A wand that can be zapped at monsters or yourself to increase their maximum amount of hit points. Zapping it at objects drains their negative enchantment, bringing it closer to +0. It can only be recharged once."); break;
			case WAN_CANCELLATION:
				pline("Cancels whatever you zap it at. Monsters lose their ability to use certain types of special attacks while objects will lose their enchantments."); break;
			case WAN_CREATE_MONSTER:
				pline("Zapping this wand summons monsters."); break;
			case WAN_CREATE_FAMILIAR:
				pline("Zapping this wand summons a tame monster."); break;
			case WAN_BAD_EFFECT:
#ifdef PHANTOM_CRASH_BUG
				pline("This wand will subject you to a random bad effect if it is zapped."); break;
#else
				pline("This wand will subject you to a random bad effect if it is zapped. It doesn't matter WHO zaps it, it's always you who will suffer from its effect. Think that's unfair? Well, suck it up, this game was never designed to be fair in the first place!"); break;
#endif
			case WAN_SUMMON_UNDEAD:
				pline("A wand that summons some undead monsters if it is zapped."); break;
			case WAN_FEAR:
				pline("Firing this wand at a monster may cause it to run away in fear."); break;
			case WAN_WIND:
				pline("A wand that creates a powerful wind to push monsters and objects out of your way."); break;
			case WAN_DISINTEGRATION_BEAM:
				pline("Similar to polymorphing into a black dragon, but in wand form, this device lets you instakill monsters that aren't disintegration resistant."); break;
			case WAN_CHROMATIC_BEAM:
				pline("You can zap this wand to fire beams at enemies. What does the beam do? You won't know until you zap it!"); break;
			case WAN_STUN_MONSTER:
				pline("Point it at a monster or a line of monsters and they will be stunned."); break;
			case WAN_TIDAL_WAVE:
				pline("An evil wand that causes you, and only you, to be immersed in a wave of water when zapped."); break;
			case WAN_SUMMON_ELM:
				pline("Summons hostile divine minions when zapped."); break;
			case WAN_DRAIN_MANA:
				pline("You can get rid of your mana by zapping this wand, but I'm certain you'd never want that effect."); break;
			case WAN_FINGER_BENDING:
				pline("Your fingers will be unable to hold anything for a while after this wand is zapped. Unfortunately it doesn't remove cursed items though."); break;
			case WAN_IMMOBILITY:
				pline("Every time this wand is zapped, a bunch of nonmoving monsters will materialize from nowhere."); break;
			case WAN_EGOISM:
				pline("Zapping this wand creates egotype monsters."); break;
			case WAN_SIN:
				pline("A very dangerous wand that will subject you to nasty 'deadly sin' effects every time it's zapped."); break;
			case WAN_INERTIA:
				pline("Everything you hit with the invisible beam of this wand will be slowed. Period. Monsters with magic resistance are not resistant to this effect."); break;
			case WAN_TIME:
				pline("Time damage is unresistable for all monsters, and will drain a level from them every time it hits."); break;
			case WAN_POLYMORPH:
				pline("Zapping this wand at monsters, objects or yourself will polymorph whatever it hits. Be aware of the fact that polymorphing monsters and objects is temporary."); break;
			case WAN_MUTATION:
				pline("Zapping this wand will add mutations if the invisible beam hits a monster. Hitting yourself or an item will polymorph it."); break;
			case WAN_TELEPORTATION:
#ifdef PHANTOM_CRASH_BUG
				pline("Zap monsters, objects or yourself with this to teleport them. On no-teleport levels you can still teleport monsters away by zapping them."); break;
#else
				pline("This wand can be zapped at monsters and objects to teleport them to a random empty location on the current dungeon level. Zapping yourself is also possible, but only if you're not on a no-teleport level, so in the case of doubt don't zap yourself, but zap the monster attacking you!"); break;
#endif
			case WAN_BANISHMENT:
				pline("A very powerful wand that banishes monsters to a random dungeon level. You may also zap it at yourself, maybe for getting away from a dangerous opponent."); break;
			case WAN_CREATE_HORDE:
				pline("This is a wand of create monster on steroids, which means it will summon a ton of monsters with each zap."); break;
			case WAN_EXTRA_HEALING:
				pline("A more powerful healing wand that restores more hit points than a wand of healing."); break;
			case WAN_FULL_HEALING:
				pline("The ultimate healing wand that restores lots of hit points of whatever you zap it at."); break;
			case WAN_WONDER:
				pline("This wand has a random effect whenever you zap it."); break;
			case WAN_BUGGING:
				pline("Zapping this wand summons bugs. Don't get your hopes up - the bugs never leave corpses, so you won't be able to use this wand for creating sacrifice fodder."); break;
			case WAN_WISHING:
				pline("Probably the most powerful wand in existence, this one allows you to wish for an object every time you zap it. However, it can be recharged at most once."); break;
			case WAN_DESLEXIFICATION:
				pline("Certain monsters (read: all those that don't exist in Vanilla NetHack or regular SLASH'EM) will be deslexified if you zap them with this wand. It can only be generated if you're playing SLASHTHEM Extended."); break;
			case WAN_ACQUIREMENT:
#ifdef PHANTOM_CRASH_BUG
				pline("Can only be recharged once, and allows you to wish for an object class."); break;
#else
				pline("A weaker version of the wand of wishing, this wand allows you to wish for an object class. What you receive exactly is determined randomly, and you can't recharge this wand if it has already been recharged one or more times."); break;
#endif
			case WAN_CLONE_MONSTER:
				pline("Zapping this wand at monsters will create a duplicate of that monster. You can also zap yourself, but unless you're polymorphed into a monster it probably won't work."); break;
			case WAN_CHARGING:
				pline("A wand that allows you to charge an object if you zap it. Beware, since this wand is so powerful you can't charge it more than once (but you can use it to charge itself if it hasn't been recharged yet)."); break;
			case WAN_DIGGING:
#ifdef PHANTOM_CRASH_BUG
				pline("You can zap walls and other obstacles with this to remove them. It creates hard engravings too, engraving up to 50 characters in a single turn."); break;
#else
				pline("Zapping this wand in a direction will try to dig open some walls and other obstacles that are in the way. It can also be used for engraving with good quality, and it can engrave up to 50 characters in a single turn."); break;
#endif
			case WAN_MAGIC_MISSILE:
				pline("This wand can be used to blast the shit out of your enemies by sending a magical ray at them."); break;
			case WAN_FIRE:
				pline("A wand that can engrave words in the floor permanently. It may also be used to shoot bolts of fire in order to burn enemies."); break;
			case WAN_COLD:
				pline("This wand shoots a cold ray that can damage monsters and freeze certain dungeon features."); break;
			case WAN_SLEEP:
				pline("A wand that fires sleep rays in a direction of your choosing."); break;
			case WAN_DEATH:
				pline("This wand allows you to blast enemies with death rays, instantly killing everything that doesn't resist."); break;
			case WAN_LIGHTNING:
				pline("Engravings creates with this wand are permanent, but you will be blinded for a few turns. It can also be zapped to shoot lightning bolts at enemies."); break;
			case WAN_FIREBALL:
				pline("A wand that blasts enemies with powerful fire explosions. It is also a good choice for engraving, being able to create permanent engravings."); break;
			case WAN_ACID:
				pline("Zapping this wand in a direction releases an acid ray to do damage."); break;
			case WAN_SOLAR_BEAM:
				pline("A wand that sends rays of pure solar energy at your enemies."); break;
			case WAN_POISON:
				pline("This wand fires poison beams."); break;
			case WAN_MISFIRE:
				pline("Zapping this wand will always cause it to explode, so don't do that!"); break;
			case WAN_VENOM_SCATTERING:
				pline("A wand that can poison foes."); break;
			case WAN_STONING:
				pline("A wand that fires invisible beams to turn monsters to stone."); break;
			case WAN_CURSE_ITEMS:
				pline("If anyone zaps this wand, your inventory items become cursed."); break;
			case WAN_AMNESIA:
				pline("You should prevent at all costs that anyone zaps this thing, for if it happens, you will suffer from amnesia!"); break;
			case WAN_BAD_LUCK:
				pline("Every zap of this wand reduces your luck by one point, regardless of who zapped it."); break;
			case WAN_REMOVE_RESISTANCE:
				pline("Zapping this wand will remove random intrinsics."); break;
			case WAN_CORROSION:
				pline("A wand that corrodes some of your inventory if you zap it, and also if someone else zaps it."); break;
			case WAN_FUMBLING:
				pline("Zapping this wand will cause you to fumble. It doesn't matter who zapped it either."); break;
			case WAN_STARVATION:
				pline("This wand will reduce your nutrition if you, or anyone else, zaps it."); break;
			case WAN_CONFUSION:
				pline("Zapping this wand will confuse you, regardless of who zapped it."); break;
			case WAN_SLIMING:
				pline("Want to turn to slime? Zap this!"); break;
			case WAN_LYCANTHROPY:
				pline("You will become a werewolf if this wand is zapped by anyone."); break;
			case WAN_PARALYSIS:
				pline("A wand that fires invisible beams to paralyze monsters."); break;
			case WAN_DISINTEGRATION:
				pline("For instances where a wand of death isn't good enough, use this to fire invisible disintegration beams."); break;
			case WAN_GAIN_LEVEL:
				pline("This wand is very powerful - every zap will increase your character level by 1. It can only be recharged once because otherwise it would be uber imba."); break;
			case WAN_MANA:
				pline("Zapping this wand will restore some of your mana."); break;
			case WAN_LEVITATION:
				pline("If anyone zaps this wand, you will levitate, but not at will."); break;
			case WAN_SPELLBINDER:
				pline("A wand that allows you to cast up to five spells at once every time you zap it."); break;
			case WAN_INERTIA_CONTROL:
				pline("Zapping this wand allows you to control the flow of one of your spells (you can select which one), which will automatically be cast once per turn without taking time."); break;
			case WAN_STERILIZE:
				pline("Prevents breeding and egg hatching for a while."); break;
			case WAN_DEBUGGING:
				pline("Can be zapped to reboot your character. This means you will unpolymorph if you were polymorphed, and get your stats/level rerolled."); break;
			case WAN_HYPER_BEAM:
				pline("An attack wand that fires a very powerful beam. Unfortunately, monsters can still resist it, but if they don't, it deals massive damage."); break;
			case WAN_PSYBEAM:
				pline("Fires psychic beams at monsters if zapped. Only monsters that possess psi attacks will resist it, all others take full damage and will also be confused."); break;
			case WAN_INFERNO:
				pline("This wand can create permanent engravings and also fires searing hot flames that will damage a monster's maximum hit points as well as blinding it."); break;
			case WAN_ICE_BEAM:
				pline("Allows you to shoot powerful beams at enemies that cause cold damage and slowness."); break;
			case WAN_THUNDER:
				pline("You can use this wand to burn permanent engravings, or if you zap it at a monster, it causes lightning damage and occasionally paralysis or numbness."); break;
			case WAN_SLUDGE:
				pline("This attack wand shoots highly damaging acid rays that can also level-drain targets."); break;
			case WAN_TOXIC:
				pline("If you fire this wand at a monster that does not resist poison, it will do incredible amounts of damage!"); break;
			case WAN_NETHER_BEAM:
				pline("Only psi-resistant monsters will be able to withstand the might of this wand, all others are going to take a ton of damage."); break;
			case WAN_AURORA_BEAM:
				pline("A wand that fires multicolored beams at monsters which deal lots of damage and can sometimes cancel the target creature."); break;
			case WAN_GRAVITY_BEAM:
				pline("Raw damage to everything it's zapped at. Big monsters take extra damage."); break;
			case WAN_CHLOROFORM:
				pline("This wand allows you to put monsters to sleep and deal damage to them at the same time!"); break;
			case WAN_DREAM_EATER:
				pline("In order to make this wand work, you must point it at a sleeping or paralyzed monster."); break;
			case WAN_BUBBLEBEAM:
				pline("Zap this wand to get rid of monsters that are neither swimming nor unbreathing. It can also sometimes create water tiles."); break;
			case WAN_GOOD_NIGHT:
				pline("Use this wand if you want to get rid of monsters that are aligned with good. All undead partially resist, and evil ones resist even more."); break;

 			default: pline("Missing item description (this is a bug). Please tell Amy about the item in question so she can add a description."); break;

			}

		}
		break;

		case COIN_CLASS:
		pline("%s - This is a coin. ",xname(obj) );
		if (!nn) pline("Unfortunately you don't know more about it. You will gain more information if you identify this item.");
		else { switch (obj->otyp) {

			case GOLD_PIECE: 
				pline("These are zorkmids, also known as the currency of the game. They actually shouldn't be appearing in your main inventory."); break;

 			default: pline("Missing item description (this is a bug). Please tell Amy about the item in question so she can add a description."); break;

			}

		}
		break;

		case GEM_CLASS:
#ifdef PHANTOM_CRASH_BUG
		pline("%s - Color: %s. Material: %s. Appearance: %s.",xname(obj), obj->dknown ? c_obj_colors[objects[obj->otyp].oc_color] : "unknown", (nn && obj->dknown) ? materialnm[objects[obj->otyp].oc_material] : "unknown", obj->dknown ? dn : "unknown");
#else
		pline("%s - This is a gem. Color: %s. Material: %s. Appearance: %s. Some of them increase your score at the end of the game, provided you didn't die, and since ascension is next to impossible, you'll probably not care about score anyway. However, they can also be used as sling ammunition, some gray stones may have certain special effects, and throwing gems to unicorns can increase your luck.",xname(obj), obj->dknown ? c_obj_colors[objects[obj->otyp].oc_color] : "unknown", (nn && obj->dknown) ? materialnm[objects[obj->otyp].oc_material] : "unknown", obj->dknown ? dn : "unknown");
#endif
		if (!nn) pline("Unfortunately you don't know more about it. You will gain more information if you identify this item.");
		else { switch (obj->otyp) {

			case DILITHIUM_CRYSTAL:
				pline("A white gem with a mohs hardness of 5, worth 4500 zorkmids."); break;
			case MAGICITE_CRYSTAL:
				pline("A blue gem with a mohs hardness of 11, worth 5000 zorkmids."); break;
			case WONDER_STONE:
				pline("An invisible gem with a mohs hardness of 13, worth 5000 zorkmids."); break;
			case DIAMOND:
				pline("A white gem with a mohs hardness of 10, worth 4000 zorkmids."); break;
			case CYAN_STONE:
				pline("A teal gem with a mohs hardness of 8, worth 600 zorkmids."); break;
			case MOONSTONE:
				pline("A white gem with a mohs hardness of 6, worth 500 zorkmids."); break;
			case DISTHENE:
				pline("A teal gem with a mohs hardness of 7, worth 400 zorkmids."); break;
			case PERIDOT:
				pline("A radiant gem with a mohs hardness of 7, worth 1200 zorkmids."); break;
			case PREHNITE:
				pline("A radiant gem with a mohs hardness of 6, worth 500 zorkmids."); break;
			case CHALCEDON:
				pline("A cyan gem with a mohs hardness of 7, worth 800 zorkmids."); break;
			case CHRYSOCOLLA:
				pline("A cyan gem with a mohs hardness of 3, worth 800 zorkmids."); break;
			case APOPHYLLITE:
				pline("A teal gem with a mohs hardness of 5, worth 400 zorkmids."); break;
			case ANDALUSITE:
				pline("A radiant gem with a mohs hardness of 7, worth 1500 zorkmids."); break;
			case EPIDOTE:
				pline("A green gem with a mohs hardness of 7, worth 900 zorkmids."); break;
			case CHAROITE:
				pline("A violet gem with a mohs hardness of 6, worth 1200 zorkmids."); break;
			case DIOPTASE:
				pline("A radiant gem with a mohs hardness of 5, worth 2500 zorkmids."); break;
			case RUBY:
				pline("A red gem with a mohs hardness of 9, worth 3500 zorkmids."); break;
			case JACINTH:
				pline("An orange gem with a mohs hardness of 9, worth 3250 zorkmids."); break;
			case ANHYDRITE:
				pline("A teal gem with a mohs hardness of 4, worth 200 zorkmids."); break;
			case HALITE:
				pline("A white gem with a mohs hardness of 2, worth 200 zorkmids."); break;
			case MARBLE:
				pline("A white gem with a mohs hardness of 3, worth 200 zorkmids."); break;
			case SAPPHIRE:
				pline("A blue gem with a mohs hardness of 9, worth 3000 zorkmids."); break;
			case BLACK_OPAL:
				pline("A black gem with a mohs hardness of 8, worth 2500 zorkmids."); break;
			case EMERALD:
				pline("A green gem with a mohs hardness of 8, worth 2500 zorkmids."); break;
			case TURQUOISE:
				pline("A green gem with a mohs hardness of 6, worth 1500 zorkmids."); break;
			case AMAZONITE:
				pline("A cyan gem with a mohs hardness of 6, worth 1000 zorkmids."); break;
			case SODALITH:
				pline("A blue gem with a mohs hardness of 6, worth 1000 zorkmids."); break;
			case VIVIANITE:
				pline("A cyan gem with a mohs hardness of 2, worth 900 zorkmids."); break;
			case KUNZITE:
				pline("A pink gem with a mohs hardness of 7, worth 600 zorkmids."); break;
			case CIRMOCLINE:
				pline("A pink gem with a mohs hardness of 11, worth 4000 zorkmids."); break;
			case CITRINE:
				pline("A yellow gem with a mohs hardness of 6, worth 1500 zorkmids."); break;
			case AQUAMARINE:
				pline("A green gem with a mohs hardness of 8, worth 1500 zorkmids."); break;
			case AMBER:
				pline("A yellowish brown gem with a mohs hardness of 2, worth 1000 zorkmids."); break;
			case LAPIS_LAZULI:
				pline("A blue gem with a mohs hardness of 5, worth 600 zorkmids."); break;
			case TOPAZ:
				pline("A yellowish brown gem with a mohs hardness of 8, worth 900 zorkmids."); break;
			case JET:
				pline("A black gem with a mohs hardness of 7, worth 850 zorkmids."); break;
			case OPAL:
				pline("A white gem with a mohs hardness of 6, worth 800 zorkmids."); break;
			case CHRYSOBERYL:
				pline("A yellow gem with a mohs hardness of 5, worth 700 zorkmids."); break;
			case GARNET:
				pline("A red gem with a mohs hardness of 7, worth 700 zorkmids."); break;
			case SPINEL:
				pline("A pink gem with a mohs hardness of 8, worth 600 zorkmids."); break;
			case AMETHYST:
				pline("A violet gem with a mohs hardness of 7, worth 600 zorkmids."); break;
			case JASPER:
				pline("A red gem with a mohs hardness of 7, worth 500 zorkmids."); break;
			case MALACHITE:
				pline("A green gem with a mohs hardness of 4, worth 800 zorkmids."); break;
			case COVELLINE:
				pline("A black gem with a mohs hardness of 2, worth 700 zorkmids."); break;
			case FLUORITE:
				pline("A violet gem with a mohs hardness of 4, worth 400 zorkmids."); break;
			case MORGANITE:
				pline("A pink gem with a mohs hardness of 8, worth 2000 zorkmids."); break;
			case ORTHOCLASE:
				pline("A yellow gem with a mohs hardness of 6, worth 2000 zorkmids."); break;
			case ROSE_QUARTZ:
				pline("A pink gem with a mohs hardness of 7, worth 700 zorkmids."); break;
			case TOURMALINE:
				pline("A red gem with a mohs hardness of 7, worth 200 zorkmids."); break;
			case MORION:
				pline("A black gem with a mohs hardness of 7, worth 200 zorkmids."); break;
			case RHODOCHROSITE:
				pline("A red gem with a mohs hardness of 4, worth 200 zorkmids."); break;
			case OBSIDIAN:
				pline("A black gem with a mohs hardness of 6, worth 200 zorkmids."); break;
			case AGATE:
				pline("A orange gem with a mohs hardness of 6, worth 200 zorkmids."); break;
			case JADE:
				pline("A green gem with a mohs hardness of 6, worth 300 zorkmids."); break;
			case LUCKSTONE:
				pline("This gray stone influences your luck if you keep it in your main inventory. It can also prevent luck from timing out."); break;
			case HEALTHSTONE:
				pline("Healthstones are usually generated cursed, but if you carry around noncursed ones your health regeneration will speed up."); break;
			case TALC:
				pline("A useless gray stone."); break;
			case GRAPHITE:
				pline("It's a gray stone that doesn't do anything special, but at least it doesn't do anything bad, unlike some other stones..."); break;
			case VOLCANIC_GLASS_FRAGMENT:
				pline("This is an obsidian designed to be fired with a sling."); break;
			case STARLIGHTSTONE:
#ifdef PHANTOM_CRASH_BUG
				pline("It's so heavy that it probably overburdens you, and if you can't get rid of it quickly then you're doomed."); break;
#else
				pline("You simply don't want to have this stone in your inventory, but considering you're looking at its description you apparently do have one... It's so heavy that it probably overburdens you, and if you can't get rid of it quickly then you're doomed."); break;
#endif
			case LOADSTONE:
#ifdef PHANTOM_CRASH_BUG
				pline("A very heavy stone that is usually generated cursed. As long as it is cursed, you can't drop it."); break;
#else
				pline("A very heavy stone that is usually generated cursed. As long as it is cursed, you can't drop it. Trying to pick one up will ignore all restrictions that would usually prevent you from picking up an item, so be careful!"); break;
#endif
			case TOUCHSTONE:
				pline("Rubbing gems on this stone may allow you to find out more about them."); break;
			case SALT_CHUNK:
#ifdef PHANTOM_CRASH_BUG
				pline("The only thing you can do with it is to dip it into potions, and maybe you get a potion of salt water which is also next to useless."); break;
#else
				pline("A very useless gray stone that has been added to the game just to re-obscure the identification of actually useful gray stones. The only thing you can do with it is to dip it into potions, and maybe you get a potion of salt water which is also next to useless."); break;
#endif
			case WHETSTONE:
				pline("This item is meant to be used in conjunction with things that can be sharpened, by rubbing them on it. However, it requires you to be near a source of water."); break;
			case MANASTONE:
				pline("Manastones are usually generated cursed, but if you carry around noncursed ones your energy regeneration will speed up."); break;
			case SLEEPSTONE:
				pline("A gray stone that is usually generated cursed. If you carry it in your open inventory, you will fall asleep even if you are sleep resistant. It also halves the chance of waking up from combat."); break;
			case STONE_OF_MAGIC_RESISTANCE:
				pline("Slotless magic resistance can be obtained by having this stone in your inventory. Beware:  it will curse itself after a while, and if the stone is cursed, you will take double damage from everything!"); break;
			case LOADBOULDER:
#ifdef PHANTOM_CRASH_BUG
				pline("This extremely heavy item is usually generated cursed and can't be dropped unless you uncurse it. Giants can lift it with no problems though."); break;
#else
				pline("This extremely heavy item is usually generated cursed and can't be dropped unless you uncurse it; if you try to pick it up, you will always do so even if it would overburden you! It's okay to pick one up if you are a giant, though."); break;
#endif
			case FLINT:
				pline("A projectile meant to be in conjunction with a sling to do damage to enemies."); break;
			case SMALL_PIECE_OF_UNREFINED_MITHR:
				pline("It's just a disguised rock that can be fired with a sling, and the main purpose it serves is to re-obscure the identification of actually useful gray stones..."); break;
			case SILVER_SLINGSTONE:
				pline("Flint stones are good sling ammunition, but ones made of silver are even better as long as the opponent is a silver-hating monster (certain undead and demons qualify)."); break;
			case ROCK:
				pline("This is a basic rock that can be thrown, but firing it with a sling does more damage."); break;
			case RIGHT_MOUSE_BUTTON_STONE:
				pline("A stone that curses itself and causes the right mouse button to stop working."); break;
		 	case DISPLAY_LOSS_STONE:
				pline("A stone that curses itself and causes the display to fail."); break;
		 	case SPELL_LOSS_STONE:
				pline("A stone that curses itself and causes spell loss."); break;
		 	case YELLOW_SPELL_STONE:
				pline("A stone that curses itself and causes yellow spells."); break;
		 	case AUTO_DESTRUCT_STONE:
				pline("A stone that curses itself and causes an auto destruct mechanism to be initiated."); break;
		 	case MEMORY_LOSS_STONE:
				pline("A stone that curses itself and causes low local memory. This message should never be displayed, yet somehow it is?!"); break;
		 	case INVENTORY_LOSS_STONE:
#ifdef PHANTOM_CRASH_BUG
				pline("A stone that curses itself and causes the memory used for displaying an inventory window to run out. Message shortened because of the phantom crash bug even though it cannot be displayed anyway."); break;
#else
				pline("A stone that curses itself and causes the memory used for displaying an inventory window to run out. You cannot view this message in-game because you can't open your inventory while having this stone in there, so you gotta be peeking at the source! --Amy"); break;
#endif
		 	case BLACKY_STONE:
				pline("A stone that curses itself and causes Blacky to close in on you with his NG walls."); break;
		 	case MENU_BUG_STONE:
				pline("A stone that curses itself and causes the menu bug."); break;
		 	case SPEEDBUG_STONE:
				pline("A stone that curses itself and causes the speed bug."); break;
		 	case SUPERSCROLLER_STONE:
				pline("A stone that curses itself and causes the superscroller effect."); break;
		 	case FREE_HAND_BUG_STONE:
				pline("A stone that curses itself and causes your free hand to be free less often."); break;
		 	case UNIDENTIFY_STONE:
				pline("A stone that curses itself and causes your possessions to unidentify themselves."); break;
		 	case STONE_OF_THIRST:
				pline("A stone that curses itself and causes a strong sense of thirst."); break;
		 	case UNLUCKY_STONE:
				pline("A stone that curses itself and causes you to be shitting out of luck (SOL)."); break;
		 	case SHADES_OF_GREY_STONE:
				pline("A stone that curses itself and causes everything to display in various shades of grey."); break;
		 	case STONE_OF_FAINTING:
				pline("A stone that curses itself and causes random fainting."); break;
		 	case STONE_OF_CURSING:
				pline("A stone that curses itself and causes your inventory to fill up with cursed items."); break;
		 	case STONE_OF_DIFFICULTY:
				pline("A stone that curses itself and causes an arbitrary increase of the game's difficulty."); break;

		 	case NONSACRED_STONE:
				pline("A stone that curses itself and causes altars to malfunction."); break;
		 	case STARVATION_STONE:
				pline("A stone that curses itself and causes you to get less food."); break;
		 	case DROPLESS_STONE:
				pline("A stone that curses itself and causes items to not drop."); break;
		 	case LOW_EFFECT_STONE:
				pline("A stone that curses itself and causes your magic level to be low."); break;
		 	case INVISO_STONE:
				pline("A stone that curses itself and causes invisible traps."); break;
		 	case GHOSTLY_STONE:
				pline("A stone that curses itself and causes invisible monsters."); break;
		 	case DEHYDRATING_STONE:
				pline("A stone that curses itself and causes dehydration."); break;
		 	case STONE_OF_HATE:
				pline("A stone that curses itself and causes pets to hate you."); break;
		 	case DIRECTIONAL_SWAP_STONE:
				pline("A stone that curses itself and causes swapping of your directional keys."); break;
		 	case NONINTRINSICAL_STONE:
				pline("A stone that curses itself and causes you to get no intrinsics from eating corpses."); break;
		 	case DROPCURSE_STONE:
				pline("A stone that curses itself and causes items to autocurse whenever you drop them."); break;
		 	case STONE_OF_NAKED_STRIPPING:
				pline("A stone that curses itself and causes you to be effectively naked."); break;
		 	case ANTILEVEL_STONE:
				pline("A stone that curses itself and causes you to gain no more experience."); break;
		 	case STEALER_STONE:
				pline("A stone that curses itself and causes item stealers to be more dangerous."); break;
		 	case REBEL_STONE:
				pline("A stone that curses itself and causes pets to spontaneously rebel."); break;
		 	case SHIT_STONE:
				pline("A stone that curses itself and causes you to shit whenever you're satiated."); break;
		 	case STONE_OF_MISFIRING:
				pline("A stone that curses itself and causes your projectiles to always misfire."); break;
		 	case STONE_OF_PERMANENCE:
				pline("A stone that curses itself and causes rapid dungeon regrowth."); break;

		 	case DEAFNESS_STONE:
				pline("A stone that curses itself and causes a hearing break."); break;
		 	case ANTIMAGIC_STONE:
				pline("A stone that curses itself and causes blood mana."); break;
		 	case WEAKNESS_STONE:
				pline("A stone that curses itself and causes weakness to damage your health."); break;
 			case ROT_THIRTEEN_STONE:
				pline("A stone that curses itself and causes this message, as well as all others, to display in rot13."); break;
		 	case BISHOP_STONE:
				pline("A stone that curses itself and causes you to be unable to move diagonally."); break;
		 	case CONFUSION_STONE:
				pline("A stone that curses itself and causes a confusing problem."); break;
		 	case DROPBUG_STONE:
				pline("A stone that curses itself and causes you to be unable to drop items."); break;
 			case DSTW_STONE:
				pline("A stone that curses itself and causes potions to sometimes not work."); break;
 			case STATUS_STONE:
				pline("A stone that curses itself and causes status effects to be impossible to cure."); break;

 			case AMNESIA_STONE:
				pline("A stone that curses itself and causes recurring amnesia."); break;
 			case BIGSCRIPT_STONE:
				pline("A stone that curses itself and causes BIGscript."); break;
 			case BANK_STONE:
				pline("A stone that curses itself and transfers money to a bank."); break;
 			case MAP_STONE:
				pline("A stone that curses itself and causes the map bug."); break;
 			case TECHNIQUE_STONE:
				pline("A stone that curses itself and causes bugged techniques."); break;
 			case DISENCHANTMENT_STONE:
				pline("A stone that curses itself and causes disenchantment."); break;
 			case VERISIERT_STONE:
				pline("A stone that curses itself and causes more monsters to spawn."); break;
 			case CHAOS_TERRAIN_STONE:
				pline("A stone that curses itself and causes chaos terrain."); break;
 			case MUTENESS_STONE:
				pline("A stone that curses itself and causes muteness."); break;
 			case ENGRAVING_STONE:
				pline("A stone that curses itself and causes engravings to stop working."); break;
 			case MAGIC_DEVICE_STONE:
				pline("A stone that curses itself and causes magic devices to explode."); break;
 			case BOOK_STONE:
				pline("A stone that curses itself and causes spellbooks to be useless."); break;
 			case LEVEL_STONE:
				pline("A stone that curses itself and causes monsters to level up."); break;
 			case QUIZ_STONE:
				pline("A stone that curses itself and causes quizzes."); break;

		 	case ALIGNMENT_STONE:
				pline("A stone that curses itself and causes your maximum alignment to decrease over time."); break;
		 	case STAIRSTRAP_STONE:
				pline("A stone that curses itself and causes stairs to be always trapped."); break;
			case UNINFORMATION_STONE:
#ifdef PHANTOM_CRASH_BUG
				pline("A stone that curses itself and causes insufficient amounts of information. You cannot be reading this message."); break;
#else
				pline("A stone that curses itself and causes insufficient amounts of information. This message should never appear on the screen because you can only see it if the stone is in your inventory, and the stone prevents this kind of message from being displayed!"); break;
#endif

			case STONE_OF_INTRINSIC_LOSS:
				pline("A stone that curses itself and causes intrinsic loss."); break;
			case BLOOD_LOSS_STONE:
				pline("A stone that curses itself and causes bleedout."); break;
			case BAD_EFFECT_STONE:
				pline("A stone that curses itself and causes bad effects."); break;
			case TRAP_CREATION_STONE:
				pline("A stone that curses itself and causes traps to be generated."); break;
			case STONE_OF_VULNERABILITY:
				pline("A stone that curses itself and causes vulnerability."); break;
			case ITEM_TELEPORTING_STONE:
				pline("A stone that curses itself and causes item teleportation."); break;
			case NASTY_STONE:
				pline("A stone that curses itself and causes nasty effects."); break;
			case FARLOOK_STONE:
				pline("A stone that curses itself and causes farlook problems."); break;
			case CAPTCHA_STONE:
				pline("A stone that curses itself and causes captchas."); break;
			case RESPAWN_STONE:
				pline("A stone that curses itself and causes monster respawn."); break;

			case LOOTCUT_STONE:
				pline("A stone that curses itself and causes lootcuts."); break;
			case MONSTER_SPEED_STONE:
				pline("A stone that curses itself and causes monster speed to increase."); break;
			case SCALING_STONE:
				pline("A stone that curses itself and causes monster minimum level scaling."); break;
			case INIMICAL_STONE:
				pline("A stone that curses itself and causes monsters to spawn hostile."); break;
			case WHITE_SPELL_STONE:
				pline("A stone that curses itself and causes white spells."); break;
			case GREYOUT_STONE:
				pline("A stone that curses itself and causes complete gray spells."); break;
			case QUASAR_STONE:
				pline("A stone that curses itself and causes quasar vision."); break;
			case MOMMY_STONE:
				pline("A stone that curses itself and causes your momma to be insulted."); break;
			case HORROR_STONE:
				pline("A stone that curses itself and causes horrible status effects."); break;
			case ARTIFICIAL_STONE:
				pline("A stone that curses itself and causes evil artifacts to spawn."); break;
			case WEREFORM_STONE:
				pline("A stone that curses itself and causes you to transform into werecreatures."); break;
			case ANTIPRAYER_STONE:
				pline("A stone that curses itself and causes prayer to fail."); break;
			case EVIL_PATCH_STONE:
				pline("A stone that curses itself and causes intrinsic nasty effects."); break;
			case HARD_MODE_STONE:
				pline("A stone that curses itself and causes you to take double damage."); break;
			case SECRET_ATTACK_STONE:
				pline("A stone that curses itself and causes monsters to use secret attacks."); break;
			case EATER_STONE:
				pline("A stone that curses itself and causes monsters to eat items."); break;
			case COVETOUS_STONE:
				pline("A stone that curses itself and causes covetous monsters to act more intelligently."); break;
			case NON_SEEING_STONE:
				pline("A stone that curses itself and causes invisible walls."); break;
			case DARKMODE_STONE:
				pline("A stone that curses itself and causes lit squares to act as if they were unlit."); break;
			case UNFINDABLE_STONE:
				pline("A stone that curses itself and causes you to never find anything when searching."); break;
			case HOMICIDE_STONE:
				pline("A stone that curses itself and causes monsters to build new traps."); break;
			case MULTITRAPPING_STONE:
				pline("A stone that curses itself and causes rare traps to be more common."); break;
			case WAKEUP_CALL_STONE:
				pline("A stone that curses itself and causes peaceful monsters to spontaneously become hostile."); break;
			case GRAYOUT_STONE:
				pline("A stone that curses itself and causes grayout."); break;
			case GRAY_CENTER_STONE:
				pline("A stone that curses itself and causes you to be in a gray center."); break;
			case CHECKERBOARD_STONE:
				pline("A stone that curses itself and causes the checkerboard disease."); break;
			case CLOCKWISE_STONE:
				pline("A stone that curses itself and causes your directional keys to be rotated clockwise."); break;
			case COUNTERCLOCKWISE_STONE:
				pline("A stone that curses itself and causes your directional keys to be rotated counterclockwise."); break;
			case LAG_STONE:
				pline("A stone that curses itself and causes artifical lag."); break;
			case BLESSCURSE_STONE:
				pline("A stone that curses itself and causes blessed items to become cursed."); break;
			case DELIGHT_STONE:
				pline("A stone that curses itself and causes you to make areas unlit."); break;
			case DISCHARGE_STONE:
				pline("A stone that curses itself and causes your devices to run out of charges faster."); break;
			case TRASH_STONE:
				pline("A stone that curses itself and causes your equipment to degrade when you put it on."); break;
			case FILTERING_STONE:
				pline("A stone that curses itself and causes certain messages to not display correctly."); break;
			case DEFORMATTING_STONE:
				pline("A stone that curses itself and causes the pokedex to stop working."); break;
			case FLICKER_STRIP_STONE:
				pline("A stone that curses itself and causes flicker strips on your status line."); break;
			case UNDRESSING_STONE:
				pline("A stone that curses itself and causes you to randomly take off equipment."); break;
			case HYPER_BLUE_STONE:
				pline("A stone that curses itself and causes hyper blue walls."); break;
			case NO_LIGHT_STONE:
				pline("A stone that curses itself and causes the hilite patch to stop working."); break;
			case PARANOIA_STONE:
				pline("A stone that curses itself and causes the paranoid patch to stop working."); break;
			case FLEECE_STONE:
				pline("A stone that curses itself and causes items in your inventory to display fleecy-colored strings."); break;
			case INTERRUPTION_STONE:
				pline("A stone that curses itself and causes consumables to take several turns to use."); break;
			case DUSTBIN_STONE:
				pline("A stone that curses itself and causes scrolls to disintegrate sometimes."); break;
			case BATTERY_STONE:
				pline("A stone that curses itself and causes you to become a living mana battery."); break;
			case BUTTERFINGER_STONE:
				pline("A stone that curses itself and causes you to occasionally break potions."); break;
			case MISCASTING_STONE:
				pline("A stone that curses itself and causes bad effects whenever you cast a spell."); break;
			case MESSAGE_SUPPRESSION_STONE:
				pline("A stone that curses itself and causes no messages to be displayed on the top line anymore. This also means that you cannot be reading this message in-game."); break;
			case STUCK_ANNOUNCEMENT_STONE:
				pline("A stone that curses itself and causes the bottom status line to no longer update automatically."); break;
			case STORM_STONE:
				pline("A stone that curses itself and causes you to become bloodthirsty."); break;
			case MAXIMUM_DAMAGE_STONE:
				pline("A stone that curses itself and causes you to always take maximum damage."); break;
			case LATENCY_STONE:
				pline("A stone that curses itself and causes the game to skip past your commands sometimes."); break;
			case STARLIT_SKY_STONE:
				pline("A stone that curses itself and causes you to not know what monsters are."); break;
			case TRAP_KNOWLEDGE_STONE:
				pline("A stone that curses itself and causes you to not know what traps are."); break;
			case HIGHSCORE_STONE:
				pline("A stone that curses itself and causes nasty things to spawn."); break;
			case PINK_SPELL_STONE:
				pline("A stone that curses itself and causes pink spells."); break;
			case GREEN_SPELL_STONE:
				pline("A stone that curses itself and causes green spells."); break;
			case EVC_STONE:
				pline("A stone that curses itself and causes evencore pictures."); break;
			case UNDERLAID_STONE:
				pline("A stone that curses itself and causes a layer of invisible markers."); break;
			case DAMAGE_METER_STONE:
				pline("A stone that curses itself and causes the showdamage patch to stop working."); break;
			case WEIGHT_STONE:
				pline("A stone that curses itself and causes the showweight and invweight patches to stop working."); break;
			case INFOFUCK_STONE:
				pline("A stone that curses itself and causes you to forget what character you are playing."); break;
			case BLACK_SPELL_STONE:
				pline("A stone that curses itself and causes black spells."); break;
			case CYAN_SPELL_STONE:
				pline("A stone that curses itself and causes cyan spells."); break;
			case HEAP_STONE:
				pline("A stone that curses itself and causes messages to be repeated. A stone that curses itself and causes messages to be repeated."); break;
			case BLUE_SPELL_STONE:
				pline("A stone that curses itself and causes blue spells."); break;
			case TRON_STONE:
				pline("A stone that curses itself and causes you to be unable to walk into the same direction twice."); break;
			case RED_SPELL_STONE:
				pline("A stone that curses itself and causes red spells."); break;
			case TOO_HEAVY_STONE:
				pline("A stone that curses itself and causes you to gain weight when picking up items."); break;
			case ELONGATED_STONE:
				pline("A stone that curses itself and causes hug and breath attacks to have a longer range."); break;
			case WRAPOVER_STONE:
				pline("A stone that curses itself and causes positively enchanted items to turn into negatively enchanted ones."); break;
			case DESTRUCTION_STONE:
				pline("A stone that curses itself and causes random inventory destruction."); break;
			case MELEE_PREFIX_STONE:
				pline("A stone that curses itself and causes you to lose turns if you attack in melee, unless you use a prefix."); break;
			case AUTOMORE_STONE:
				pline("A stone that curses itself and causes --More-- prompts to disappear."); break;
			case UNFAIR_ATTACK_STONE:
				pline("A stone that curses itself and causes monsters to use unfair attacks."); break;

			case METABOLIC_STONE:
				pline("A stone that curses itself and causes faster metabolism."); break;
			case STONE_OF_NO_RETURN:
				pline("A stone that curses itself and causes teleports to fail."); break;
			case EGOSTONE:
				pline("A stone that curses itself and causes egotype monsters to spawn."); break;
			case FAST_FORWARD_STONE:
				pline("A stone that curses itself and causes time to flow by faster."); break;
			case ROTTEN_STONE:
				pline("A stone that curses itself and causes food to be rotten."); break;
			case UNSKILLED_STONE:
				pline("A stone that curses itself and causes skill loss."); break;
			case LOW_STAT_STONE:
				pline("A stone that curses itself and causes lower stats."); break;
			case TRAINING_STONE:
				pline("A stone that curses itself and causes skill training to fail."); break;
			case EXERCISE_STONE:
				pline("A stone that curses itself and causes attribute exercise to fail."); break;
			case TURN_LIMIT_STONE:
				pline("A stone that curses itself and causes your ascension turn limit to decrease."); break;
			case WEAK_SIGHT_STONE:
				pline("A stone that curses itself and causes weak sight."); break;
			case CHATTER_STONE:
				pline("A stone that curses itself and causes random messages to appear instead of real ones."); break;
			case DISCONNECT_STONE:
				pline("A stone that curses itself and causes disconnected staircases."); break;
			case SCREW_STONE:
				pline("A stone that curses itself and causes a very nasty interface screw."); break;
			case BOSSFIGHT_STONE:
				pline("A stone that curses itself and causes boss monsters to spawn more often."); break;
			case ENTIRE_LEVEL_STONE:
				pline("A stone that curses itself and causes rare monsters to become frequent."); break;
			case BONE_STONE:
				pline("A stone that curses itself and causes you to find and leave more bones files."); break;
			case AUTOCURSE_STONE:
				pline("A stone that curses itself and causes equipment to autocurse when worn or wielded."); break;
			case HIGHLEVEL_STONE:
				pline("A stone that curses itself and causes high-level monsters to spawn more often."); break;
			case SPELL_MEMORY_STONE:
				pline("A stone that curses itself and causes rapid spell memory loss."); break;
			case SOUND_EFFECT_STONE:
				pline("A stone that curses itself and causes verbalized sound effects."); break;
			case TIME_USE_STONE:
				pline("A stone that curses itself and causes every action to take time."); break;

 			default: pline("Not much is known about this type of gem, but chances are you're looking at a piece of worthless glass. They are, indeed, worthless."); break;

			}

		}
		break;

		case ROCK_CLASS:
#ifdef PHANTOM_CRASH_BUG
		pline("%s - Color: %s. Material: %s. Appearance: %s.",xname(obj), obj->dknown ? c_obj_colors[objects[obj->otyp].oc_color] : "unknown", obj->dknown ? materialnm[objects[obj->otyp].oc_material] : "unknown", obj->dknown ? obj_descr[obj->otyp].oc_name : "unknown" );
#else
		pline("%s - This is a boulder or statue. Color: %s. Material: %s. Appearance: %s. Boulders can be thrown and are difficult to get past if they're just lying around on the floor, while statues may be reanimated or smashed.",xname(obj), obj->dknown ? c_obj_colors[objects[obj->otyp].oc_color] : "unknown", obj->dknown ? materialnm[objects[obj->otyp].oc_material] : "unknown", obj->dknown ? obj_descr[obj->otyp].oc_name : "unknown" );
#endif
		if (!nn) pline("Unfortunately you don't know more about it. You will gain more information if you identify this item.");
		else { switch (obj->otyp) {

			case BOULDER: 
				pline("A large boulder that weighs a ton. It can be thrown, provided you're strong enough."); break;
			case STATUE: 
				pline("This statue depicts some sort of monster. There may be a way to make it come back to life, or you can smash it to see if it contains items."); break;

 			default: pline("Missing item description (this is a bug). Please tell Amy about the item in question so she can add a description."); break;

			}

		}
		break;

		case BALL_CLASS:
#ifdef PHANTOM_CRASH_BUG
		pline("%s - Color: %s. Material: %s. Appearance: %s.",xname(obj), obj->dknown ? c_obj_colors[objects[obj->otyp].oc_color] : "unknown", obj->dknown ? materialnm[objects[obj->otyp].oc_material] : "unknown", obj->dknown ? obj_descr[obj->otyp].oc_name : "unknown" );
#else
		pline("%s - This is an iron ball. Color: %s. Material: %s. Appearance: %s. You can be chained to one, in which case it will follow you around, but it can also be used as a weapon that uses the flail skill.",xname(obj), obj->dknown ? c_obj_colors[objects[obj->otyp].oc_color] : "unknown", obj->dknown ? materialnm[objects[obj->otyp].oc_material] : "unknown", obj->dknown ? obj_descr[obj->otyp].oc_name : "unknown" );
#endif
		if (!nn) pline("Unfortunately you don't know more about it. You will gain more information if you identify this item.");
		else { switch (obj->otyp) {

			case HEAVY_IRON_BALL: 
				pline("A heavy but damaging weapon that uses the flail skill."); break;
			case QUITE_HEAVY_IRON_BALL: 
				pline("This iron ball is heavier than a standard heavy iron ball but also does more damage. It uses the flail skill."); break;
			case REALLY_HEAVY_IRON_BALL: 
				pline("Rarely found, this flail-class weapon weighs really much but does a ton of damage."); break;
			case EXTREMELY_HEAVY_IRON_BALL: 
				pline("The single most damaging weapon in the entire game. Provided you're capable of lifting and wielding it, this extremely heavy flail-class weapon will smack the living daylights out of your enemies."); break;
			case IMPOSSIBLY_HEAVY_IRON_BALL: 
				pline("You probably won't be able to lift this ball. It seems you somehow managed to get it into your inventory, but now you're probably overburdened and cannot move unless you drop it."); break;

			case HEAVY_STONE_BALL: 
				pline("This ball is made of stone. It uses the flail skill."); break;
			case HEAVY_GLASS_BALL: 
				pline("A breakable ball of glass, which you can swing at enemies to deal good damage. This uses the flail skill."); break;
			case HEAVY_GOLD_BALL: 
				pline("With this very heavy ball, you can do great damage to monsters if you use it as a melee or thrown weapon. The flail skill is used to determine the damage and chance to hit."); break;
			case HEAVY_ELYSIUM_BALL: 
				pline("A ball made of unbreakable material. It does tons of damage on a successful hit, and it uses the flail skill."); break;

			case HEAVY_CLAY_BALL: 
				pline("A very heavy mineral ball that uses the flail skill."); break;
			case HEAVY_GRANITE_BALL: 
				pline("This ball weighs a ton and a half, and is made of stone. If you use it as a weapon, you will exercise the flail skill."); break;
			case HEAVY_CONUNDRUM_BALL: 
				pline("A ball that is nearly unbreakable and does tons of damage. It uses the flail skill."); break;
			case HEAVY_CONCRETE_BALL: 
				pline("It uses the flail skill, is made of stone and does enormous amounts of damage."); break;
			case IMPOSSIBLY_HEAVY_GLASS_BALL: 
				pline("Good luck getting rid of this thing! It's breakable, but it weighs so much that you probably can't do anything other than drop it to the floor..."); break;
			case IMPOSSIBLY_HEAVY_MINERAL_BALL: 
				pline("If you're a lithivore, you can eat this ball; otherwise you probably have to drop it due to its sheer weight."); break;
			case IMPOSSIBLY_HEAVY_ELYSIUM_BALL: 
				pline("This ball cannot be damaged in any way and will prevent you from doing anything as long as it's in your inventory."); break;
			case VERY_HEAVY_BALL:
				pline("A flail-class weapon that weighs a lot."); break;
			case HEAVY_COMPOST_BALL:
				pline("A rather strong, but heavy, flail-class weapon."); break;
			case DISGUSTING_BALL:
				pline("This ball can be eaten if you want to get rid of it, or you can swing it and do lots of damage to monsters. It uses the flail skill."); break;
			case HEAVY_ELASTHAN_BALL:
				pline("A very heavy and very fleecy ball that can be used as a weapon, which will be more effective if you have the flail skill."); break;
			case IMPOSSIBLY_HEAVY_NUCLEAR_BALL:
				pline("Congratulations, you're probably overloaded now. If you can somehow wield and swing this thing, it will do a ton of damage, but my suspicion is that you're better off dropping it."); break;

 			default: pline("Missing item description (this is a bug). Please tell Amy about the item in question so she can add a description."); break;

			}

		}
		break;

		case CHAIN_CLASS:
#ifdef PHANTOM_CRASH_BUG
		pline("%s - Color: %s. Material: %s. Appearance: %s.",xname(obj), obj->dknown ? c_obj_colors[objects[obj->otyp].oc_color] : "unknown", obj->dknown ? materialnm[objects[obj->otyp].oc_material] : "unknown", obj->dknown ? dn : "unknown");
#else
		pline("%s - This is an iron chain. Color: %s. Material: %s. Appearance: %s. They are lightweight flail-class weapons that can be used in melee; if you're punished, one will be created to chain you to an iron ball, but iron chains created by punishment cannot be picked up.",xname(obj), obj->dknown ? c_obj_colors[objects[obj->otyp].oc_color] : "unknown", obj->dknown ? materialnm[objects[obj->otyp].oc_material] : "unknown", obj->dknown ? dn : "unknown");
#endif
		if (!nn) pline("Unfortunately you don't know more about it. You will gain more information if you identify this item.");
		else { switch (obj->otyp) {

			case IRON_CHAIN: 
				pline("A basic iron chain that doesn't do much damage."); break;
			case STONE_CHAIN: 
				pline("A basic stone chain that doesn't do much damage."); break;
			case GLASS_CHAIN: 
				pline("A basic glass chain that doesn't do much damage."); break;
			case ROTATING_CHAIN: 
				pline("This iron chain is relatively heavy but does moderate damage in melee."); break;
			case GOLD_CHAIN: 
				pline("This gold chain is relatively heavy but does moderate damage in melee."); break;
			case CLAY_CHAIN: 
				pline("This mineral chain is relatively heavy but does moderate damage in melee."); break;
			case SCOURGE: 
				pline("A powerful iron chain that also has a considerable weight."); break;
			case ELYSIUM_SCOURGE: 
				pline("A powerful unbreakable chain that also has a considerable weight."); break;
			case GRANITE_SCOURGE: 
				pline("A powerful mineral chain that also has a considerable weight."); break;
			case NUNCHIAKU: 
				pline("This is the second-strongest iron chain in the game that does quite a lot of damage while still being lighter than a heavy iron ball."); break;
			case CONUNDRUM_NUNCHIAKU: 
				pline("This is the second-strongest unbreakable chain in the game that does quite a lot of damage while still being lighter than a heavy iron ball."); break;
			case CONCRETE_NUNCHIAKU: 
				pline("This is the second-strongest mineral chain in the game that does quite a lot of damage while still being lighter than a heavy iron ball."); break;
			case HOSTAGE_CHAIN: 
				pline("An iron chain that weighs the same as a heavy iron ball, yet does more damage."); break;
			case GLASS_HOSTAGE_CHAIN: 
				pline("A glass chain that weighs the same as a heavy iron ball, yet does more damage."); break;
			case MINERAL_HOSTAGE_CHAIN: 
				pline("A mineral chain that weighs the same as a heavy iron ball, yet does more damage."); break;
			case ELYSIUM_HOSTAGE_CHAIN: 
				pline("An unbreakable chain that weighs the same as a heavy iron ball, yet does more damage."); break;
			case HEAVY_CHAIN:
				pline("It's a flail that does little damage and is rather useless."); break;
			case COMPOST_CHAIN:
				pline("This flail-class weapon does moderate damage."); break;
			case DISGUSTING_CHAIN:
				pline("A flail that does relatively good damage. You can eat it to dispose of it."); break;
			case ELASTHAN_CHAIN:
				pline("It's a flail-class weapon that does quite good damage."); break;
			case NUCLEAR_HOSTAGE_CHAIN:
				pline("While it weighs a lot, this type of flail does very good damage if you swing it."); break;

 			default: pline("Missing item description (this is a bug). Please tell Amy about the item in question so she can add a description."); break;

			}

		}
		break;

		case VENOM_CLASS:
#ifdef PHANTOM_CRASH_BUG
		pline("%s - Color: %s. Material: %s. Appearance: %s.",xname(obj), obj->dknown ? c_obj_colors[objects[obj->otyp].oc_color] : "unknown", obj->dknown ? materialnm[objects[obj->otyp].oc_material] : "unknown", obj->dknown ? dn : "unknown");
#else
		pline("%s - This is a splash of venom. Color: %s. Material: %s. Appearance: %s. It can be used in melee or for throwing, but either of those actions will probably use it up.",xname(obj), obj->dknown ? c_obj_colors[objects[obj->otyp].oc_color] : "unknown", obj->dknown ? materialnm[objects[obj->otyp].oc_material] : "unknown", obj->dknown ? dn : "unknown");
#endif
		if (!nn) pline("Unfortunately you don't know more about it. You will gain more information if you identify this item.");
		else { switch (obj->otyp) {

			case BLINDING_VENOM: 
				pline("Splashing an enemy with this venom may blind them."); break;
			case TAIL_SPIKES: 
				pline("A barrage of spikes that can be used to damage enemies."); break;
			case FAERIE_FLOSS_RHING: 
				pline("If you get hit with this, you'll lose an experience level. Monsters will probably be unphased though."); break;
			case ACID_VENOM: 
				pline("Hitting a monster with this thing may deal some acid damage to it."); break;
			case SEGFAULT_VENOM: 
				pline("This item is not dangerous in and of itself, but if you're playing the segfaulter race, it can cause a 'segfault panic' that erases your character."); break;

 			default: pline("Missing item description (this is a bug). Please tell Amy about the item in question so she can add a description."); break;

			}

		}
		break;

		case ILLOBJ_CLASS:
		pline("%s - This is an illegal object. You shouldn't be able to get these at all.",xname(obj) );
		if (!nn) pline("Unfortunately you don't know more about it. You will gain more information if you identify this item.");
		else { switch (obj->otyp) {

			case STRANGE_OBJECT: 
				pline("A strange object that actually has no business being in your inventory. Well, at least it's not a glorkum instead. :-)"); break;

 			default: pline("Missing item description (this is a bug). Please tell Amy about the item in question so she can add a description."); break;

			}

		}
		break;

		default: pline("Missing item class description (this is a bug). Please tell Amy about the item in question so she can add a description."); break;
	}

	if (nn && (obj->oartifact || obj->fakeartifact)) {

		if (obj->oartifact) {

			switch (obj->oartifact) {

				case ART_FIREWALL:
					pline("Artifact specs: +4 to-hit and +4 damage to fire-susceptible monsters, resist fire when wielded, lawful, flame mage sacrifice gift."); break;
				case ART_STING:
					pline("Artifact specs: warns of orcs and +5 to-hit and double damage to them, lawful, aligned with elf race."); break;
				case ART_GIANTKILLER:
					pline("Artifact specs: +5 to-hit and double damage to giants, neutral."); break;
				case ART_QUICK_BLADE:
					pline("Artifact specs: +9 to-hit and +2 damage, lawful."); break;
				case ART_ORCRIST:
					pline("Artifact specs: +5 to-hit and double damage to orcs, lawful, aligned with elf race."); break;
				case ART_DRAGONBANE:
					pline("Artifact specs: +5 to-hit and double damage to dragons."); break;
				case ART_EXCALIBUR:
					pline("Artifact specs: helps searching, +5 to-hit and +10 damage, drain resistance when wielded, lawful, knight sacrifice gift. Occasionally generated if you dip a long sword into a fountain."); break;
				case ART_LUCK_BLADE:
					pline("Artifact specs: acts as a luckstone when wielded, +5 to-hit and +6 damage, chaotic, convict sacrifice gift."); break;
				case ART_SUNSWORD:
					pline("Artifact specs: +5 to-hit and double damage to undead, resist blindness when wielded, lawful."); break;
				case ART_SNICKERSNEE:
					pline("Artifact specs: +8 damage, lawful, samurai sacrifice gift."); break;
				case ART_SWORD_OF_JUSTICE:
					pline("Artifact specs: +5 to-hit and +12 damage to crossaligned monsters, lawful, yeoman sacrifice gift."); break;
				case ART_DEMONBANE:
					pline("Artifact specs: +5 to-hit and double damage to demons, lawful."); break;
				case ART_WEREBANE:
					pline("Artifact specs: +5 to-hit and double damage to werecreatures. Also protects from lycanthropy."); break;
				case ART_GRAYSWANDIR:
					pline("Artifact specs: hallucination resistance when wielded, +5 to-hit and double damage, lawful."); break;
				case ART_SKULLCRUSHER:
					pline("Artifact specs: +3 to-hit and +10 damage, lawful, caveman sacrifice gift."); break;
				case ART_TROLLSBANE:
					pline("Artifact specs: +5 to-hit and double damage to trolls."); break;
				case ART_OGRESMASHER:
					pline("Artifact specs: +5 to-hit and double damage to ogres."); break;
				case ART_REAPER:
					pline("Artifact specs: +5 to-hit and +20 damage, lawful, yeoman sacrifice gift."); break;
				case ART_HOLY_SPEAR_OF_LIGHT:
					pline("Artifact specs: +5 to-hit and +10 damage to undead, can be invoked to light areas, lawful."); break;
				case ART_ROD_OF_LORDLY_MIGHT:
					pline("Artifact specs: +3 to-hit and double damage, lawful, noble sacrifice gift."); break;
				case ART_MAGICBANE:
					pline("Artifact specs: +3 to-hit and +4 damage, stuns targets, magic resistance when wielded, neutral, wizard sacrifice gift. Grants curse resistance."); break;
				case ART_DOOM_CHAINSAW:
					pline("Artifact specs: +20 to-hit and +4 damage, neutral, doom marine sacrifice gift."); break;
				case ART_LUCKBLADE:
					pline("Artifact specs: acts as a luckstone when wielded, +5 to-hit and +6 damage, neutral, aligned with gnome race."); break;
				case ART_SWORD_OF_BALANCE:
					pline("Artifact specs: +2 to-hit and +10 damage to crossaligned monsters, neutral."); break;
				case ART_FROST_BRAND:
					pline("Artifact specs: +5 to-hit and double damage to cold-susceptible monsters, cold resistance when wielded."); break;
				case ART_FIRE_BRAND:
					pline("Artifact specs: +5 to-hit and double damage to fire-susceptible monsters, fire resistance when wielded."); break;
				case ART_VORPAL_BLADE:
					pline("Artifact specs: +5 to-hit and +2 damage, beheads targets, neutral."); break;
				case ART_DISRUPTER:
					pline("Artifact specs: +5 to-hit and +30 damage to undead, neutral, priest sacrifice gift."); break;
				case ART_MJOLLNIR:
					pline("Artifact specs: +5 to-hit and +24 damage to shock-susceptible monsters, can be thrown with a strength of 25, neutral, valkyrie sacrifice gift."); break;
				case ART_GAUNTLETS_OF_DEFENSE:
					pline("Artifact specs: half physical damage when worn, can be invoked for invisibility, neutral, monk sacrifice gift."); break;
				case ART_MIRRORBRIGHT:
					pline("Artifact specs: reflection and hallucination resistance when worn, neutral, healer sacrifice gift."); break;
				case ART_DELUDER:
					pline("Artifact specs: stealth and acts as a luckstone when worn, neutral, wizard sacrifice gift."); break;
				case ART_WHISPERFEET:
					pline("Artifact specs: stealth and acts as a luckstone when worn, neutral, tourist sacrifice gift."); break;
				case ART_GRIMTOOTH:
					pline("Artifact specs: +2 to-hit and +6 damage, chaotic, aligned with orc race."); break;
				case ART_DEEP_FREEZE:
					pline("Artifact specs: +5 to-hit and +6 damage to cold-susceptible monsters, cold resistance when wielded, chaotic, ice mage sacrifice gift."); break;
				case ART_SERPENT_S_TONGUE:
					pline("Artifact specs: +2 to-hit and double damage, chaotic, necromancer sacrifice gift. Does extra poison damage."); break;
				case ART_MARAUDER_S_MAP:
					pline("Artifact specs: Can be invoked to detect objects. Special effects if read. Chaotic, pirate sacrifice gift."); break;
				case ART_CLEAVER:
					pline("Artifact specs: +3 to-hit and +6 damage, neutral, barbarian sacrifice gift."); break;
				case ART_DOOMBLADE:
					pline("Artifact specs: +10 damage, can sometimes do extra damage, chaotic."); break;
				case ART_SEA_GULL:
					pline("Artifact specs: +2 to-hit and +4 damage to fire-susceptible monsters."); break;
				case ART_JUNGLE_GUARD:
					pline("Artifact specs: Regeneration and acid resistance when wielded, +1 to-hit and +10 damage to acid-susceptible monsters."); break;
				case ART_DARK_MOON_RISING:
					pline("Artifact specs: Warning when wielded, +4 to-hit and +8 damage."); break;
				case ART_DIGGING_DOG:
					pline("Artifact specs: Helps searching when wielded, +2 to-hit and +12 damage."); break;
				case ART_WORLD_S_LARGEST_COCK:
					pline("Artifact specs: ESP when wielded, +20 to-hit and +20 damage to Team @, chaotic."); break;
				case ART_STORMBRINGER_S_LITTLE_BROT:
					pline("Artifact specs: drain resistance when wielded, +1 to-hit and +2 drain life damage, chaotic."); break;
				case ART_THORN_ROSE:
					pline("Artifact specs: +5 to-hit and +2 damage to fire-susceptible monsters, fire resistance when wielded, lawful."); break;
				case ART_BLUEWRATH:
					pline("Artifact specs: +5 to-hit and +2 damage to cold-susceptible monsters, cold resistance when wielded, neutral."); break;
				case ART_KAMEHAMEHADOKEN:
					pline("Artifact specs: +8 to-hit and +16 damage."); break;
				case ART_ELECTRIFIER:
					pline("Artifact specs: +5 to-hit and +2 damage to shock-susceptible monsters, shock resistance when wielded."); break;
				case ART_DOUBLE_BESTARD:
					pline("Artifact specs: +20 damage. The misspelling is intentional."); break;
				case ART_GUARDIAN_OF_ARANOCH:
					pline("Artifact specs: +20 damage."); break;
				case ART_DULLSWANDIR:
					pline("Artifact specs: Hallucination resistance when wielded."); break;
				case ART_GOLDSWANDIR:
					pline("Artifact specs: Hallucination resistance when wielded, +5 to-hit and +10 damage."); break;
				case ART_SOUNDING_IRON:
					pline("Artifact specs: +2 to-hit and +6 damage."); break;
				case ART_FIRMNAIL:
					pline("Artifact specs: Acts as a luckstone and gives fire resistance when wielded, +1 to-hit and +8 damage to fire-susceptible monsters."); break;
				case ART_SUPERCLEAN_DESEAMER:
					pline("Artifact specs: Protection and stealth when wielded, +10 to-hit and +4 damage."); break;
				case ART_SOOTHING_FAN:
					pline("Artifact specs: Energy regeneration when wielded."); break;
				case ART_GENERIC_JAPANESE_MELEE_WEA:
					pline("Artifact specs: Half physical damage and half spell damage when wielded."); break;
				case ART_THWACK_WHACKER:
					pline("Artifact specs: +10 to-hit and +48 damage to undead, lawful."); break;
				case ART_EVENING_STAR:
					pline("Artifact specs: ESP when wielded, +10 to-hit and +60 damage to lights."); break;
				case ART_FLOGGING_RHYTHM:
					pline("Artifact specs: +4 to-hit and +16 damage, neutral."); break;
				case ART_MODIFIED_POCKET_CALCULATOR:
					pline("Artifact specs: +24 to-hit and +2 damage."); break;
				case ART_BITCHWHIPPER:
					pline("Artifact specs: warns of female monsters, +5 to-hit and +24 damage to female monsters."); break;
				case ART_ORC_MAGIC:
					pline("Artifact specs: Magic resistance when worn."); break;
				case ART_ANTI_DISENCHANTER:
					pline("Artifact specs: Disenchantment resistance when worn."); break;
				case ART_HOT_AND_COLD:
					pline("Artifact specs: Cold resistance when worn."); break;
				case ART_GLORIOUS_DEAD:
					pline("Artifact specs: Reflection and magic resistance while carried."); break;
				case ART_PRECIOUS_WISH:
					pline("Artifact specs: Magic resistance while carried."); break;
				case ART_TROLLED_BY_THE_RNG:
					pline("Artifact specs: +5 to-hit and +10 damage."); break;
				case ART_STARCRAFT_FLAIL:
					pline("Artifact specs: Half spell damage when wielded, +1 to-hit and double damage, chaotic."); break;
				case ART_PWNHAMMER:
					pline("Artifact specs: Half physical damage and cold resistance when wielded, +5 to-hit and +16 damage to cold-susceptible monsters."); break;
				case ART_PWNHAMMER_DUECE:
					pline("Artifact specs: Regeneration and fire resistance when wielded, +8 to-hit and +24 damage to fire-susceptible monsters."); break;
				case ART_DOCKSIDE_WALK:
					pline("Artifact specs: Teleport control when wielded, +2 to-hit and +10 damage."); break;
				case ART_KARATE_KID:
					pline("Artifact specs: Free action when wielded, +5 to-hit and +16 damage, lawful."); break;
				case ART_GIRLFUL_BONKING:
					pline("Artifact specs: aggravate monster, diarrhea and reduced carry capacity when wielded and also causes claw attacks to do extra damage to you, +20 to-hit and +30 damage."); break;
				case ART_ARMOR_PIERCING_HUG:
					pline("Artifact specs: Protection and shock resistance when wielded, +50 to-hit and +2 damage."); break;
				case ART_ASIAN_WINTER:
					pline("Artifact specs: Aggravates monsters and fire resistance when wielded, +4 to-hit and +18 damage to cold-susceptible monsters, chaotic."); break;
				case ART_FRENCH_MARIA:
					pline("Artifact specs: Warning and acid resistance when wielded, +2 to-hit and +12 damage."); break;
				case ART_FORCE_INDIA:
					pline("Artifact specs: Protection when wielded, +20 to-hit and double damage, chaotic."); break;
				case ART_STUPIDITY_IN_MOTION:
					pline("Artifact specs: Drain resistance and reflection when wielded, +1 to-hit and +2 level-drain damage, neutral."); break;
				case ART_SEXY_NURSE_SANDAL:
					pline("Artifact specs: Regeneration and energy regeneration when wielded, chaotic."); break;
				case ART_TENDER_BEAUTY:
					pline("Artifact specs: +5 to-hit and +12 damage."); break;
				case ART_MASSIVE_BUT_LOVELY:
					pline("Artifact specs: Stealth when wielded, +6 to-hit and +18 damage."); break;
				case ART_SWEETHEART_PUMP:
					pline("Artifact specs: Psi resistance when wielded, +15 to-hit and +2 damage."); break;
				case ART_SANDRA_S_EVIL_MINDDRILL:
					pline("Artifact specs: Aggravates monsters, searching and shock resistance when wielded. Can randomly cause amnesia. +32 damage, chaotic."); break;
				case ART_RIBCRACKER:
					pline("Artifact specs: +8 to-hit and +16 damage."); break;
				case ART_DULL_METAL:
					pline("Artifact specs: +1 to-hit and +20 damage."); break;
				case ART_GNARLWHACK:
					pline("Artifact specs: Fire and hallucination resistance as well as searching when wielded, +5 to-hit and +8 damage to fire-susceptible monsters."); break;
				case ART_FIRE_LEADER:
					pline("Artifact specs: Acts as a luckstone, +9 to-hit and +24 damage to fire-susceptible monsters, can be invoked to untrap, neutral."); break;
				case ART_FUMATA_YARI:
					pline("Artifact specs: speed and acid resistance when wielded, +2 to-hit and +16 damage to acid-susceptible monsters."); break;
				case ART_NON_SUCKER:
					pline("Artifact specs: +6 to-hit and +12 damage."); break;
				case ART_DIMOAK_S_HEW:
					pline("Artifact specs: Blindness resistance when wielded, +8 damage."); break;
				case ART_LAND_KNIGHT_PIERCER:
					pline("Artifact specs: Acts as a luckstone when wielded, +2 to-hit and +10 damage."); break;
				case ART_APPLY_B:
					pline("Artifact specs: ESP and stealth when wielded, +1 to-hit and +20 damage."); break;
				case ART_COCK_APPLICATION:
					pline("Artifact specs: Protection when wielded, +3 to-hit and +16 level-drain damage."); break;
				case ART_NOT_A_HAMMER:
					pline("Artifact specs: Reflection when wielded, +4 to-hit and +16 damage to cold-susceptible monsters. This is not a hammer-class weapon."); break;
				case ART_IT_S_A_POLEARM:
					pline("Artifact specs: Magic resistance when wielded, +4 to-hit and +16 damage to shock-susceptible monsters. This is a polearm-class weapon."); break;
				case ART_BEC_DE_ASCORBIN:
					pline("Artifact specs: +1 to-hit and +4 damage, can be applied for healing, lawful."); break;
				case ART_PALEOLITHIC_RELIC:
					pline("Artifact specs: +2 to-hit and +8 damage."); break;
				case ART_BRONZE_AGE_RELIC:
					pline("Artifact specs: +2 to-hit and +12 damage."); break;
				case ART_MISGUIDED_MISSILE:
					pline("Artifact specs: Teleport control when wielded, +16 damage."); break;
				case ART_MARE_S_SPECIAL_ROCKET:
					pline("Artifact specs: Reflection and cold resistance when wielded, +8 to-hit and +16 damage to cold-susceptible monsters, lawful."); break;
				case ART_LIGHTNING_BLADE:
					pline("Artifact specs: +2 to-hit and +12 damage to shock-susceptible monsters."); break;
				case ART_FISHING_GRANDPA:
					pline("Artifact specs: Warns of eels and adds +20 to-hit and +40 damage versus eels."); break;
				case ART_STATIC_STICK:
					pline("Artifact specs: shock resistance when wielded, +4 to-hit and +14 damage to shock-susceptible monsters."); break;
				case ART_PEOPLE_EATING_TRIDENT:
					pline("Artifact specs: sight bonus when wielded, warns of Team @ and adds +8 to-hit and double damage versus all @."); break;
				case ART_MADELINE_S_GUARDIAN:
					pline("Artifact specs: Reflection when wielded. If you're interested in its owner, keep your filthy paws off her unless you're AmyBSOD! She's mine, you hear?"); break;
				case ART_PENGUIN_S_THRUSTING_SWORD:
					pline("Artifact specs: flying when wielded, +12 to-hit and +18 damage, chaotic. It sure looks like a sword to me..."); break;
				case ART_LACKWARE:
					pline("Artifact specs: +1 to-hit and +2 damage."); break;
				case ART_WILD_HUNT:
					pline("Artifact specs: Searching, protection and fire resistance when wielded, +2 to-hit and +16 damage to fire-susceptible monsters, chaotic."); break;
				case ART_BUFFY_AMMO:
					pline("Artifact specs: +10 to-hit and +40 damage to demons."); break;
				case ART_HEAVY_HITTER_ARROW:
					pline("Artifact specs: +5 to-hit and double damage, lawful."); break;
				case ART_AGORA:
					pline("Artifact specs: +14 damage."); break;
				case ART_UPGRADED_LEMURE:
					pline("Artifact specs: +5 to-hit and +18 damage, searching when wielded."); break;
				case ART_WALTHER_PPK:
					pline("Artifact specs: +2 to-hit and +10 damage."); break;
				case ART_DESERT_EAGLE:
					pline("Artifact specs: +5 to-hit and +18 damage."); break;
				case ART_INGRAM_MAC___:
					pline("Artifact specs: speed when wielded."); break;
				case ART_FN_M____PARA:
					pline("Artifact specs: aggravates monsters when wielded, +16 damage."); break;
				case ART_SUREFIRE_GUN:
					pline("Artifact specs: Improves dexterity when wielded."); break;
				case ART_MOSIN_NAGANT:
					pline("Artifact specs: Searching when wielded, +20 to-hit and +30 damage."); break;
				case ART_LEONE_M__GUAGE_SUPER:
					pline("Artifact specs: +40 damage."); break;
				case ART_CITYKILLER_COMBAT_SHOTGUN:
					pline("Artifact specs: Reflection when wielded, +10 damage."); break;
				case ART_SMUGGLERS_END:
					pline("Artifact specs: Fire resistance when wielded, +10 to-hit and +2 damage."); break;
				case ART_COLONEL_BASTARD_S_LASER_PI:
					pline("Artifact specs: +10 to-hit and +10 damage."); break;
				case ART_COOKIE_CUTTER:
					pline("Artifact specs: Reflection and magic resistance when wielded."); break;
				case ART_DOOMGUY_S_WET_DREAM:
					pline("Artifact specs: +2 to-hit and double damage."); break;
				case ART_GRAND_DADDY:
					pline("Artifact specs: +20 to-hit and +20 damage to fire-susceptible monsters."); break;
				case ART_EXTRA_FIREPOWER:
					pline("Artifact specs: +2 to-hit and +10 damage."); break;
				case ART_DEMON_MACHINE:
					pline("Artifact specs: infravision when wielded, +6 to-hit and +12 damage to fire-susceptible monsters."); break;
				case ART_ICBM:
					pline("Artifact specs: +20 to-hit and +2 damage."); break;
				case ART_BEARKILLER:
					pline("Artifact specs: +5 to-hit and +50 damage to thick-skinned monsters."); break;
				case ART_PUNCTURE_MISSILE:
					pline("Artifact specs: +8 to-hit and +40 damage to @-class monsters."); break;
				case ART_INSTANT_DEATH:
					pline("Artifact specs: +100 damage."); break;
				case ART_NEEDLE_LIKE_THE_NEW_LOG:
					pline("Artifact specs: +7 to-hit and double damage."); break;
				case ART_CATWOMANBANE:
					pline("Artifact specs: +10 to-hit and +80 damage to cats, lawful."); break;
				case ART_DOCTOR_JONES__AID:
					pline("Artifact specs: Warns of snakes, +5 to-hit and +4 damage to snakes."); break;
				case ART_CRUEL_PUNISHER:
					pline("Artifact specs: aggravates monsters and energy regeneration when wielded, +2 to-hit and +12 damage, chaotic."); break;
				case ART_BRISTLY_STRING:
					pline("Artifact specs: Monsters take damage if they melee you. +3 to-hit and +12 damage to fire-susceptible monsters."); break;
				case ART_POLICE_BRUTALITY:
					pline("Artifact specs: +4 to-hit and +14 damage."); break;
				case ART_DEMONSTRANTS_GO_HOME:
					pline("Artifact specs: Warning and fire resistance when wielded, +6 to-hit and +16 damage."); break;
				case ART_WE_ARE_NOT_OPPRESSIVE:
					pline("Artifact specs: Reflection when wielded, +8 to-hit and +16 damage, lawful."); break;
				case ART_DEATH_TO_SHOPLIFTERS:
					pline("Artifact specs: Can behead targets."); break;
				case ART_LEGENDARY_SHIRT:
					pline("Artifact specs: Reflection and protection when worn."); break;
				case ART_SEXY_CONVICTS:
					pline("Artifact specs: ESP and drain resistance when worn."); break;
				case ART_NOBILITY_WORLDWIDE:
					pline("Artifact specs: Energy regeneration when worn."); break;
				case ART_PEACE_ADVOCATE:
					pline("Artifact specs: Free action and warning of Team @ when worn."); break;
				case ART_GENTLE_SOFT_CLOTHING:
					pline("Artifact specs: poison and disintegration resistance when worn."); break;
				case ART_HELEN_S_DISCARDED_SHIRT:
					pline("Artifact specs: aggravate monsters and teleport control when worn, acts as a luckstone."); break;
				case ART_ANTIMAGIC_SHELL:
					pline("Artifact specs: Magic resistance when worn, prevents spellcasting."); break;
				case ART_MEMORIAL_GARMENTS:
					pline("Artifact specs: Keen memory and curse resistance when worn, lawful."); break;
				case ART_TOTAL_CONTROL:
					pline("Artifact specs: Confusion and stun resistance when worn, neutral."); break;
				case ART_VICTORIA_IS_EVIL_BUT_PRETT:
					pline("Artifact specs: Polymorph control and manaleech when worn, chaotic."); break;
				case ART_MEDICAL_POWER_ARMOR_PROTOT:
					pline("Artifact specs: Protection when worn, can be invoked for healing."); break;
				case ART_AS_HEAVY_AS_IT_IS_UGLY:
					pline("Artifact specs: regeneration when worn."); break;
				case ART_VOLUME_ARMAMENT:
					pline("Artifact specs: Reflection, magic resistance and superscrolling when worn."); break;
				case ART_FUCKING_ORICHALCUM:
					pline("Artifact specs: magic resistance when worn."); break;
				case ART_STEALTH_SUIT:
					pline("Artifact specs: stealth when worn."); break;
				case ART_LITTLE_BIG_MIDGET:
					pline("Artifact specs: free action when worn, neutral."); break;
				case ART_CATHAN_S_NETWORK:
					pline("Artifact specs: regeneration and bonus to strength when worn."); break;
				case ART_FLY_LIKE_AN_EAGLE:
					pline("Artifact specs: flying when worn."); break;
				case ART_PRETTY_LITTLE_MAGICAL_GIRL:
					pline("Artifact specs: manaleech when worn by a female character."); break;
				case ART_PLAYBOY_WITH_EARS:
					pline("Artifact specs: increased charisma when worn."); break;
				case ART_ANTISEPSIS_COAT:
					pline("Artifact specs: sickness resistance when worn."); break;
				case ART_FALCET:
					pline("Artifact specs: regeneration and cold resistance when worn."); break;
				case ART_SUPERESCAPE_MAIL: /* trap! no warning for the player :-P --Amy */
					pline("Artifact specs: It looks like a normal ring mail."); break;
				case ART_GRAYSCALE_WANDERER:
					pline("Artifact specs: Warning, magic resistance and shades of grey if worn."); break;
				case ART_CD_ROME_ARENA:
					pline("Artifact specs: Regeneration when worn, slows the player down."); break;
				case ART_CHASTITY_ARMOR:
					pline("Artifact specs: Prevents you from contracting slexually transmitted diseases when worn."); break;
				case ART_LAURA_CROFT_S_BATTLEWEAR:
					pline("Artifact specs: Allows you to swim in lava when worn."); break;
				case ART_OFFENSE_OWNS_DEFENSE:
					pline("Artifact specs: Double attacks when worn."); break;
				case ART_PROTECTION_WITH_A_PRICE:
					pline("Artifact specs: Stun and hallucination resistance when worn, grants 5 extra points of AC."); break;
				case ART_CUTE_IDEA:
					pline("Artifact specs: Half physical damage when worn."); break;
				case ART_ALL_HAIL_THE_MIGHTY_RNG:
					pline("Artifact specs: Protection when worn, acts as a luckstone, neutral."); break;
				case ART_HO_OH_S_FEATHERS:
					pline("Artifact specs: Magic resistance, aggravate monster and conflict when worn, neutral."); break;
				case ART_UPGRADE_THIS:
					pline("Artifact specs: Searching when worn."); break;
				case ART_DON_SUICUNE_DOES_NOT_APPRO:
					pline("Artifact specs: reflection, drain resistance, aggravate monster and nastiness when worn."); break;
				case ART_PRETTY_PUFF:
					pline("Artifact specs: cold resistance when worn."); break;
				case ART_INVISIBLE_VISIBILITITY:
					pline("Artifact specs: The Amy is a troll and made this thing grant invisibility. :-P"); break;
				case ART_VISIBLE_INVISIBILITITY:
					pline("Artifact specs: see invisible when worn."); break;
				case ART_BLACKCLOAK:
					pline("Artifact specs: drain resistance when worn, chaotic."); break;
				case ART_EVELINE_S_LAB_COAT:
					pline("Artifact specs: stealth, shock resistance and acts like a luckstone when worn, neutral."); break;
				case ART_INA_S_LAB_COAT:
					pline("Artifact specs: searching, cold, disintegration and sickness resistance, hunger and random fainting when worn; autocurses."); break;
				case ART_SUPERMAN_S_SUPER_SUIT:
					pline("Artifact specs: Quad attacks, regeneration and half physical damage when worn."); break;
				case ART_FULL_WINGS:
					pline("Artifact specs: flying, magic and hallucination resistance when worn."); break;
				case ART_BROKEN_WINGS:
					pline("Artifact specs: disables flying, aggravates monsters, freezes you solid and autocurses when worn."); break;
				case ART_ACQUIRED_POISON_RESISTANCE:
					pline("Artifact specs: poison resistance when worn."); break;
				case ART_IT_S_A_WONDERFUL_FAILURE:
					pline("Artifact specs: magic resistance when worn, chaotic."); break;
				case ART_RITA_S_DECEPTIVE_MANTLE: /* certainly no warning! --Amy */
					pline("Artifact specs: A nice-looking cloak that was manufactured in Rita's Lingerie Studio. Chaotic."); break;
				case ART_STUNTED_HELPER:
					pline("Artifact specs: regeneration when worn."); break;
				case ART_INSUFFICIENT_PROTECTION:
					pline("Artifact specs: warning when worn."); break;
				case ART_MEMORY_AID:
					pline("Artifact specs: keen memory when worn."); break;
				case ART_FREQUENT_BUT_WEAK_STATUS:
					pline("Artifact specs: stun and confusion resistance and protection when worn."); break;
				case ART_A_REASON_TO_LIVE:
					pline("Artifact specs: reflection and magic resistance when worn, deactivates teleport control."); break;
				case ART_FULL_MOON_TONIGHT:
					pline("Artifact specs: acts as a luckstone when worn."); break;
				case ART_ALL_IN_ONE_ASCENSION_KIT:
					pline("Artifact specs: magic and drain resistance and reflection when worn."); break;
				case ART_RADAR_CLOAK:
					pline("Artifact specs: warning and ESP when worn."); break;
				case ART_HOSTES_AD_PULVEREM_FERIRE:
					pline("Artifact specs: boost strength and dexterity and gives acid resistance when worn."); break;
				case ART_UBERJACKAL_EFFECT:
					pline("Artifact specs: aggravate monster when worn."); break;
				case ART_VARIATIO_DELECTAT:
					pline("Artifact specs: lawful."); break;
				case ART_SPEEDRUNNER_S_DREAM:
					pline("Artifact specs: speed when worn."); break;
				case ART_CAN_T_KILL_WHAT_YOU_CAN_T_:
					pline("Artifact specs: see invisible, invisibility and hallucination resistance when worn."); break;
				case ART_IMAGE_PROJECTOR:
					pline("Artifact specs: displacement, teleport control and half spell damage when worn."); break;
				case ART_SILENT_NOISE:
					pline("Artifact specs: stealth when worn."); break;
				case ART_DARK_ANGELS:
					pline("Artifact specs: flying when worn, chaotic."); break;
				case ART_SKILLS_BEAT_STATS:
					pline("Artifact specs: faster skill training when worn."); break;
				case ART_BARON_VON_RICHTHOFEN_S_PRE:
					pline("Artifact specs: magic resistance when worn."); break;
				case ART_RNG_S_GAMBLE:
					pline("Artifact specs: reflection and magic resistance when worn, chaotic."); break;
				case ART_FIGHTRIGHT:
					pline("Artifact specs: searching when worn, neutral."); break;
				case ART_VITAMIN_B:
					pline("Artifact specs: ESP when worn, lawful."); break;
				case ART_SPECTRAL_RESISTANCE:
					pline("Artifact specs: fire, cold, shock and poison resistance when worn, neutral."); break;
				case ART_GIANT_WOK:
					pline("Artifact specs: protection and hallucination resistance when worn."); break;
				case ART_PLAYBOY_SUPPLEMENT:
					pline("Artifact specs: boosts charisma when worn."); break;
				case ART_REAL_SPEED_DEVIL:
					pline("Artifact specs: makes the player very fast indeed, that is, faster than speed boots!"); break;
				case ART_ROYAL_TIARA:
					pline("Artifact specs: ESP, teleport control and searching when worn."); break;
				case ART_FUNCTIONAL_RADIO:
					pline("Artifact specs: shock resistance when worn, allows you to listen to the radio."); break;
				case ART_WARNER_BROTHER:
					pline("Artifact specs: warning when worn."); break;
				case ART_DARK_NADIR:
					pline("Artifact specs: improves your to-hit when worn, but autocurses and creates darkness every once in a while."); break;
				case ART_LOVING_DEITY:
					pline("Artifact specs: protection when worn."); break;
				case ART_COW_ENCHANTMENT:
					pline("Artifact specs: adds 9 points of AC."); break;
				case ART_THOR_S_MYTHICAL_HELMET:
					pline("Artifact specs: half physical damage and half spell damage when worn."); break;
				case ART_CLANG_COMPILATION:
					pline("Artifact specs: protection and shock resistance when worn."); break;
				case ART_SURFACE_TO_AIR_SITE:
					pline("Artifact specs: multishot bonus when worn."); break;
				case ART_MASSIVE_CRANIUM:
					pline("Artifact specs: half physical damage when worn."); break;
				case ART_BURGER_EATER:
					pline("Artifact specs: hallucination resistance when worn."); break;
				case ART_OMNISCIENT:
					pline("Artifact specs: warning when worn."); break;
				case ART_SPACEWARP:
					pline("Artifact specs: teleport control when worn."); break;
				case ART_DICTATORSHIP:
					pline("Artifact specs: polymorph control when worn."); break;
				case ART_SAFE_INSECURITY:
					pline("Artifact specs: half physical damage and half spell damage when worn."); break;
				case ART_YOU_ARE_ALREADY_DEAD:
					pline("Artifact specs: magic and drain resistance and reflection when worn, chaotic."); break;
				case ART_SHPX_GUVF_FUVG:
					pline("Artifact specs: searching, stealth and unbreathing when worn."); break;
				case ART_GO_OTHER_PLACE:
					pline("Artifact specs: teleportitis when worn."); break;
				case ART_BEESWAX_HELMET:
					pline("Artifact specs: acts as a luckstone when worn."); break;
				case ART_LOW_LOCAL_MEMORY:
					pline("Artifact specs: free action and drain resistance when worn."); break;
				case ART_SEVERE_AGGRAVATION:
					pline("Artifact specs: aggravate monster when worn :-)."); break;
				case ART_FORMFILLER:
					pline("Artifact specs: heavily curses itself when worn."); break;
				case ART_STONE_EROSION:
					pline("Artifact specs: petrification resistance when worn."); break;
				case ART_BLINDING_FOG:
					pline("Artifact specs: blindness resistance when worn."); break;
				case ART_BIG_BONNET:
					pline("Artifact specs: poison resistance and protection when worn."); break;
				case ART_EULOGY_S_EULOGY:
					pline("Artifact specs: protection and stealth when worn, chaotic."); break;
				case ART_MIND_SHIELDING:
					pline("Artifact specs: stun resistance when worn."); break;
				case ART_CONSPIRACY_THEORY:
					pline("Artifact specs: ESP when worn, neutral."); break;
				case ART_BOX_FIST:
					pline("Artifact specs: +5 martial arts damage when worn."); break;
				case ART_SWING_LESS_CAST_MORE:
					pline("Artifact specs: energy regeneration when worn."); break;
				case ART_MOLASS_TANK:
					pline("Artifact specs: 10 extra points of AC and magic resistance when worn."); break;
				case ART_SCIENCE_FLICTION:
					pline("Artifact specs: regeneration and energy regeneration when worn."); break;
				case ART_AFK_MEANS_ASS_FUCKER:
					pline("Artifact specs: autocurses when worn and greatly speeds up all item-stealing monsters."); break;
				case ART_SIGNONS_STEEL_TOTAL:
					pline("Artifact specs: fire, poison, cold and petrification resistance when worn."); break;
				case ART_DOUBLE_LUCK:
					pline("Artifact specs: acts as a luckstone when worn."); break;
				case ART_PLUG_AND_PRAY:
					pline("Artifact specs: reflection when worn."); break;
				case ART_GAUNTLETS_OF_SLAYING:
					pline("Artifact specs: improve strength and dexterity."); break;
				case ART_IRIS_S_PRECIOUS_METAL:
					pline("Artifact specs: reflection when worn, demons are often spawned peaceful, chaotic."); break;
				case ART_FLICTIONLESS_COMBAT:
					pline("Artifact specs: magic resistance when worn, also has some secret properties."); break;
				case ART_COME_BACK_TO_LIFE:
					pline("Artifact specs: while wearing them, you may sometimes come back to life after death."); break;
				case ART_SCROLLSCROLLSCROLL:
					pline("Artifact specs: increases drop rate of certain useful scroll types while worn."); break;
				case ART_SEALED_KNOWLEDGE:
					pline("Artifact specs: half physical damage, reflection and ESP when worn."); break;
				case ART_FIFTY_SHADES_OF_FUCKED_UP:
					pline("Artifact specs: protection when worn and also boosts martial arts damage by 5 and whip damage by 10."); break;
				case ART_ARABELLA_S_BANK_OF_CROSSRO: /* don't mention the negative effects on purpose --Amy */
					pline("Artifact specs: Allows you to do bank transfers with Arabella's private bank when worn."); break;
				case ART_OUT_OF_CONTROL:
					pline("Artifact specs: teleportitis when worn, and disables teleport control."); break;
				case ART_SHADOWDISK:
					pline("Artifact specs: blindness resistance when worn, neutral."); break;
				case ART_LURTZ_S_WALL:
					pline("Artifact specs: boosts block rate by 20%% and free action while worn, chaotic."); break;
				case ART_AEAEAEAEAEGIS:
					pline("Artifact specs: 10 extra points of AC, neutral."); break;
				case ART_SHATTERED_DREAMS:
					pline("Artifact specs: autocurses, aggravate monster and conflict when worn, lawful."); break;
				case ART_BURNING_DISK:
					pline("Artifact specs: burns you and autocurses."); break;
				case ART_TYPE_OF_ICE_BLOCK_HATES_YO:
					pline("Artifact specs: freezes you and autocurses."); break;
				case ART_NUMBED_CAN_T_DO:
					pline("Artifact specs: numbs you and autocurses."); break;
				case ART_VENOMAT:
					pline("Artifact specs: stuns you and autocurses."); break;
				case ART_THEY_MUST_ALL_DIE:
					pline("Artifact specs: ESP when worn."); break;
				case ART_WHANG_CLINK_CLONK:
					pline("Artifact specs: 5 extra points of AC and 10%% extra chance to block."); break;
				case ART_REFLECTOR_EJECTOR:
					pline("Artifact specs: teleportitis and reflection when worn."); break;
				case ART_LITTLE_THORN_ROSE:
					pline("Artifact specs: causes thorns damage to attackers and disables sleep resistance when worn. You know the fairy tale of Little Thorn Rose, right?"); break;
				case ART_TEH_BASH_R:
					pline("Artifact specs: +2 damage to your weapon attacks while worn."); break;
				case ART_TOO_GOOD_TO_BE_TRUE:
					pline("Artifact specs: drain resistance when worn, lawful."); break;
				case ART_SOLAR_POWER:
					pline("Artifact specs: sight bonus when worn, neutral."); break;
				case ART_BRASS_GUARD:
					pline("Artifact specs: free action when worn, neutral."); break;
				case ART_SYSTEMATIC_CHAOS:
					pline("Artifact specs: conflict and sustain ability when worn, autocurses, chaotic."); break;
				case ART_GOLDEN_DAWN:
					pline("Artifact specs: increased monster spawn rate and keen memory when worn, autocurses, lawful."); break;
				case ART_REAL_PSYCHOS_WEAR_PURPLE:
					pline("Artifact specs: psi resistance, farlook bug and hate effect when worn, autocurses, chaotic."); break;
				case ART_BINDER_CRASH:
					pline("Artifact specs: count as stiletto heels for binders, everyone else will anger their god and cause them to heavily curse themselves. Very rarely they transform a non-Binder into a Binder instead, but if they don't, then they don't, so do not try repeatedly!"); break;
				case ART_MEPHISTO_S_BROGUES:
					pline("Artifact specs: poison and cold resistance and flying when worn, disables fire resistance and autocurses, chaotic."); break;
				case ART_GNOMISH_BOOBS:
					pline("Artifact specs: increases charisma when worn, neutral."); break;
				case ART_KOKYO_NO_PAFOMANSUU_OKU:
					pline("Artifact specs: reflection, hallucination resistance and increased charisma when worn."); break;
				case ART_LITTLE_GIRL_S_REVENGE:
					pline("Artifact specs: magic resistance when worn. Unfortunately I cannot give them a 'resistance to attacks from evil parents' effect as well..."); break;
				case ART_AMYBSOD_S_VAMPIRIC_SNEAKER:
					pline("Artifact specs: drain resistance and blood loss when worn. By the way, AmyBSOD actually ran a half marathon with them untrained, in 2 hours and 38 minutes."); break;
				case ART_CINDERELLA_S_SLIPPERS:
					pline("Artifact specs: prism reflection, aggravate monster and wounded legs when worn. Why are you trying to wear a pair of shoes that's too small for your feet anyway?"); break;
				case ART_EVELINE_S_LOVELIES:
					pline("Artifact specs: shock resistance, increased charisma and +5 kicking damage when worn, neutral."); break;
				case ART_NATALIA_S_PUNISHER:
					pline("Artifact specs: petrification resistance and +8 damage with hammer-class weapons while worn. Natalia likes to use them to beat her children."); break;
				case ART_ELLA_S_BLOODLUST:
					pline("Artifact specs: magic resistance, double attacks and aggravate monster when worn, autocurses, chaotic."); break;
				case ART_ANASTASIA_S_GENTLENESS:
					pline("Artifact specs: free action, reflection and -10 strength when worn, chaotic."); break;
				case ART_KATRIN_S_PARALYSIS:
					pline("Artifact specs: cold and shock resistance when worn, kicking a monster with them causes it to be stuck to you."); break;
				case ART_EVA_S_INCONSPICUOUS_CHARM:
					pline("Artifact specs: fire resistance and increased charisma and dexterity when worn."); break;
				case ART_SOLVEJG_S_STINKING_SLIPPER:
					pline("Artifact specs: energy regeneration, aggravate monster, shock resistance, manaleech, improved charisma but reduced intelligence and wisdom when worn. Heavily curse themselves, neutral."); break;
				case ART_JESSICA_S_TENDERNESS:
					pline("Artifact specs: cold, sleep and psi resistance when worn, neutral."); break;
				case ART_LEATHER_PUMPS_OF_HORROR:
					pline("Artifact specs: poison resistance and improved charisma when worn."); break;
				case ART_LILAC_BEAUTY:
					pline("Artifact specs: reflection, magic resistance and stealth when worn, increase charisma but reduce all other stats, taking them off drains your experience level."); break;
				case ART_RHEA_S_COMBAT_PUMPS:
					pline("Artifact specs: poison and sickness resistance when worn, deals passive poison damage to monsters, neutral."); break;
				case ART_MANDY_S_ROUGH_BEAUTY:
					pline("Artifact specs: free action and improved charisma when worn, +10 kicking damage, chaotic."); break;
				case ART_I_M_A_BITCH__DEAL_WITH_IT:
					pline("Artifact specs: aggravate monster, grants 5 extra points of AC and sets itself to +0 when worn if the enchantment was negative."); break;
				case ART_MANUELA_S_TORTURE_HEELS:
					pline("Artifact specs: ESP, magic resistance, conflict and aggravate monster when worn, heavily curse themselves. Their former owner used them to stomp little kittens to death. Chaotic."); break;
				case ART_BEAUTIFUL_TOPMODEL:
					pline("Artifact specs: greatly increased charisma when worn, neutral."); break;
				case ART_CORINA_S_UNFAIR_SCRATCHER:
					pline("Artifact specs: cold, shock and petrification resistance and flying when worn, neutral."); break;
				case ART_SPORKED:
					pline("Artifact specs: ESP and regeneration when worn."); break;
				case ART_HERMES__UNFAIRNESS:
					pline("Artifact specs: unbreathing and aggravate monster when worn, monsters are always spawned hostile because it's Hermes' fault that Orpheus lost his wife!"); break;
				case ART_YET_ANOTHER_STUPID_IDEA:
					pline("Artifact specs: fire and petrification resistance, acts as a luckstone when worn, lawful."); break;
				case ART_HOT_FLAME:
					pline("Artifact specs: cold resistance when worn."); break;
				case ART_JESUS_FOOTWEAR:
					pline("Artifact specs: wearing them prevents your inventory from getting wet."); break;
				case ART_CURSING_ANOMALY:
					pline("Artifact specs: autocurses (AmyBSOD really likes to put this property on artifacts); reflection, searching and itemcursing when worn."); break;
				case ART_FUN_ALL_IN_ONE:
					pline("Artifact specs: conflict, unbreathing, aggravate monster and reflection when worn."); break;
				case ART_LOLLERSKATES:
					pline("Artifact specs: fire resistance and teleport control when worn."); break;
				case ART_DESERTWALK:
					pline("Artifact specs: fire resistance when worn."); break;
				case ART_WAITING_TIMEOUT:
					pline("Artifact specs: free action when worn."); break;
				case ART_NOSE_ENCHANTMENT:
					pline("Artifact specs: fire resistance, half physical damage and polymorphitis when worn."); break;
				case ART_FANTASTIC_SHOES:
					pline("Artifact specs: They like to talk to you."); break;
				case ART_UNTRAINED_HALF_MARATHON:
					pline("Artifact specs: wounded legs and speed when worn. Don't try it in real life!"); break;
				case ART_BLACK_DIAMOND_ICON:
					pline("Artifact specs: multiplies the monster spawn rate by 4 when worn."); break;
				case ART_RIDDLE_ME_THIS:
					pline("Artifact specs: magic resistance, causes the Riddler to test your knowledge when worn."); break;
				case ART_BASE_FOR_SPEED_ASCENSION:
					pline("Artifact specs: sickness resistance when worn."); break;
				case ART_PARANOIA_STRIDE:
					pline("Artifact specs: fear and hallucination resistance and stealth when worn."); break;
				case ART_DING_DONG_PING_PONG:
					pline("Artifact specs: reflection and teleport control when worn."); break;
				case ART_RING_OF_WOE:
					pline("Artifact specs: aggravate monster, hunger and conflict when worn. Also prime curses itself, do you REALLY want to wear it???"); break;
				case ART_WEREFOO_GO_HOME:
					pline("Artifact specs: ESP when worn."); break;
				case ART_SECRET_DETECTIVE:
					pline("Artifact specs: ESP and searching when worn."); break;
				case ART_MAGIC_SIGNET:
					pline("Artifact specs: magic resistance when worn."); break;
				case ART_EAT_MORE_KITTENS:
					pline("Artifact specs: reflection when worn, autocurses."); break;
				case ART_RING_OF_THE_SCHWARTZ:
					pline("Artifact specs: disintegration resistance when worn, chaotic."); break;
				case ART_AFTERTHOUGHT:
					pline("Artifact specs: searching when worn."); break;
				case ART_POLAR_OPPOSITES:
					pline("Artifact specs: acid resistance when worn."); break;
				case ART_WIN_OR_LOSE:
					pline("Artifact specs: ESP, teleport control and acts as a luckstone when worn."); break;
				case ART_CRYLOCK:
					pline("Artifact specs: improves AC by 10 points and heavily curses itself when worn."); break;
				case ART_GOLDENIVY_S_RAGE:
					pline("Artifact specs: aggravate monster, teleportitis, teleport control, sickness resistance and flying when worn."); break;
				case ART_TEAM_NASTYTRAP_S_BAUBLE:
					pline("Artifact specs: reflection and drain resistance when worn."); break;
				case ART_FLOAT_EYELER_S_CONDITION:
					pline("Artifact specs: levitation when worn, because bhaak likes to float around."); break;
				case ART_SKILL_LESS_OF_THE_SERVICE:
					pline("Artifact specs: reflection and magic resistance when worn."); break;
				case ART_FATALLY_LOW:
					pline("Artifact specs: ESP, searching, half spell damage and acts as a luckstone when worn."); break;
				case ART_CRIMINAL_QUEEN:
					pline("Artifact specs: speed and increased charisma when worn, chaotic."); break;
				case ART_LIFE_SUCKS:
					pline("Artifact specs: just try putting it on! :-)"); break;
				case ART_BESTESTOR:
					pline("Artifact specs: ESP when worn."); break;
				case ART_TRAITORIOUS_DEVIL:
					pline("Artifact specs: drain resistance when worn."); break;
				case ART_WARNED_AND_PROTECTED:
					pline("Artifact specs: poison resistance when worn."); break;
				case ART_CONTROLLED_TELEPORTITIS:
					pline("Artifact specs: teleport control when worn."); break;
				case ART_GUARDIAN_ANGLE:
					pline("Artifact specs: prism reflection when worn."); break;
				case ART_SNOREFEST:
					pline("Artifact specs: makes you relatively resistant to sleep and aggravates monsters when worn."); break;
				case ART_PRECIOUS_UNOBTAINABLE_PROP:
					pline("Artifact specs: warp reflection and stun, psi, petrification and sickness resistance when worn."); break;
				case ART_BALLSY_BASTARD:
					pline("Artifact specs: free action, petrification and drain resistance and manaleech when worn."); break;
				case ART_FIX_EVERYTHING:
					pline("Artifact specs: sustain ability when worn."); break;
				case ART_ONLY_ONE_ESCAPE:
					pline("Artifact specs: wearing it allows you to jump."); break;
				case ART_OH_COME_ON:
					pline("Artifact specs: shock resistance and charisma bonus when worn."); break;
				case ART_AMULET_OF_CARLAMMAS:
					pline("Artifact specs: ESP when worn."); break;
				case ART_COMPUTER_AMULET:
					pline("Artifact specs: keen memory when worn."); break;
				case ART_STINGING_MEDALLION:
					pline("Artifact specs: disintegration resistance and flying when worn."); break;
				case ART_RECOVERED_RELIC:
					pline("Artifact specs: stun resistance when worn."); break;
				case ART_TYRANITAR_S_QUEST:
					pline("Artifact specs: while wearing it, your techniques don't always get a timeout after being used."); break;
				case ART_SPACE_CYCLE:
					pline("Artifact specs: teleportitis and polymorphitis when worn."); break;
				case ART_NECKLACE_OF_ADORNMENT:
					pline("Artifact specs: charisma boost when worn."); break;
				case ART_GOOD_BEE:
					pline("Artifact specs: sight bonus and poison resistance when worn."); break;
				case ART_WHERE_DID_IT_GO:
					pline("Artifact specs: teleport control when worn."); break;
				case ART_CONFUSTICATOR:
					pline("Artifact specs: searching when worn, but also confuses you."); break;
				case ART_EEH_EEH:
					pline("Artifact specs: energy regeneration when worn."); break;
				case ART___TH_NAZGUL:
					pline("Artifact specs: regeneration, half physical damage, free action and manaleech when worn, but disables drain resistance and heavily curses itself"); break;
				case ART_VERY_TRICKY_INDEED:
					pline("Artifact specs: Applying it will spawn more monsters than usual."); break;
				case ART_ONE_SIZE_FITS_EVERYTHING:
					pline("Artifact specs: Greatly reduces the weight of stuff you put into it."); break;
				case ART_STONESPLITTER:
					pline("Artifact specs: +2 to-hit and +10 damage, searching when wielded."); break;
				case ART_DARKENING_THING:
					pline("Artifact specs: +3 to-hit and +8 damage, applying it aggravates monsters."); break;
				case ART_ROOMMATE_S_SPECIAL_IDEA:
					pline("Artifact specs: Double damage to team x and can sometimes behead them."); break;
				case ART_LIGHTS__CAMERA__ACTION:
					pline("Artifact specs: taking photos with it can scare nearby monsters."); break;
				case ART_FAIREST_IN_THE_LAND:
					pline("Artifact specs: Applying it at a hostile nymph will pacify her."); break;
				case ART_EYES_OF_THE_SPYING_ACADEMY:
					pline("Artifact specs: ESP and searching when worn. Property of Team Splat."); break;
				case ART_BLINDFOLD_OF_MISPELLING:
					pline("Artifact specs: Causes a confusing problem and autocurses when worn."); break;
				case ART_ANSWER_IS___:
					pline("Artifact specs: If you fool around with this thing, the universe will display its own wicked sense of humor."); break;
				case ART_PENIS_SAFETY:
					pline("Artifact specs: Your penis gains magic resistance if you wear this, and your remaining body does too."); break;
				case ART_KNIGHT_S_AID:
					pline("Artifact specs: A lawful saddle."); break;
				case ART_VROOM_VROOM:
					pline("Artifact specs: A neutral saddle."); break;
				case ART_MOTORCYCLE_RACE:
					pline("Artifact specs: A chaotic saddle."); break;
				case ART_YASDORIAN_S_TROPHY_GETTER:
					pline("Artifact specs: Always generates blessed tins and also summons monsters. If it summons a boss and you manage to kill it, be sure to shout 'TROPHY GET!'"); break;
				case ART_YASDORIAN_S_JUNETHACK_IDEN:
					pline("Artifact specs: magic and psi resistance when wielded."); break;
				case ART_TIN_FU:
					pline("Artifact specs: +20 damage when used as a weapon, supermarket cashier sacrifice gift."); break;
				case ART_VIBE_LUBE:
					pline("Artifact specs: Instantly applies the full 3 layers of grease with only one charge."); break;
				case ART_GUARANTEED_SPECIAL_PET:
					pline("Artifact specs: Always generates a tame monster, unless it is of a species that cannot be tamed."); break;
				case ART_PEN_OF_RANDOMNESS:
					pline("Artifact specs: Randomly chooses the BUC status of target scrolls."); break;
				case ART_EGG_OF_SPLAT:
					pline("Artifact specs: Eating it makes you deathly sick."); break;
				case ART_HOE_PA:
					pline("Artifact specs: Eating it gives temporary resistances to fire, cold, shock and poison."); break;
				case ART_YASDORIAN_S_PARTLY_EATEN_T:
					pline("Artifact specs: Eating it gives intrinsic magic resistance and nastiness, and disables poison and sickness resistance for longer than this game will last."); break;
				case ART_BOOMSHINE:
					pline("Artifact specs: Quaff this explosive drink at your own peril. It does not explode if you throw it at a monster, though."); break;
				case ART_CURSED_PARTS:
					pline("Artifact specs: Arabella created this potion specially for you, %s!", plname); break;
				case ART_PLANECHANGERS:
					pline("Artifact specs: Wanna polymorph forever? Then quaff away!"); break;
				case ART_SANDMAN_VOLUME__:
					pline("Artifact specs: Drain resistance when wielded."); break;
				case ART_AND_YOUR_MORTAL_WORLD_SHAL:
					pline("Artifact specs: Fire resistance when wielded."); break;
				case ART_SOURCE_CODES_OF_WORK_AVOID:
					pline("Artifact specs: Free action and flying when wielded."); break;
				case ART_ERU_ILUVATAR_S_BIBLE:
					pline("Artifact specs: Poison resistance when wielded, and if you die while wielding it, you have a small chance of coming back to life."); break;
				case ART_ORTHODOX_MANIFEST:
					pline("Artifact specs: Drain resistance when wielded, and you can turn undead in Gehennom while wielding it."); break;
				case ART_SECRETS_OF_INVISIBLE_PLEAS:
					pline("Artifact specs: Invisibility and see invisible when wielded."); break;
				case ART_ACTA_METALLURGICA_VOL___:
					pline("Artifact specs: Acid resistance and 5 extra points of AC when wielded."); break;
				case ART_IBM_GUILD_MANUAL: /* if Biskup can be evil, I can be too :-P (yes, there's no description, this is intentional) --Amy */
					pline("Artifact specs: unknown."); break;
				case ART_NOTHING_VENTURED_NOTHING_G:
					pline("Artifact specs: Does nothing."); break;
				case ART_KNOW_YOUR_INTRINSICS:
					pline("Artifact specs: Improves your intelligence and wisdom when wielded."); break;
				case ART_STEALING_PROTECTION:
					pline("Artifact specs: Has no special effect beyond its base item."); break;
				case ART_RARE_FINDINGS:
					pline("Artifact specs: Searching when wielded."); break;
				case ART_SNARE_BEGONE:
					pline("Artifact specs: Has no special effect beyond its base item."); break;
				case ART_FOOK_YOO:
					pline("Artifact specs: Has no special effect beyond its base item."); break;
				case ART_OVERLEVELER:
					pline("Artifact specs: Has no special effect beyond its base item."); break;
				case ART_UN_DEATH:
					pline("Artifact specs: Drain resistance when wielded."); break;
				case ART_NOT_KNOWN_ANYMORE:
					pline("Artifact specs: Allows secure identification of items."); break;
				case ART_USELESSNESS_OF_PLENTY:
					pline("Artifact specs: resist fear when wielded."); break;
				case ART_ARABELLA_S_SECRET_WEAPON:
					pline("Artifact specs: None of your business! Arabella does not need you poking your nose in her operations!"); break;
				case ART_AWOL_PARTY:
					pline("Artifact specs: Has no special effect beyond its base item."); break;
				case ART_FUNNY_SEGFAULT:
					pline("Artifact specs: Has no special effect beyond its base item."); break;
				case ART_BUNGA_BUNGA:
					pline("Artifact specs: Increses charisma when wielded."); break;
				case ART_SOVETSKAYA_PYAT__LO_NENAVI:
					pline("Artifact specs: Has no special effect beyond its base item. However, the type of ice block hates you."); break;
				case ART_FAEAEAEAEAEAU:
					pline("Artifact specs: Fire resistance when wielded."); break;
				case ART_DIGGER_GEM:
					pline("Artifact specs: +5 to-hit and +16 damage to acid-susceptible monsters."); break;
				case ART_KHOR_S_CURSE:
					pline("Artifact specs: well, see for yourself, you picked it up after all, hahaha! :D"); break;
				case ART_STRANGE_PROTECTOR:
					pline("Artifact specs: protection when wielded."); break;
				case ART_ROADRASH_WEAPON:
					pline("Artifact specs: +16 damage."); break;
				case ART_VUVUZELA:
					pline("Artifact specs: reflection when wielded, aggravates monsters when applied."); break;
				case ART_CLARENT:
					pline("Artifact specs: acts as a luckstone when wielded, +8 to-hit and +2 damage to thick-skinned monsters, lawful."); break;
				case ART_SHADOWBLADE:
					pline("Artifact specs: stealth when wielded, +5 to-hit and +6 damage, chaotic."); break;
				case ART_YOICHI_NO_YUMI:
					pline("Artifact specs: +20 to-hit and double damage, lawful."); break;
				case ART_KIKU_ICHIMONJI:
					pline("Artifact specs: +4 to-hit and +12 damage, lawful."); break;
				case ART_ITLACHIAYAQUE:
					pline("Artifact specs: half spell damage and fire resistance when wielded, lawful. No one can spell the name correctly."); break;
				case ART_QUEEN_S_GUARD:
					pline("Artifact specs: +6 to-hit and +6 damage, lawful."); break;
				case ART_PEACEKEEPER:
					pline("Artifact specs: +4 to-hit and +8 damage to crossaligned monsters, lawful."); break;
				case ART_RESTKEEPER:
					pline("Artifact specs: +4 to-hit and +8 damage to crossaligned monsters, lawful. The type of ice block (Soviet5lo) created this item in case someone wants to compile firearms out of SLASHTHEM, which will never happen. :-P"); break;
				case ART_ICEBITER:
					pline("Artifact specs: cold resistance when wielded, +6 to-hit and +6 damage to cold-susceptible monsters, lawful."); break;
				case ART_SHOCK_BRAND:
					pline("Artifact specs: shock resistance when wielded, +5 to-hit and double damage to shock-susceptible monsters."); break;
				case ART_ACID_BRAND:
					pline("Artifact specs: acid resistance when wielded, +5 to-hit and double damage to acid-susceptible monsters."); break;
				case ART_SNAKESKIN:
					pline("Artifact specs: hallucination resistance, protection and acid resistance when worn, neutral. Soviet, the filthy heretic, actually deferred the role for which this artifact was originally intended..."); break;
				case ART_POSEIDON_S_TREASURE:
					pline("Artifact specs: +10 to-hit and +10 damage to shock-susceptible monsters, neutral."); break;
				case ART_GLADIUS:
					pline("Artifact specs: +8 to-hit and +6 damage, neutral. Someone ought to tell Soviet that he made an artifact sword that's basically called 'sword' (lol)."); break;
				case ART_HRUNTING:
					pline("Artifact specs: +4 to-hit and +4 damage, neutral. In SLASHTHEM this thing is associated with the warrior role and the author of that fork completely forgot that the warrior role is Elder Scrolls-themed."); break;
				case ART_DEBUGGER:
					pline("Artifact specs: shock resistance when wielded, +5 to-hit and +4 damage to shock-susceptible monsters, neutral."); break;
				case ART_ACIDTESTER:
					pline("Artifact specs: acid resistance when wielded, +5 to-hit and +4 damage to acid-susceptible monsters, neutral. According to Soviet the name is temporary."); break;
				case ART_STRAIGHTSHOT:
					pline("Artifact specs: +10 to-hit and +2 damage, neutral."); break;
				case ART_SHIMMERSTAFF:
					pline("Artifact specs: +8 to-hit and +4 damage, neutral."); break;
				case ART_FULL_METAL_JACKET:
					pline("Artifact specs: protection, fire and hallucination resistance when worn, neutral. The original author says that the name of this artifact would be temporary for some reason."); break;
				case ART_TESLA_S_COAT:
					pline("Artifact specs: half physical damage and shock resistance when worn, neutral."); break;
				case ART_OATHKEEPER:
					pline("Artifact specs: +7 to-hit and +8 damage, neutral. According to Soviet5lo the name is temporary but I decided to keep it."); break;
				case ART_BLACKSHROUD:
					pline("Artifact specs: warning, drain resistance and acts as a luckstone when worn, neutral."); break;
				case ART_SILVERSHARD:
					pline("Artifact specs: +2 to-hit and double damage."); break;
				case ART_MIRROR_BRAND:
					pline("Artifact specs: reflection when wielded, +5 to-hit and double stun damage to crossaligned monsters, neutral."); break;
				case ART_DIRK:
					pline("Artifact specs: +5 to-hit and +4 damage, neutral. Soviet admits that he created this artifact because 'he couldn't think of something better for musicians'..."); break;
				case ART_TENDERIZER:
					pline("Artifact specs: +3 to-hit and +6 damage, neutral. The idea was probably stolen from Fallout 3."); break;
				case ART_FUNGISWORD:
					pline("Artifact specs: hallucination resistance when wielded, +10 to-hit and double damage to fungi, lawful."); break;
				case ART_DIPLOMAT:
					pline("Artifact specs: +10 to-hit and +24 damage to monsters with proper names, neutral."); break;
				case ART_PETSLAYER:
					pline("Artifact specs: +5 to-hit and +10 damage to domestic creatures, chaotic."); break;
				case ART_MOUSER_S_SCALPEL:
					pline("Artifact specs: +5 to-hit and +2 damage, neutral. Misleading name because it's not a scalpel in the first place."); break;
				case ART_GRAYWAND:
					pline("Artifact specs: +3 to-hit and +6 damage to cold-susceptible monsters, neutral. Not even remotely a wand. No idea what the original author of this artifact intended."); break;
				case ART_HEARTSEEKER:
					pline("Artifact specs: +3 to-hit and +2 damage, neutral."); break;
				case ART_CAT_S_CLAW:
					pline("Artifact specs: +5 to-hit and +6 damage to rodents, neutral."); break;
				case ART_NIGHTINGALE:
					pline("Artifact specs: +6 to-hit and +2 damage, chaotic. In SLASHTHEM this is the ninja's sacrifice gift, and it's notably much worse than the samurai artifacts."); break;
				case ART_BLOODMARKER:
					pline("Artifact specs: +3 to-hit and +6 damage, chaotic."); break;
				case ART_SHAWSHANK:
					pline("Artifact specs: +9 to-hit and +8 damage, chaotic. Soviet5lo created it for the Gangster role, being oblivious to the fact that the gangster is based on the Grand Theft Auto series."); break;
				case ART_SPINESEEKER:
					pline("Artifact specs: +5 to-hit and +4 damage, chaotic."); break;
				case ART_CROWN_ROYAL_CLOAK:
					pline("Artifact specs: protection, acid resistance and acts as a luckstone when worn, neutral."); break;
				case ART_GAMBLER_S_SUIT:
					pline("Artifact specs: protection and acts as a luckstone when worn."); break;
				case ART_WAND_OF_MIGHT:
					pline("Artifact specs: Someone insisted on putting an artifact wand of wishing in the game and you are the lucky fellow who found it. Congratulations. Now wish for an ascension kit please."); break;
				case ART_WARFORGER:
					pline("Artifact specs: fire resistance when wielded, +15 to-hit and +14 damage, neutral. Originally intended to be carried by Durin the Blacksmith, maybe?"); break;
				case ART_SLING_OF_DAVID:
					pline("Artifact specs: half physical damage when wielded, +5 to-hit and double damage, neutral. No, Soviet, sling bullets fired by this thing will not instakill giants."); break;
				case ART_GOLDEN_WHISTLE_OF_NORA:
					pline("Artifact specs: warning, half physical damage and ESP when wielded, lawful. The type of ice block came up with this beautiful name and says it's TEMPORARY??? I don't wanna know what terrible name it will have in future SLASHTHEM versions."); break;
				case ART_FUMA_ITTO_NO_KEN:
					pline("Artifact specs: drain resistance when wielded, +8 to-hit and +8 damage to crossaligned monsters, chaotic."); break;
				case ART_PICK_OF_THE_GRAVE:
					pline("Artifact specs: cold resistance, regeneration, half physical damage, teleport control, aggravate monster and hunger when wielded, +8 to-hit and +10 level-drain damage, neutral."); break;
				case ART_FLUTE_OF_SLIME:
					pline("Artifact specs: warning, teleport control and shock resistance when wielded, chaotic. According to Soviet the name is temporary, and in this case I agree that it sounds not all that great."); break;
				case ART_FIRE_CHIEF_HELMET:
					pline("Artifact specs: warning, protection, half spell damage, half physical damage and weak sight when worn, lawful."); break;
				case ART_DELUXE_YENDORIAN_KNIFE:
					pline("Artifact specs: ESP and fire resistance when wielded, +6 to-hit and +20 damage, neutral."); break;
				case ART_HARP_OF_LIGHTNING:
					pline("Artifact specs: warning, teleport control and acid resistance when wielded, neutral."); break;
				case ART_HARP_OF_HARMONY:
					pline("Artifact specs: warning, stealth and drain resistance when wielded, lawful."); break;
				case ART_CUDGEL_OF_CUTHBERT:
					pline("Artifact specs: hallucination and drain resistance plus regeneration, warning, increased monster difficulty, wall trap effect and half spell damage when wielded, +5 to-hit and double damage to crossaligned monsters, lawful."); break;
				case ART_SWORD_OF_SVYATOGOR:
					pline("Artifact specs: half physical damage and cold resistance when wielded, +7 to-hit and +8 damage, lawful. No idea what weird mythology 'Svyatogor' comes from, but certainly not The Elder Scrolls, so this has no business being the warrior quest artifact in SLASHTHEM."); break;
				case ART_TOMMY_GUN_OF_CAPONE:
					pline("Artifact specs: fire resistance, warning, stealth and acts as a luckstone when wielded, +5 to-hit and +6 damage, chaotic."); break;
				case ART_WHISTLE_OF_THE_WARDEN:
					pline("Artifact specs: ESP, teleport control and drain resistance when wielded, lawful."); break;
				case ART_HAND_MIRROR_OF_CTHYLLA:
					pline("Artifact specs: teleport control and searching when wielded."); break;
				case ART_SCALPEL_OF_THE_BLOODLETTER:
					pline("Artifact specs: regeneration, half physical damage and bleedout when wielded, +9 to-hit and +10 level-drain damage, neutral."); break;
				case ART_GOURD_OF_INFINITY:
					pline("Artifact specs: ESP, half spell damage, hallucination and drain resistance when wielded, neutral."); break;
				case ART_LOCKPICK_OF_ARSENE_LUPIN:
					pline("Artifact specs: searching, ESP, stealth, warning and acts as a luckstone when wielded, neutral."); break;
				case ART_STAFF_OF_WITHERING:
					pline("Artifact specs: cold resistance when wielded, +3 to-hit and +4 level-drain damage to crossaligned monsters, chaotic."); break;
				case ART_BOW_OF_SKADI:
					pline("Artifact specs: cold resistance when wielded, +1 to-hit and +24 damage to cold-susceptible monsters, lawful. In dnethack you can somehow read this weapon (wtf) and learn cone of cold (double wtf)."); break;
				case ART_CROWN_OF_THE_SAINT_KING:
					pline("Artifact specs: 5 extra points of AC, lawful. In dnethack it would make pets always follow you but that would be a pain in the butt to code."); break;
				case ART_HELM_OF_THE_DARK_LORD:
					pline("Artifact specs: 5 extra points of AC, chaotic. In dnethack it would make pets always follow you but that would be a pain in the butt to code."); break;
				case ART_SUNBEAM:
					pline("Artifact specs: drain resistance when wielded, +10 to-hit and double damage."); break;
				case ART_MOONBEAM:
					pline("Artifact specs: drain resistance when wielded, +10 to-hit and double damage."); break;
				case ART_VEIL_OF_LATONA:
					pline("Artifact specs: drain and magic resistance, reflection, superscroller, black ng walls and confusion when worn, neutral. The bad effects were added because the dnethack version of this item was uber imba."); break;
				case ART_CARNWENNAN:
					pline("Artifact specs: warning and stealth when wielded, +5 to-hit and +10 damage to magic-liking monsters, lawful."); break;
				case ART_SLAVE_TO_ARMOK:
					pline("Artifact specs: +5 to-hit and double damage to elves, orcs, lords and peaceful creatures, bloodthirsty, lawful. According to Chris, DF Dwarves can be a nasty lot."); break;
				case ART_DRAGONLANCE:
					pline("Artifact specs: reflection and warning when wielded, +10 to-hit and +20 damage to dragons."); break;
				case ART_KINGSLAYER:
					pline("Artifact specs: warning when wielded, +10 to-hit and +20 damage to lords and princes, chaotic."); break;
				case ART_PEACE_KEEPER:
					pline("Artifact specs: warning when wielded, +5 to-hit and +10 damage to always-hostile monsters, lawful."); break;
				case ART_RHONGOMYNIAD:
					pline("Artifact specs: +3 to-hit and double damage, lawful."); break;
				case ART_GILDED_SWORD_OF_Y_HA_TALLA:
					pline("Artifact specs: +5 to-hit and double stun damage, poison resistance when wielded."); break;
				case ART_AXE_OF_THE_DWARVISH_LORDS:
					pline("Artifact specs: teleport control and sight bonus when wielded, +1 to-hit and double damage, lawful."); break;
				case ART_WINDRIDER:
					pline("Artifact specs: +1 to-hit and double damage."); break;
				case ART_ROD_OF_THE_RAM:
					pline("Artifact specs: +1 to-hit and double damage, neutral."); break;
				case ART_ATMA_WEAPON:
					pline("Artifact specs: +6 to-hit and +6 damage to nasty monsters."); break;
				case ART_LIMITED_MOON:
					pline("Artifact specs: +1 to-hit and double damage, chaotic."); break;
				case ART_BLACK_ARROW:
					pline("Artifact specs: +1 to-hit and double damage."); break;
				case ART_TENSA_ZANGETSU:
					pline("Artifact specs: speed and half spell damage when wielded, massively increases hunger and damages you every turn, +1 to-hit and double damage, neutral."); break;
				case ART_SODE_NO_SHIRAYUKI:
					pline("Artifact specs: cold resistance when wielded, +1 to-hit and double damage to cold-susceptible monsters, lawful."); break;
				case ART_TOBIUME:
					pline("Artifact specs: fire resistance when wielded, +1 to-hit and +2 damage to fire-susceptible monsters, chaotic. According to Chris, this artifact is an 'awkward' weapon."); break;
				case ART_LANCE_OF_LONGINUS:
					pline("Artifact specs: half spell damage, half physical damage, reflection, magic and drain resistance, stun, confusion, hallucination and freezing when wielded, lawful. Seriously, dnethack artifacts are completely out of whack."); break;
				case ART_HARKENSTONE:
					pline("Artifact specs: aggravate monster when wielded, +5 to-hit and double damage, chaotic."); break;
				case ART_RELEASE_FROM_CARE:
					pline("Artifact specs: drain resistance when wielded, +1 to-hit and +10 damage, beheads targets."); break;
				case ART_SILENCE_GLAIVE:
					pline("Artifact specs: drain resistance when wielded, +1 to-hit and +2 level-drain damage."); break;
				case ART_GARNET_ROD:
					pline("Artifact specs: regeneration, energy regeneration, speed and massively increased hunger when wielded."); break;
				case ART_HELPING_HAND:
					pline("Artifact specs: searching, warning and stealth when wielded, lawful."); break;
				case ART_BLADE_SINGER_S_SPEAR:
					pline("Artifact specs: +6 to-hit and +6 damage."); break;
				case ART_BLADE_DANCER_S_DAGGER:
					pline("Artifact specs: +4 to-hit and +4 damage."); break;
				case ART_LIMB_OF_THE_BLACK_TREE:
					pline("Artifact specs: fire resistance when wielded, +4 to-hit and +2 damage to fire-susceptible monsters, chaotic."); break;
				case ART_LASH_OF_THE_COLD_WASTE:
					pline("Artifact specs: cold resistance when wielded, +4 to-hit and +2 damage to cold-susceptible monsters, chaotic."); break;
				case ART_RAMIEL:
					pline("Artifact specs: shock resistance when wielded, +4 to-hit and +2 damage to shock-susceptible monsters, lawful. In dnethack this thing would have a special ranged attack but here you'll probably want to use the 'apply b' tactic."); break;
				case ART_SPINESEARCHER:
					pline("Artifact specs: stealth when wielded, +1 to-hit and +6 damage, chaotic."); break;
				case ART_QUICKSILVER:
					pline("Artifact specs: +4 to-hit and +8 damage."); break;
				case ART_SKY_RENDER:
					pline("Artifact specs: displacement when wielded, +10 to-hit and +10 damage, lawful."); break;
				case ART_FLUORITE_OCTAHEDRON:
					pline("Artifact specs: +5 to-hit and double damage."); break;
				case ART_TIE_DYE_SHIRT_OF_SHAMBHALA:
					pline("Artifact specs: sets itself to +10 when worn, creates traps when worn, neutral."); break;
				case ART_GRANDMASTER_S_ROBE:
					pline("Artifact specs: 5 extra points of AC and improves marital arts damage by 10, neutral."); break;
				case ART_DRAGON_PLATE:
					pline("Artifact specs: magic resistance when worn, but reduces carry capacity and spellcasting success chances, lawful."); break;
				case ART_BEASTMASTER_S_DUSTER:
					pline("Artifact specs: animals are usually spawned peaceful and sometimes tame, lawful."); break;
				case ART_SHIELD_OF_THE_ALL_SEEING:
					pline("Artifact specs: warning, searching and fire resistance when worn."); break;
				case ART_SHIELD_OF_YGGDRASIL:
					pline("Artifact specs: regeneration and poison resistance when worn."); break;
				case ART_WATER_FLOWERS:
					pline("Artifact specs: displacement when worn, chaotic. Chris_ANG gave them an interesting activation ability but that wasn't ported over."); break;
				case ART_HAMMERFEET:
					pline("Artifact specs: +1 to-hit and double damage, chaotic."); break;
				case ART_SHIELD_OF_THE_RESOLUTE_HEA:
					pline("Artifact specs: half physical damage when worn. No idea why they're called 'shield', because they're no shield at all."); break;
				case ART_GAUNTLETS_OF_SPELL_POWER:
					pline("Artifact specs: half spell damage and increased spellcasting chances when worn."); break;
				case ART_PREMIUM_HEART:
					pline("Artifact specs: +1 to-hit and double damage. Everyone wishes for this thing in dnethack, I wonder if they're similarly overpowered in slex?"); break;
				case ART_STORMHELM:
					pline("Artifact specs: cold and shock resistance when worn, chaotic."); break;
				case ART_HELLRIDER_S_SADDLE:
					pline("Artifact specs: supposed to give reflection, see for yourself whether it works!"); break;
				case ART_ROD_OF_SEVEN_PARTS:
					pline("Artifact specs: drain resistance when wielded, +7 to-hit and +20 damage to crossaligned monsters, lawful."); break;
				case ART_FIELD_MARSHAL_S_BATON:
					pline("Artifact specs: warning when wielded, lawful."); break;
				case ART_WEREBUSTER:
					pline("Artifact specs: +10 to-hit and +20 damage to werecreatures."); break;
				case ART_MASAMUNE:
					pline("Artifact specs: trap revealing effect when wielded."); break;
				case ART_BLACK_CRYSTAL:
					pline("Artifact specs: warning and magic resistance when wielded, +3 to-hit and double damage, chaotic."); break;
				case ART_WATER_CRYSTAL:
					pline("Artifact specs: cold resistance when wielded."); break;
				case ART_FIRE_CRYSTAL:
					pline("Artifact specs: fire resistance when wielded."); break;
				case ART_EARTH_CRYSTAL:
					pline("Artifact specs: half physical damage when wielded."); break;
				case ART_AIR_CRYSTAL:
					pline("Artifact specs: shock resistance when wielded."); break;
				case ART_ANNULUS:
					pline("Artifact specs: half spell damage and magic resistance when wielded, +5 to-hit and +2 damage, chaotic."); break;
				case ART_IRON_BALL_OF_LEVITATION:
					pline("Artifact specs: levitation, drain resistance, stealth, warning and acts as a luckstone when wielded, +5 to-hit and +10 damage to crossaligned monsters, chaotic. In dnethack you can carry magic chests and therefore the entire dungeon by exploiting this thing."); break;
				case ART_IRON_SPOON_OF_LIBERATION:
					pline("Artifact specs: magic resistance, stealth, searching and acts as a luckstone when wielded, +5 to-hit and double damage, chaotic."); break;
				case ART_SILVER_STARLIGHT:
					pline("Artifact specs: +4 to-hit and +4 damage. In dnethack this weapon would somehow improve your shuriken, which makes no sense."); break;
				case ART_WRATHFUL_SPIDER:
					pline("Artifact specs: stealth when wielded, chaotic."); break;
				case ART_TENTACLE_ROD:
					pline("Artifact specs: +7 to-hit and +2 damage."); break;
				case ART_CRESCENT_BLADE:
					pline("Artifact specs: reflection when wielded, beheads monsters, +4 to-hit and double damage to fire-susceptible monsters, lawful."); break;
				case ART_DARKWEAVER_S_CLOAK:
					pline("Artifact specs: magic resistance when worn."); break;
				case ART_SPIDERSILK:
					pline("Artifact specs: improved spellcasting chances when worn, chaotic."); break;
				case ART_WEBWEAVER_S_CROOK:
					pline("Artifact specs: magic resistance when wielded, +1 to-hit and double damage, lawful."); break;
				case ART_LOLTH_S_FANG:
					pline("Artifact specs: drain resistance when wielded, +10 to-hit and +10 damage to acid-susceptible monsters."); break;
				case ART_WEB_OF_LOLTH:
					pline("Artifact specs: warning, magic and drain resistance as well as improved spellcasting chances when worn, but slows you down to half speed, chaotic."); break;
				case ART_CLAWS_OF_THE_REVENANCER:
					pline("Artifact specs: drain resistance, energy regeneration and fainting when worn, +1 to-hit and +2 level-drain damage, neutral."); break;
				case ART_LIECLEAVER:
					pline("Artifact specs: searching, drain and hallucination resistance when wielded, +5 to-hit and +10 damage, lawful."); break;
				case ART_RUINOUS_DESCENT_OF_STARS:
					pline("Artifact specs: magic resistance when wielded, +1 to-hit and double damage, chaotic."); break;
				case ART_SICKLE_MOON:
					pline("Artifact specs: +1 to-hit and double damage."); break;
				case ART_ARCOR_KERYM:
					pline("Artifact specs: drain resistance when wielded, +5 to-hit and double damage, lawful."); break;
				case ART_ARYFAERN_KERYM:
					pline("Artifact specs: shock resistance and improved spellcasting chances when wielded, +5 to-hit and +10 damage to shock-susceptible monsters, neutral."); break;
				case ART_ARYVELAHR_KERYM:
					pline("Artifact specs: reflection and drain resistance when wielded, +5 to-hit and double damage, chaotic."); break;
				case ART_ARMOR_OF_KHAZAD_DUM:
					pline("Artifact specs: magic resistance when worn, lawful."); break;
				case ART_WAR_MASK_OF_DURIN:
					pline("Artifact specs: half spell damage, +5 damage with axes and fire, acid and poison resistance when worn, lawful."); break;
				case ART_DURIN_S_AXE:
					pline("Artifact specs: searching and drain resistance when wielded, +10 to-hit and +10 damage, lawful."); break;
				case ART_GLAMDRING:
					pline("Artifact specs: warning when wielded, +10 to-hit and +10 damage to orcs and demons, lawful. In ToME you will want to use this weapon to kill Uvatha. :-)"); break;
				case ART_ARMOR_OF_EREBOR:
					pline("Artifact specs: magic, cold and fire resistance and half physical damage when worn, disables flying and causes chaos terrain, disables reflection occasionally, lawful."); break;
				case ART_SCEPTRE_OF_LOLTH:
					pline("Artifact specs: +1 to-hit and double damage, chaotic."); break;
				case ART_WEB_OF_THE_CHOSEN:
					pline("Artifact specs: reflection, half spell damage, acid and shock resistance and slows you down a bit when worn, chaotic."); break;
				case ART_CLOAK_OF_THE_CONSORT:
					pline("Artifact specs: half physical damage, cold and drain resistance when worn, but causes display loss most of the time, neutral."); break;
				case ART_ROGUE_GEAR_SPIRITS:
					pline("Artifact specs: searching, warning, ESP and fire resistance when wielded, +5 to-hit and double damage, neutral."); break;
				case ART_MOONBOW_OF_SEHANINE:
					pline("Artifact specs: +5 to-hit and double damage, chaotic."); break;
				case ART_SPELLSWORD_OF_CORELLON:
					pline("Artifact specs: +1 to-hit and +10 damage, chaotic."); break;
				case ART_WARHAMMER_OF_VANDRIA:
					pline("Artifact specs: +5 to-hit and double damage, chaotic."); break;
				case ART_SHIELD_OF_SAINT_CUTHBERT:
					pline("Artifact specs: half physical damage and half spell damage when worn, lawful."); break;
				case ART_BELTHRONDING:
					pline("Artifact specs: stealth and displacement when wielded, +5 to-hit and double damage, chaotic."); break;
				case ART_ROD_OF_THE_ELVISH_LORDS:
					pline("Artifact specs: +3 to-hit and double damage, chaotic."); break;
				case ART_SOL_VALTIVA:
					pline("Artifact specs: fire resistance when wielded, +5 to-hit and +24 damage to fire-susceptible monsters, chaotic."); break;
				case ART_STAFF_OF_THE_ARCHMAGI:
					pline("Artifact specs: searching, cold, fire and shock resistance and acts as a luckstone when wielded, +20 to-hit and +4 stun damage."); break;
				case ART_ROBE_OF_THE_ARCHMAGI:
					pline("Artifact specs: reflection, magic resistance, displacement, blood mana and stun when worn."); break;
				case ART_HAT_OF_THE_ARCHMAGI:
					pline("Artifact specs: sight bonus and warning when worn."); break;
				case ART_KUSANAGI_NO_TSURUGI:
					pline("Artifact specs: energy regeneration, searching, acts as a luckstone, aggravate monster, recurring disenchantment and itemcursing when wielded, beheads monsters, +20 to-hit and +12 damage, lawful."); break;
				case ART_GENOCIDE:
					pline("Artifact specs: fire resistance when wielded, +10 to-hit and +20 damage to fire-susceptible monsters, bloodthirsty, lawful."); break;
				case ART_ROD_OF_DIS:
					pline("Artifact specs: +10 to-hit and +8 damage, lawful."); break;
				case ART_AVARICE:
					pline("Artifact specs: +10 to-hit and +2 damage, lawful. In dnethack this artifact would steal items from monsters but why would I go through the PITA of coding that?!"); break;
				case ART_FIRE_OF_HEAVEN:
					pline("Artifact specs: shock and fire resistance when wielded, +1 to-hit and double damage to fire-susceptible monsters, lawful."); break;
				case ART_DIADEM_OF_AMNESIA:
					pline("Artifact specs: causes recurring amnesia when worn, lawful. Put it on right now to ease your troubled mind."); break;
				case ART_THUNDER_S_VOICE:
					pline("Artifact specs: shock resistance when wielded, +6 to-hit and +6 damage to shock-susceptible monsters, lawful."); break;
				case ART_SERPENT_S_TOOTH:
					pline("Artifact specs: poison resistance when wielded, lawful."); break;
				case ART_UNBLEMISHED_SOUL:
					pline("Artifact specs: acts as a luckstone when wielded, lawful."); break;
				case ART_WRATH_OF_HEAVEN:
					pline("Artifact specs: resist fire and shock when wielded, +1 to-hit and double damage to shock-susceptible monsters, lawful."); break;
				case ART_ALL_SEEING_EYE_OF_THE_FLY:
					pline("Artifact specs: undead warning when worn, lawful."); break;
				case ART_COLD_SOUL:
					pline("Artifact specs: cold, shock and fire resistance when wielded, lawful."); break;
				case ART_SCEPTRE_OF_THE_FROZEN_FLOO:
					pline("Artifact specs: cold resistance when wielded, +1 to-hit and double damage to cold-susceptible monsters, lawful."); break;
				case ART_CARESS:
					pline("Artifact specs: shock resistance when wielded, +1 to-hit and +20 shock damage to elves, humans and dwarves, lawful. Being whipped feels very soothing! <3"); break;
				case ART_ICONOCLAST:
					pline("Artifact specs: magic resistance when wielded, +9 to-hit and +18 damage to humans, elves and dwarves, lawful. In dnethack the damage bonus would be +99, which is of course not overpowered at all!"); break;
				case ART_THREE_HEADED_FLAIL:
					pline("Artifact specs: speed when wielded, chaotic."); break;
				case ART_HEARTCLEAVER:
					pline("Artifact specs: +1 to-hit and double damage, chaotic."); break;
				case ART_WRATHFUL_WIND:
					pline("Artifact specs: cold resistance when wielded, +10 to-hit and double damage to cold-susceptible monsters, chaotic."); break;
				case ART_STING_OF_THE_POISON_QUEEN:
					pline("Artifact specs: magic resistance when wielded, +4 to-hit and +12 damage, chaotic."); break;
				case ART_SCOURGE_OF_LOLTH:
					pline("Artifact specs: +1 to-hit and double damage, chaotic."); break;
				case ART_DOOMSCREAMER:
					pline("Artifact specs: acid resistance when wielded, +1 to-hit and double damage to acid-susceptible monsters, chaotic."); break;
				case ART_WAND_OF_ORCUS:
					pline("Artifact specs: +5 to-hit and +12 drain life damage, bloodthirsty, chaotic. But you're probably going to zap monsters with it so all of those stats are irrelevant anyway."); break;
				case ART_SWORD_OF_ERATHAOL:
					pline("Artifact specs: blindness resistance and searching when wielded, +7 to-hit and +10 damage, lawful. This weapon is from a series of identical artifacts that Chris made for dnethack; only their base items differ."); break;
				case ART_SABER_OF_SABAOTH:
					pline("Artifact specs: blindness resistance and searching when wielded, +7 to-hit and +10 damage, lawful. This weapon is from a series of identical artifacts that Chris made for dnethack; only their base items differ."); break;
				case ART_SWORD_OF_ONOEL:
					pline("Artifact specs: blindness resistance and searching when wielded, +7 to-hit and +10 damage, lawful. This weapon is from a series of identical artifacts that Chris made for dnethack; only their base items differ."); break;
				case ART_GLAIVE_OF_SHAMSIEL:
					pline("Artifact specs: blindness resistance and searching when wielded, +7 to-hit and +10 damage, lawful. This weapon is from a series of identical artifacts that Chris made for dnethack; only their base items differ."); break;
				case ART_LANCE_OF_URIEL:
					pline("Artifact specs: blindness resistance and searching when wielded, +7 to-hit and +10 damage, lawful. This weapon is from a series of identical artifacts that Chris made for dnethack; only their base items differ."); break;
				case ART_HAMMER_OF_BARQUIEL:
					pline("Artifact specs: blindness resistance and searching when wielded, +7 to-hit and +10 damage, lawful. This weapon is from a series of identical artifacts that Chris made for dnethack; only their base items differ."); break;
				case ART_STORMBRINGER:
					pline("Artifact specs: +5 to-hit and +2 drain life damage, drain resistance when wielded, bloodthirsty, chaotic."); break;
				case ART_REAVER:
					pline("Artifact specs: +5 to-hit and +8 damage, chaotic, pirate sacrifice gift."); break;
				case ART_THIEFBANE:
					pline("Artifact specs: +5 to-hit and +2 level-drain damage to Team @, with a chance of beheading, chaotic."); break;
				case ART_DEATHSWORD:
					pline("Artifact specs: +5 to-hit and +14 damage to Team @, chaotic, barbarian sacrifice gift."); break;
				case ART_BAT_FROM_HELL:
					pline("Artifact specs: +3 to-hit and +20 damage, chaotic, rogue sacrifice gift."); break;
				case ART_ELFRIST:
					pline("Artifact specs: +5 to-hit and +16 damage to elves, chaotic, aligned with orc race."); break;
				case ART_PLAGUE:
					pline("Artifact specs: +5 to-hit and +8 damage, poison resistance when wielded, automatically poisons arrows, chaotic."); break;
				case ART_MUMAKBANE:
					pline("Artifact specs: +5 to-hit and +60 fire damage to quadrupeds (because actual mumaks are too rare to make a specific MUMAK-slaying effect useful), neutral."); break;
				case ART_WORMBITER:
					pline("Artifact specs: +5 to-hit and double damage to worms, neutral."); break;
				case ART_SHOCKER:
					pline("Artifact specs: +3 to-hit and double damage to shock-susceptible monsters, neutral."); break;
				case ART_SCALES_OF_THE_DRAGON_LORD:
					pline("Artifact specs: protection when worn, can be invoked for dragon breath, chaotic. Special generation."); break;
				case ART_BURNED_MOTH_RELAY:
					pline("Artifact specs: protection while in inventory, neutral."); break;
				case ART_KEY_OF_ACCESS:
					pline("Artifact specs: Can be invoked for portal creation. Never randomly generated."); break;
				case ART_HELLFIRE:
					pline("Artifact specs: +5 to-hit and +8 damage, fire resistance when wielded, shoots flaming ammo, chaotic."); break;
				case ART_HOUCHOU:
					pline("Artifact specs: Throwing it at a target will instakill it."); break;
				case ART_WALLET_OF_PERSEUS:
					pline("Artifact specs: Greatly reduces the weight of its contents."); break;
				case ART_NIGHTHORN:
					pline("Artifact specs: reflection when wielded, lawful, special quest reward that cannot be wished for."); break;
				case ART_KEY_OF_LAW:
					pline("Artifact specs: This key opens specific doors on Vlad's Tower and can only be obtained by beating the lawful quest, which you did! TROPHY GET!"); break;
				case ART_EYE_OF_THE_BEHOLDER:
					pline("Artifact specs: can be invoked for death gaze, neutral, special quest reward that cannot be wished for."); break;
				case ART_KEY_OF_NEUTRALITY:
					pline("Artifact specs: This key opens specific doors on Vlad's Tower and can only be obtained by beating the neutral quest, which you did! TROPHY GET!"); break;
				case ART_HAND_OF_VECNA:
					pline("Artifact specs: regeneration, half physical damage and drain resistance when wielded, cold resistance while carried, can be invoked for summon undead, chaotic, special quest reward that cannot be wished for."); break;
				case ART_KEY_OF_CHAOS:
					pline("Artifact specs: This key opens specific doors on Vlad's Tower and can only be obtained by beating the chaotic quest, which you did! TROPHY GET!"); break;
				case ART_GAUNTLET_KEY:
					pline("Artifact specs: Opens a specific door on a certain variant of the lawful quest, and is obviously lawful itself."); break;
				case ART_ORB_OF_DETECTION:
					pline("Artifact specs: ESP, half spell damage and magic resistance while carried, can be invoked for invisibility, lawful, archeologist quest artifact."); break;
				case ART_BALL_OF_LIGHT:
					pline("Artifact specs: ESP, half spell damage and magic resistance while carried, can be invoked to light areas, blindness resistance when wielded, lawful, erdrick quest artifact."); break;
				case ART_HEART_OF_AHRIMAN:
					pline("Artifact specs: stealth while carried, +5 to-hit and double damage, can be invoked for levitation, neutral, barbarian quest artifact."); break;
				case ART_ARKENSTONE:
					pline("Artifact specs: ESP while carried, can be invoked for healing, lawful, midget quest artifact."); break;
				case ART_SCEPTRE_OF_MIGHT:
					pline("Artifact specs: +3 to-hit and +6 damage to crossaligned monsters, magic resistance while carried, can be invoked for conflict, lawful, caveman quest artifact."); break;
				case ART_MYSTERIOUS_SPIKES:
					pline("Artifact specs: +30 to-hit and +60 damage to crossaligned monsters, x-ray vision when wielded, magic resistance while carried, can be invoked for healing, lawful, mystic quest artifact."); break;
				case ART_IRON_BALL_OF_LIBERATION:
					pline("Artifact specs: stealth, searching, warning and magic resistance while carried, can be invoked for phasing, neutral."); break;
				case ART_PITCH_BLADE:
					pline("Artifact specs: teleport control while wielded, +5 to-hit and +6 damage, chaotic, murderer quest artifact."); break;
				case ART_PALANTIR_OF_WESTERNESSE:
					pline("Artifact specs: ESP, regeneration and half spell damage while carried, can be invoked for taming, chaotic, elph quest artifact."); break;
				case ART_ROCKER_SLING:
					pline("Artifact specs: +5 to-hit and double damage to giants, neutral, rocker quest artifact."); break;
				case ART_LIONTAMER:
					pline("Artifact specs: +5 to-hit and +16 damage to cats, lawful, zookeeper quest artifact."); break;
				case ART_DRAGONCLAN_SWORD:
					pline("Artifact specs: +3 to-hit and +20 damage, can bisect enemies, lawful, ninja quest artifact."); break;
				case ART_KILLING_EDGE:
					pline("Artifact specs: +3 to-hit and +6 damage, can bisect enemies, bloodthirsty, chaotic assassin quest artifact."); break;
				case ART_BLACK_DEATH:
					pline("Artifact specs: +5 to-hit and +10 level-drain damage, chaotic undertaker quest artifact."); break;
				case ART_SLOW_BLADE:
					pline("Artifact specs: searching and regeneration while carried, +2 to-hit and +2 damage, can be invoked for healing, lawful, acid mage quest artifact."); break;
				case ART_FIRE_BRIGADE_REEL:
					pline("Artifact specs: half spell damage and half physical damage while carried, +4 to-hit and +8 damage to cold-susceptible monsters, can be invoked to summon a water elemental, lawful, firefighter quest artifact."); break;
				case ART_CANDLE_OF_ETERNAL_FLAME:
					pline("Artifact specs: warning, cold resistance and teleport control while carried, can be invoked to summon a fire elemental, neutral, flame mage quest artifact."); break;
				case ART_NETHACK_SOURCES:
					pline("Artifact specs: searching, ESP and regeneration while carried, can be invoked to identify, neutral, geek quest artifact."); break;
				case ART_MASTER_BOOT_DISK:
					pline("Artifact specs: reflection while carried, can be invoked for phasing, neutral, graduate quest artifact."); break;
				case ART_LYRE_OF_ORPHEUS:
					pline("Artifact specs: magic resistance while carried, can be invoked for taming, neutral, bard quest artifact."); break;
				case ART_OPERATIONAL_SCALPEL:
					pline("Artifact specs: regeneration when wielded, +3 to-hit and double level-drain damage, can be invoked for healing, neutral, scientist quest artifact."); break;
				case ART_STAFF_OF_AESCULAPIUS:
					pline("Artifact specs: regeneration when wielded, +3 to-hit and double level-drain damage, can be invoked for healing, neutral, healer quest artifact."); break;
				case ART_TENTACLE_STAFF:
					pline("Artifact specs: warning and stealth when wielded, +8 to-hit and double damage to shock-susceptible monsters, can be invoked to charge objects, chaotic, twelph quest artifact."); break;
				case ART_STORM_WHISTLE:
					pline("Artifact specs: warning, fire resistance and teleport control while carried, can be invoked to summon a water elemental, lawful, ice mage quest artifact."); break;
				case ART_THUNDER_WHISTLE:
					pline("Artifact specs: warning, shock resistance and teleport control while carried, chaotic, electric mage quest artifact."); break;
				case ART_IMMUNITY_RING:
					pline("Artifact specs: magic resistance when worn, ESP and drain resistance while carried, can be invoked to charge objects, neutral, poison mage quest artifact."); break;
				case ART_BLACKHARP:
					pline("Artifact specs: warning, teleport control and drain resistance while carried, chaotic, musician quest artifact."); break;
				case ART_MAGIC_MIRROR_OF_MERLIN:
					pline("Artifact specs: ESP and cold resistance while carried, double damage for your spells, lawful, knight quest artifact."); break;
				case ART_MAGIC_MIRROR_OF_ARTHUBERT:
					pline("Artifact specs: half spell damage and stun protection while carried, lawful, chevalier quest artifact."); break;
				case ART_MAGIC_MIRROR_OF_JASON:
					pline("Artifact specs: half physical damage and magic resistance while carried, lawful, warrior quest artifact."); break;
				case ART_CHEKHOV_S_GUN:
					pline("Artifact specs: +5 to-hit and +8 damage, poison resistance when wielded, chaotic, gangster quest artifact."); break;
				case ART_SHINY_MAGNUM:
					pline("Artifact specs: +10 to-hit and +16 damage, lawful, officer quest artifact."); break;
				case ART_WITHERED_NINE_MILLIMETER:
					pline("Artifact specs: +5 to-hit and +8 damage, neutral, courier quest artifact."); break;
				case ART_TRAINING_SMG:
					pline("Artifact specs: +5 to-hit and +8 damage, can be invoked for enlightening, neutral, scribe quest artifact."); break;
				case ART_CHARGED_USB_STICK:
					pline("Artifact specs: can be invoked to charge objects, neutral, wandkeeper quest artifact."); break;
				case ART_VERBAL_BLADE:
					pline("Artifact specs: +5 to-hit and +2 damage, can behead enemies, neutral, zyborg quest artifact."); break;
				case ART_TOME_DARK_SWORD:
					pline("Artifact specs: half spell damage, half physical damage, ESP and stealth when wielded, can be invoked for enlightening, neutral, unbeliever quest artifact."); break;
				case ART_ELDER_STAFF:
					pline("Artifact specs: regeneration when wielded, +6 to-hit and double level drain damage, chaotic, death eater quest artifact."); break;
				case ART_GAUNTLETS_OF_ILLUSION:
					pline("Artifact specs: hallucination resistance while carried, can be invoked for invisibility, neutral, pokemon quest artifact."); break;
				case ART_LOVELY_PINK_PUMPS:
					pline("Artifact specs: regeneration while carried, drain resistance when worn, can be invoked for invisibility, neutral, transvestite quest artifact."); break;
				case ART_KISS_BOOTS:
					pline("Artifact specs: energy regeneration while carried, drain resistance, half spell damage and half physical damage when worn, can be invoked for identify, neutral, transsylvanian quest artifact."); break;
				case ART_GOLDEN_HIGH_HEELS:
					pline("Artifact specs: stealth while carried, acid resistance when worn, can be invoked for levitation, neutral, topmodel quest artifact."); break;
				case ART_UNOBTAINABLE_BEAUTIES:
					pline("Artifact specs: warning, reflection and acid resistance when worn, regeneration while carried, can be invoked for healing, neutral, failed existence quest artifact."); break;
				case ART_ACTIVIST_STICK:
					pline("Artifact specs: +5 to-hit and double damage, lawful, activistor quest artifact."); break;
				case ART_EYES_OF_THE_OVERWORLD:
					pline("Artifact specs: X-ray vision when worn, magic resistance when carried, can be invoked for enlightening, neutral, monk quest artifact."); break;
				case ART_GAUNTLETS_OF_OFFENSE:
					pline("Artifact specs: half physical damage when carried, can be invoked for invisibility, neutral, psion quest artifact."); break;
				case ART_PEN_OF_THE_VOID:
					pline("Artifact specs: +5 to-hit and double damage, neutral, binder quest artifact."); break;
				case ART_BLOODY_BEAUTY:
					pline("Artifact specs: +4 to-hit and +4 level-drain damage, neutral, bleeder quest artifact."); break;
				case ART_GOFFIC_BACKPACK:
					pline("Artifact specs: half spell damage, half physical damage and regeneration while carried, can be invoked for energy boost, neutral, goff quest artifact."); break;
				case ART_MANTLE_OF_HEAVEN:
					pline("Artifact specs: half spell damage and shock resistance when worn, cold resistance while carried, lawful, noble quest artifact."); break;
				case ART_VESTMENT_OF_HELL:
					pline("Artifact specs: half physical damage and acid resistance when worn, fire resistance while carried, chaotic, noble quest artifact."); break;
				case ART_CLOAK_OF_NEUTRALITY:
					pline("Artifact specs: energy regeneration and drain resistance when worn, acid resistance while carried, neutral, drunk quest artifact."); break;
				case ART_GREAT_DAGGER_OF_GLAURGNAA:
					pline("Artifact specs: +8 to-hit and +4 drain life damage to crossaligned monsters, magic resistance while carried, can be invoked for energy boost, chaotic, necromancer quest artifact."); break;
				case ART_MITRE_OF_HOLINESS:
					pline("Artifact specs: improves your abilities to kill undead, fire resistance while carried, can be invoked for energy boost, lawful, priest quest artifact."); break;
				case ART_PAINKILLER:
					pline("Artifact specs: +12 to-hit and +24 damage to humans, magic resistance while carried, chaotic, abuser quest artifact."); break;
				case ART_DRAGON_WHIP:
					pline("Artifact specs: +5 to-hit and +10 damage to domestic creatures, fire resistance while wielded, can be invoked for dragon breath, chaotic, slave master quest artifact."); break;
				case ART_RUPTURER:
					pline("Artifact specs: warning and cold resistance when wielded, +3 to-hit and +14 damage, can be invoked for dragon breath, chaotic, bloodseeker quest artifact."); break;
				case ART_BLOOD_MARKER:
					pline("Artifact specs: half spell damage and magic resistance when wielded, can be invoked for identify, neutral, librarian quest artifact."); break;
				case ART_CUTTHROAT_BLADE:
					pline("Artifact specs: can behead enemies, magic resistance when wielded, can be invoked for death gaze, neutral, pickpocket quest artifact."); break;
				case ART_SHARPENED_TOOTHPICK:
					pline("Artifact specs: searching bonus while wielded, +8 to-hit and +16 damage, neutral, bully quest artifact."); break;
				case ART_KITCHEN_CUTTER:
					pline("Artifact specs: +6 to-hit and +12 damage to strong monsters, neutral, cook quest artifact."); break;
				case ART_ARCHON_STAFF:
					pline("Artifact specs: energy regeneration while carried, +10 to-hit and +20 damage to demons, can be invoked for object detection, lawful, augurer quest artifact."); break;
				case ART_SHILLELAGH:
					pline("Artifact specs: energy regeneration while carried, +16 to-hit and +8 damage to nasty monsters, can be invoked to light areas, lawful, sage quest artifact."); break;
				case ART_ALTAR_CARVER:
					pline("Artifact specs: +8 to-hit and +16 damage to stalking monsters, can be invoked to summon undead, lawful, otaku quest artifact."); break;
				case ART_MIRAGE_TAIL:
					pline("Artifact specs: +4 to-hit and +8 damage, fire resistance when wielded, can be invoked for dragon breath, neutral, artist quest artifact."); break;
				case ART_GAME_DISC:
					pline("Artifact specs: half physical damage and fire resistance when wielded, neutral, gamer quest artifact."); break;
				case ART_MODIFIED_Z_SWORD:
					pline("Artifact specs: +8 to-hit and +8 damage, can be invoked for energy boost, lawful, saiyan quest artifact."); break;
				case ART_PICK_OF_FLANDAL_STEELSKIN:
					pline("Artifact specs: half physical damage when wielded, neutral, goldminer quest artifact."); break;
				case ART_PRIME_MINISTER_S_TUXEDO:
					pline("Artifact specs: half physical damage and magic resistance when worn, lawful, politician quest artifact."); break;
				case ART_SLOWNESS_SHIRT:
					pline("Artifact specs: half spell damage and drain resistance when worn, lawful, ladiesman quest artifact."); break;
				case ART_COAT_OF_STYLE:
					pline("Artifact specs: half physical damage and acid resistance when worn, can be invoked for enlightening, chaotic, stunt master quest artifact."); break;
				case ART_CARBON_NANOTUBE_SUIT:
					pline("Artifact specs: half physical damage and magic resistance when worn, can be invoked for untrapping, chaotic, gunner quest artifact."); break;
				case ART_BRUTAL_CHAINSAW:
					pline("Artifact specs: +10 to-hit and +20 damage, can be invoked to create portals, neutral, doom marine quest artifact."); break;
				case ART_TREASURY_OF_PROTEUS:
					pline("Artifact specs: magic resistance and acts as a luckstone while carried, chaotic, pirate quest artifact."); break;
				case ART_PORTCHEST:
					pline("Artifact specs: magic resistance while carried, can be invoked to create portals, lawful, foxhound agent quest artifact."); break;
				case ART_SAINT_SOMETHING_FOUR_CRYST:
					pline("Artifact specs: regeneration, energy regeneration and reflection when wielded, can be invoked for taming, neutral, mahou shoujo quest artifact."); break;
				case ART_MASTER_BALL:
					pline("Artifact specs: regeneration, energy regeneration, reflection, magic resistance and bad effects when wielded, +16 to-hit and +32 damage, can be invoked for taming, neutral, doll mistress quest artifact."); break;
				case ART_ONE_RING:
					pline("Artifact specs: regeneration, energy regeneration and reflection when worn, can be invoked to create portals, lawful, ringseeker quest artifact."); break;
				case ART_IMPERIAL_TOKEN:
					pline("Artifact specs: drain resistance when worn, neutral, gladiator quest artifact."); break;
				case ART_PEARL_OF_WISDOM:
					pline("Artifact specs: half physical damage when worn, neutral, korsair quest artifact."); break;
				case ART_MAUI_S_FISHHOOK:
					pline("Artifact specs: half spell damage and searching when wielded, warns of eels and +10 to-hit and double damage to eels, chaotic, diver quest artifact."); break;
				case ART_HELM_OF_STORMS:
					pline("Artifact specs: half physical damage when worn, magic resistance while carried, neutral, spacewars fighter quest artifact."); break;
				case ART_AMULET_OF_KINGS:
					pline("Artifact specs: can be invoked to create portals, lawful, camperstriker quest artifact."); break;
				case ART_LONGBOW_OF_DIANA:
					pline("Artifact specs: reflection when wielded, ESP while carried, +5 to-hit and double damage, can be invoked to create ammo, chaotic, ranger quest artifact."); break;
				case ART_HEFFER_S_BOW:
					pline("Artifact specs: warning, regeneration and energy regeneration when wielded, +6 to-hit and double damage, can be invoked to create ammo, lawful, druid quest artifact."); break;
				case ART_GUNBOW:
					pline("Artifact specs: half physical damage when wielded, energy regeneration and drain resistance while carried, +7 to-hit and double damage to shock-susceptible monsters, can be invoked to create ammo, neutral, amazon quest artifact."); break;
				case ART_MASTER_KEY_OF_THIEVERY:
					pline("Artifact specs: warning, teleport control and half physical damage while carried, can be invoked for untrapping, chaotic, rogue quest artifact."); break;
				case ART_NOCTURNAL_KEY:
					pline("Artifact specs: searching, hallucination resistance and acts as a luckstone while carried, can be invoked to create portals, chaotic, locksmith quest artifact."); break;
				case ART_TSURUGI_OF_MURAMASA:
					pline("Artifact specs: can bisect enemies, acts as a luckstone when wielded, lawful, samurai quest artifact."); break;
				case ART_VIVEC_BLADE:
					pline("Artifact specs: acts as a luckstone when wielded, +8 to-hit and +8 damage, neutral, ordinator quest artifact."); break;
				case ART_SUMMONED_SWORD:
					pline("Artifact specs: stealth and regeneration while carried, +4 to-hit and +8 damage, can be invoked for dragon breath, chaotic, thalmor quest artifact."); break;
				case ART_BOW_OF_VINES:
					pline("Artifact specs: acts as a luckstone when wielded, ESP while carried, +7 to-hit and +10 damage, can be invoked for energy boost, neutral, bosmer quest artifact."); break;
				case ART_AMBASSADOR_ROBE:
					pline("Artifact specs: drain resistance, half spell damage and half physical damage when worn, energy regeneration and magic resistance while carried, can be invoked to create portals, chaotic, altmer quest artifact."); break;
				case ART_N_WAH_KILLER:
					pline("Artifact specs: +5 to-hit and +12 damage, magic resistance while carried, lawful, dunmer quest artifact."); break;
				case ART_SUPREME_JUSTICE_KEEPER:
					pline("Artifact specs: protection and acts as a luckstone when wielded, +5 to-hit and double damage to crossaligned monsters, lawful."); break;
				case ART_HOLYDIRK:
					pline("Artifact specs: reflection when wielded, half physical damage and magic resistance while carried, +5 to-hit and double damage to undead, lawful, medium quest artifact."); break;
				case ART_CHARMPOINT:
					pline("Artifact specs: reflection when wielded, half physical damage and magic resistance while carried, +5 to-hit and double damage to undead, can be invoked for taming, chaotic, sexymate quest artifact."); break;
				case ART_SILVER_CRYSTAL:
					pline("Artifact specs: ESP, magic resistance and regeneration while carried, can be invoked for healing, lawful, fighter quest artifact."); break;
				case ART_RED_STONE_OF_EIGIA:
					pline("Artifact specs: warning, drain and fire resistance while carried, can be invoked for object detection, neutral, stand user quest artifact."); break;
				case ART_FORTUNE_SWORD:
					pline("Artifact specs: acts as a luckblade when wielded, ESP and magic resistance while carried, +3 to-hit and +8 damage, neutral, fencer quest artifact."); break;
				case ART_YENDORIAN_EXPRESS_CARD:
					pline("Artifact specs: ESP, half spell damage and magic resistance while carried, can be invoked to charge objects, neutral, tourist quest artifact."); break;
				case ART_CREDEX_GOLD:
					pline("Artifact specs: stealth and teleport control while carried, can be invoked to charge objects, neutral, supermarket cashier quest artifact."); break;
				case ART_STAKE_OF_VAN_HELSING:
					pline("Artifact specs: magic resistance while carried, +5 to-hit and +12 damage, can instakill vampires, lawful, undead slayer quest artifact."); break;
				case ART_VAMPIRE_KILLER:
					pline("Artifact specs: magic resistance while carried, +5 to-hit and +12 damage, can instakill vampires, lawful, lunatic quest artifact."); break;
				case ART_ITCHALAQUIAQUE:
					pline("Artifact specs: magic resistance while carried, +5 to-hit and +12 damage, lawful, anachrononononononaut quest artifact."); break;
				case ART_ORB_OF_FATE:
					pline("Artifact specs: acts as a luckstone when wielded, warning, half physical damage and half spell damage while carried, can be invoked for level teleport, neutral, valkyrie quest artifact."); break;
				case ART_ORB_OF_RESISTANCE:
					pline("Artifact specs: warning, magic resistance, half spell damage and half physical damage while carried, can be invoked to light areas, neutral, paladin quest artifact."); break;
				case ART_EYE_OF_THE_AETHIOPICA:
					pline("Artifact specs: energy regeneration, half spell damage and magic resistance while carried, can be invoked to create portals, neutral, wizard quest artifact."); break;
				case ART_MEDALLION_OF_SHIFTERS:
					pline("Artifact specs: energy regeneration, half physical damage and magic resistance while carried, can be invoked for level teleport, neutral, shapeshifter quest artifact."); break;
				case ART_KING_S_STOLEN_CROWN:
					pline("Artifact specs: half spell damage, half physical damage and magic resistance while carried, can be invoked for level teleport, lawful, jester quest artifact."); break;
				case ART_SLIME_CROWN:
					pline("Artifact specs: half spell damage, half physical damage and magic resistance while carried, can be invoked for level teleport, chaotic, DQ Slime quest artifact."); break;
				case ART_CROWN_OF_SAINT_EDWARD:
					pline("Artifact specs: half spell damage and magic resistance while carried, lawful, yeoman quest artifact."); break;
				case ART_LIGHTSABER_PROTOTYPE:
					pline("Artifact specs: reflection when wielded, +5 to-hit and +10 damage, can be invoked for energy boost, lawful, jedi quest artifact."); break;
				case ART_SILVER_SNIVER:
					pline("Artifact specs: warning of demons when wielded."); break;
				case ART_JESUS_MUST_DIE:
					pline("Artifact specs: +4 to-hit and +10 damage."); break;
				case ART_RANDOMISATOR:
					pline("Artifact specs: +4 to-hit and +10 damage, bad effects when wielded."); break;
				case ART_MINI_PEOPLE_EATER:
					pline("Artifact specs: +10 to-hit and double damage versus humanoids, warning of humanoids when wielded."); break;
				case ART_TIGATOR_S_THORN:
					pline("Artifact specs: +5 to-hit and double damage to animals, displays all pokemon when wielded."); break;
				case ART_GIMLI_S_WAR_AXE:
					pline("Artifact specs: +5 to-hit and +10 damage, fire resistance when wielded, neutral."); break;
				case ART_RATTLE_BATTLE:
					pline("Artifact specs: +2 to-hit and +4 damage."); break;
				case ART_SLEEPLESS_NIGHTS:
					pline("Artifact specs: sleep resistance when wielded, chaotic."); break;
				case ART_SCHRINGELING:
					pline("Artifact specs: +8 to-hit and +2 damage."); break;
				case ART_MEMETAL:
					pline("Artifact specs: +6 to-hit and +16 damage, deafness when wielded, chaotic."); break;
				case ART_CUBIC_BONE:
					pline("Artifact specs: +4 to-hit and +4 damage, cold and level-drain resistance when wielded, lawful."); break;
				case ART_BROOMCHAMBER_ENDURANCE:
					pline("Artifact specs: regeneration and energy regeneration when wielded, lawful."); break;
				case ART_PINSELFLINSELING:
					pline("Artifact specs: +7 to-hit and +2 damage, reflection when wielded, wielding it allows you to engrave without fail unless you're impaired."); break;
				case ART_TEAMANTBANE:
					pline("Artifact specs: +5 to-hit and double damage versus ants."); break;
				case ART_LASER_PALADIN:
					pline("Artifact specs: wielding it gives extra multishot to all your ranged attacks, lawful."); break;
				case ART_ARMORWREAKER:
					pline("Artifact specs: +20 damage, disables stealth and aggravates monsters when wielded."); break;
				case ART_DESTRUCTION_BALL:
					pline("Artifact specs: +40 damage, using it in melee can reduce its enchantment unless it's already -20 or worse."); break;
				case ART_YESTERDAY_ASTERISK:
					pline("Artifact specs: +5 to-hit and +14 damage, will occasionally time you while wielded."); break;
				case ART_MELEE_DUALITY:
					pline("Artifact specs: double attacks while wielded."); break;
				case ART_SMAAAAAAAAAAAASH:
					pline("Artifact specs: +10 to-hit and +6 damage."); break;
				case ART_STROMBRINGER:
					pline("Artifact specs: +5 to-hit and +2 level-drain damage, drain resistance when wielded, bloodthirsty, chaotic."); break;
				case ART_BLAZERUNNER:
					pline("Artifact specs: +8 to-hit and +8 damage to fire-susceptible monsters, fire resistance when wielded."); break;
				case ART_CIVIL_WAR:
					pline("Artifact specs: conflict when wielded, chaotic."); break;
				case ART_HEAVY_THUNDERSTORM:
					pline("Artifact specs: +5 to-hit and +2 level-drain damage, drain resistance when wielded, bloodthirsty, chaotic."); break;
				case ART_RIGHTLASH_LEFT:
					pline("Artifact specs: applying it at a monster can occasionally improve its enchantment value, up to a maximum of +15."); break;
				case ART_ALASSEA_TELEMNAR:
					pline("Artifact specs: +5 to-hit and double drain life damage, bloodthirsty, heavily curses itself and displays fleecy-colored glyphs if wielded, wielding it for too long can cause it to disintegrate, chaotic."); break;
				case ART_GILRAEN_SEREGON:
					pline("Artifact specs: +5 to-hit and +10 damage, fire and stoning resistance when wielded, neutral."); break;
				case ART_VAMPIREBANE:
					pline("Artifact specs: +5 to-hit and double damage to vampires, warns of vampires when wielded."); break;
				case ART_GOLEMBANE:
					pline("Artifact specs: +5 to-hit and double damage to golems, warns of golems when wielded."); break;
				case ART_POINTLESS_JAVELIN:
					pline("Artifact specs: +2 to-hit and +10 damage."); break;
				case ART_EELBANE:
					pline("Artifact specs: +5 to-hit and double damage to sea creatures, warning of semicolons when wielded."); break;
				case ART_MOVE_IN_THE_SHADOWS:
					pline("Artifact specs: invisibility and protection when wielded, chaotic."); break;
				case ART_ANACHRONONONONAUT_PACKAGE:
					pline("Artifact specs: +8 to-hit and +8 damage to lightning-susceptible monsters, shock resistance when wielded; if you're an anachronist, wielding it also gives unbreathing."); break;
				case ART_GLASSPOINT:
					pline("Artifact specs: +8 to-hit and +4 damage."); break;
				case ART_EAMANE_LUINWE:
					pline("Artifact specs: +10 to-hit and +12 damage to fire-susceptible monsters, stealth, confusion and very fast speed when wielded, neutral."); break;
				case ART_GUN_CONTROL_LAWS:
					pline("Artifact specs: +5 to-hit and +40 damage, autocurses when wielded, carries an ancient Morgothian curse, chaotic."); break;
				case ART_OVERHEATER:
					pline("Artifact specs: +5 to-hit and +16 damage to fire-susceptible monsters, fire resistance when wielded, autocurses; while wielding it, fire traps occasionally spawn underneath you."); break;
				case ART_MAXIMUM_LAUNCH_POWER:
					pline("Artifact specs: extra multishot."); break;
				case ART_KILL_THEM_ALL:
					pline("Artifact specs: +20 to-hit and +40 damage."); break;
				case ART_PSCHIIIIIIIII:
					pline("Artifact specs: +5 to-hit and +40 damage."); break;
				case ART_ELECTROCUTION:
					pline("Artifact specs: +10 to-hit and +60 damage to shock-susceptible monsters."); break;
				case ART_POWER_PELLET:
					pline("Artifact specs: +1 to-hit and double damage."); break;
				case ART_FIRE_IN_THE_HOLE:
					pline("Artifact specs: +5 to-hit and +40 damage to fire-susceptible monsters."); break;
				case ART_POWERED_ARBALEST:
					pline("Artifact specs: +2 to-hit and +12 damage."); break;
				case ART_DEMON_BREAKPOINT:
					pline("Artifact specs: +20 damage."); break;
				case ART_LITTLE_ANNOYANCE:
					pline("Artifact specs: +6 to-hit and +2 damage."); break;
				case ART_OUCHIE_OUCH:
					pline("Artifact specs: +2 to-hit and +10 damage."); break;
				case ART_LAG_SPIKE:
					pline("Artifact specs: +10 to-hit and +6 damage."); break;
				case ART_PUNISHMENT_FOR_YOU:
					pline("Artifact specs: +3 damage per rank in your flail skill, lawful."); break;
				case ART_SHOCKLASH:
					pline("Artifact specs: +8 to-hit and +12 damage to shock-susceptible monsters, shock resistance when wielded."); break;
				case ART_EVERYTHING_MUST_BURN:
					pline("Artifact specs: +2 to-hit and +16 damage to fire-susceptible monsters, fire resistance and ability to survive in lava when wielded, but wielding it will occasionally burn you."); break;
				case ART_FEMALE_BEAUTY:
					pline("Artifact specs: magic resistance when worn, +5 charisma if you're female but -5 to all stats if not."); break;
				case ART_BIENVENIDO_A_MIAMI:
					pline("Artifact specs: fire and cold resistance and +3 charisma when worn."); break;
				case ART_THERMAL_BATH:
					pline("Artifact specs: swimming, sickness resistance, turn limitation and protects your stuff from getting wet while worn."); break;
				case ART_GIANT_SWINGING_PENIS:
					pline("Artifact specs: wearing it as a male character improves your AC by 10 points, otherwise it sets your AC to 10 with no way to improve it."); break;
				case ART_MAEDHROS_SARALONDE:
					pline("Artifact specs: 5 extra points of AC, +2 melee damage, and you can pray successfully 250 turns earlier while wearing it, neutral."); break;
				case ART_WATER_SHYNESS:
					pline("Artifact specs: magic resistance and reflection when worn, disables flying and swimming, heavily autocurses, occasionally spawns pools underneath you (but thankfully those don't autotrigger)."); break;
				case ART_PRECIOUS_VIRGINITY:
					pline("Artifact specs: protection when worn, prevents sexual encounters if you're female."); break;
				case ART_VERY_INVISIBLE:
					pline("Artifact specs: stealth, invisibility and displacement when worn, neutral."); break;
				case ART_CHECK_YOUR_ESCAPES:
					pline("Artifact specs: prevents teleportation and grants sickness resistance and free action when worn."); break;
				case ART_WOODSTOCK:
					pline("Artifact specs: improves your chance to block with a shield, reduces the chance that arrows (but not e.g. bolts) break while you're wearing it."); break;
				case ART_NOPPED_SUIT:
					pline("Artifact specs: 3 extra points of AC when worn."); break;
				case ART_LUKE_S_JEDI_POWER:
					pline("Artifact specs: allows you to jump and use the force when worn, lawful."); break;
				case ART_GREGOR_S_GANGSTER_GARMENTS:
					pline("Artifact specs: using the #borrow command is more likely to work while wearing it, can be invoked for object detection, chaotic."); break;
				case ART_SOFT_GIRL:
					pline("Artifact specs: regeneration, 5 extra points of AC, changes your gender if you wear it without being female."); break;
				case ART_SHRINK_S_AID:
					pline("Artifact specs: acid resistance, half physical damage and 7 extra points of AC when worn, autocurses."); break;
				case ART_LEA_S_SCHOOL_UNIFORM:
					pline("Artifact specs: It looks nice and comfortable."); break;
				case ART_LARIEN_TELRUNYA:
					pline("Artifact specs: stealth when worn, lawful."); break;
				case ART_FIREBURN_COLDSHATTER:
					pline("Artifact specs: fire and cold resistance and 5 extra points of AC when worn."); break;
				case ART_NO_MORE_EXPLOSIONS:
					pline("Artifact specs: fire resistance when worn, reduces chance of alchemic blasts and improves your chances of making your own potions with a chemistry set."); break;
				case ART_PREMIUM_VISCOSITY:
					pline("Artifact specs: poison resistance when worn, grabbing monsters (e.g. eels) will slip off without actually managing to grab you."); break;
				case ART_COOKING_COURSE:
					pline("Artifact specs: fire resistance when worn, stepping on a fire trap can extinguish it, neutral."); break;
				case ART_ROKKO_CHAN_S_SUIT:
					pline("Artifact specs: jumping, very fast speed and +5 to-hit for ranged attacks when worn, but disables your ability to score critical hits and caps your strength and dexterity at 12."); break;
				case ART_FULLY_LIONIZED:
					pline("Artifact specs: stealth, speed and hunger when worn."); break;
				case ART_COLD_LIKE_A_CORPSE:
					pline("Artifact specs: cold resistance when worn."); break;
				case ART_YAUI_GAUI_FURS:
					pline("Artifact specs: 5 extra points of AC when worn."); break;
				case ART_DEATHCLAW_HIDE:
					pline("Artifact specs: +10 to-hit for your melee attacks when worn."); break;
				case ART_FAST_CAMO_PREDATOR:
					pline("Artifact specs: stealth, speed and hunger when worn."); break;
				case ART_PREDATORY_STABILITY:
					pline("Artifact specs: free action when worn, neutral."); break;
				case ART_SPACEWASTE:
					pline("Artifact specs: 3 extra points of AC when worn."); break;
				case ART_BUGNOSE:
					pline("Artifact specs: fire resistance and displays all 'a' and 'x' when worn."); break;
				case ART_DISBELIEVING_POWERLORD:
					pline("Artifact specs: adds d5 to your melee damage when worn."); break;
				case ART_DOUBLE_NEGATION:
					pline("Artifact specs: acts as a luckstone when worn."); break;
				case ART_DONALD_TRUMP_S_PRESIDENTIA:
					pline("Artifact specs: warning and hallucination resistance when worn, chaotic."); break;
				case ART_GODLESS_VOID:
					pline("Artifact specs: blocks telepathy when worn."); break;
				case ART_NUMBER_____:
					pline("Artifact specs: no specialties."); break;
				case ART_JANA_S_EXTREME_HIDE_AND_SE:
					pline("Artifact specs: allows you to conceal underneath items. You do not know if it does anything else though..."); break;
				case ART_LAST_STEELING:
					pline("Artifact specs: while wearing it, you may occasionally be able to rustproof an iron object in your inventory. Make sure the object you pick is actually made of iron and not e.g. some other metal, or it won't work!"); break;
				case ART_NOTHING_REALLY_SPECIAL:
					pline("Artifact specs: protection when worn."); break;
				case ART_PRIMITIVE_SHIELDING:
					pline("Artifact specs: protection when worn."); break;
				case ART_TARI_FEFALAS:
					pline("Artifact specs: poison resistance when worn, monsters that attack you will take poison damage, causes radio broadcasts, slippery hands heal in one turn, lawful."); break;
				case ART_STEELSKULL_PROTECTOR:
					pline("Artifact specs: 3 extra points of AC when worn."); break;
				case ART_ELESSAR_ELENDIL:
					pline("Artifact specs: speed and fumbling when worn, chaotic."); break;
				case ART_SEXYNESS_HAS_A_NAME:
					pline("Artifact specs: effects that heal your hit points are doubled in effectiveness, or quadrupled if you're a healer; lawful."); break;
				case ART_SQUEAKY_TENDERNESS:
					pline("Artifact specs: while wearing it, monsters that can fart have a certain chance of spawning tame. Because I know that you like to listen to squeaky farting noises. :-) --Amy"); break;
				case ART_IT_BREATHES_MORE:
					pline("Artifact specs: can be invoked for dragon breath."); break;
				case ART_CONESHAPE_HAT:
					pline("Artifact specs: acid resistance when worn."); break;
				case ART_HARD_HAT_AREA:
					pline("Artifact specs: 5 extra points of AC when worn."); break;
				case ART_DUNCE_POUNCE:
					pline("Artifact specs: wearing it caps your intelligence and wisdom at 6 but increases strength by 5 and dexterity by 3."); break;
				case ART_REMOTE_GAMBLE:
					pline("Artifact specs: +2 damage and accuracy when worn."); break;
				case ART_HOT_HEADED_HAT:
					pline("Artifact specs: fire resistance when worn."); break;
				case ART_GREEN_STATUS:
					pline("Artifact specs: poison resistance when worn."); break;
				case ART_ALLURATION:
					pline("Artifact specs: You feel like having to wear this."); break;
				case ART_WOLF_KING:
					pline("Artifact specs: no digestion, weak sight, right mouse button loss and blocks telepathy when worn, heavily autocurses."); break;
				case ART_WSCHIIIIIE_:
					pline("Artifact specs: clairvoyance when worn."); break;
				case ART_NEVER_CLEAN:
					pline("Artifact specs: confusion when worn."); break;
				case ART_WEB_RADIO:
					pline("Artifact specs: internet access when worn."); break;
				case ART_DULLIFIER:
					pline("Artifact specs: stealth and invisibility when worn, but disables sleep resistance."); break;
				case ART_SOON_THERE_WILL_BE_AN_ERRO:
					pline("Artifact specs: reflection and magic resistance when worn, but the name should be warning enough..."); break;
				case ART_DOUBLE_JEOPARDY:
					pline("Artifact specs: teleportitis and polymorphitis when worn, chaotic."); break;
				case ART_IF_THE_RIGHT_MOUSE_BUTTON_:
					pline("Artifact specs: energy regeneration when worn. You do not recognize any other effects that this item may have."); break;
				case ART_IRON_HELM_OF_GORLIM:
					pline("Artifact specs: +10 to-hit and damage when worn, carries a Topi Ylinen curse, chaotic."); break;
				case ART_NEVEREATER:
					pline("Artifact specs: slow digestion when worn."); break;
				case ART_CERTAIN_SLOW_DEATH:
					pline("Artifact specs: conflict when worn."); break;
				case ART_DRINK_COCA_COLA:
					pline("Artifact specs: regeneration and hunger when worn, lawful."); break;
				case ART_HAVE_ALL_YOU_NEED:
					pline("Artifact specs: fire, cold, shock and sleep resistance when worn."); break;
				case ART_NOSED_BUG:
					pline("Artifact specs: improves your AC by 7 points when worn."); break;
				case ART_MASSIVE_IRON_CROWN_OF_MORG:
					pline("Artifact specs: resist fire, cold, shock, poison and acid and +5 all stats when worn, will prime curse itself and carries an ancient Morgothian curse. Yes, I know in ToME the latter was not the case... but this isn't ToME! :D --Amy"); break;
				case ART_RANDOMNESS_PREVAILS:
					pline("Artifact specs: teleportitis and polymorphitis when worn, disables teleport control and polymorph control, neutral."); break;
				case ART_CASQUESPIRE_TRANSLATE:
					pline("Artifact specs: 5 extra points of AC when worn."); break;
				case ART_GOLD_STANDARD:
					pline("Artifact specs: while wearing it, monsters that drop gold will generally drop more gold than usual."); break;
				case ART_HELMET_OF_DIGGING:
					pline("Artifact specs: increases digging speed with pick-axes and such when worn."); break;
				case ART_ARMY_LEADER:
					pline("Artifact specs: ESP when worn, soldiers and their higher ranks will occasionally spawn tame."); break;
				case ART_SECURE_BATHMASTER:
					pline("Artifact specs: resist fire, cold and light when worn, lawful."); break;
				case ART_DEEP_INSANITY:
					pline("Artifact specs: magic resistance, reflection and inventorylessness when worn, autocurses, chaotic."); break;
				case ART_RADAR_NOT_WORKING:
					pline("Artifact specs: monsters never approach you on their own if you wear it, but you cannot detect them either and newly spawned ones are completely invisible."); break;
				case ART_BETTERVISION:
					pline("Artifact specs: ESP when worn."); break;
				case ART_REFUEL_BADLY:
					pline("Artifact specs: manaleech when worn, lawful."); break;
				case ART_BURN_OR_NO:
					pline("Artifact specs: fire resistance when worn, putting them on for the first time grants intrinsic burnopathy."); break;
				case ART_FREE_ACTION_CALLED_FREE_AC:
					pline("Artifact specs: if you guessed that wearing them gives free action, you are right!"); break;
				case ART_FUMBLEFINGERS_QUEST:
					pline("Artifact specs: while wearing them, sitting on a throne will always give the fumblefingers effect."); break;
				case ART_OH_LOOK_AT_THAT:
					pline("Artifact specs: wearing them while not having the petkeeping skill will unlock it and cap it at expert, but also prime curse the gloves."); break;
				case ART_LINE_IN_THE_SAND:
					pline("Artifact specs: fire resistance and trap revealing when worn."); break;
				case ART_HANDBOXED:
					pline("Artifact specs: greatly increases your carry capacity when worn."); break;
				case ART_YES_TO_RANGED_COMBAT:
					pline("Artifact specs: your ranged attacks do d6 extra damage while you're wearing them."); break;
				case ART_SPECTRATOR:
					pline("Artifact specs: 1 out of 5 turns you will resist fire, cold, shock, poison, acid, stone, drain and magic while wearing them, lawful."); break;
				case ART_USE_THE_FORCE_LUKE:
					pline("Artifact specs: using the #force command on a monster does much more damage while wielding them, can be invoked for levitation."); break;
				case ART_EXPERTENGAME_THE_ENTIRE_LE:
					pline("Artifact specs: shock resistance and flying when worn, but monsters will often create traps for you to stumble into."); break;
				case ART_WHAT_S_UP_BITCHES:
					pline("Artifact specs: while wearing them, nymphs are displayed and will almost always spawn peaceful. Sometimes they may even spawn tame. Neutral."); break;
				case ART_BALLS_FLYING_BACK_AND_FORT:
					pline("Artifact specs: reflection when worn."); break;
				case ART_WHAT_DO:
					pline("Artifact specs: hallucination resistance when worn, neutral."); break;
				case ART_GRABBER_MASTER:
					pline("Artifact specs: wearing them will occasionally give you a gold detection effect."); break;
				case ART_DEFENSIVE_MAGIC:
					pline("Artifact specs: magic resistance when worn."); break;
				case ART_NON_SOMETHING:
					pline("Artifact specs: half physical damage when worn."); break;
				case ART_DOUBLER_GLOVES:
					pline("Artifact specs: protection when worn."); break;
				case ART_DWARVEN_BONG:
					pline("Artifact specs: no specialties."); break;
				case ART_ABSURD_HEELED_TILESET:
					pline("Artifact specs: count as stiletto heels."); break;
				case ART_GRANDPA_S_BROGUES:
					pline("Artifact specs: fear resistance when worn."); break;
				case ART_VERA_S_FREEZER:
					pline("Artifact specs: cold resistance and freezopathy when worn, kicking a monster with them can slow it down and cold attacks cannot shatter your potions. The 'freeze' status effect will slow you down less. However, your fire resistance is deactivated and the 'burn' status effect cannot be cured other by waiting it out. Lawful."); break;
				case ART_HIGH_HEELED_HUG:
					pline("Artifact specs: count as high heels (specifically, hugging boots with block heels, which also exist in real life), neutral."); break;
				case ART_FREE_FOR_ENOUGH:
					pline("Artifact specs: free action when worn, neutral."); break;
				case ART_DOUBLE_SAFETY:
					pline("Artifact specs: reflection when worn."); break;
				case ART_LOVELY_GIRLS_WEAR_PLATEAU_:
					pline("Artifact specs: cold resistance and half physical damage when worn."); break;
				case ART_FATALITY:
					pline("Artifact specs: conflict when worn, chaotic."); break;
				case ART_PORCELAIN_ELEPHANT:
					pline("Artifact specs: aggravate monster and increased chance of waking up monsters when worn, 5 extra points of AC."); break;
				case ART_FD_DETH:
					pline("Artifact specs: displays all 'f' and 'd' when worn."); break;
				case ART_LOVELY_GIRL_PLATEAUS:
					pline("Artifact specs: kicking a monster with them will stun and confuse it, aggravate monster when worn, newly spawned monsters are always hostile, chaotic."); break;
				case ART_KYLIE_LUM_S_SNAKESKIN_BOOT:
					pline("Artifact specs: If you put them on, all monsters will be spawned hostile for the remaining game. If you kick a monster with them, you do 10 extra points of damage and the kick cannot be clumsy and may sometimes paralyze the target. But wearing them as a non-topmodel causes them to carry an ancient Morgothian curse, and don't even think about putting them on if you're a failed existence!"); break;
				case ART_SANDRA_S_BEAUTIFUL_FOOTWEA:
					pline("Artifact specs: confusion resistance, swimming and unbreathing when worn, chaotic."); break;
				case ART_UNEVEN_STILTS:
					pline("Artifact specs: +15 charisma when worn, but they can also occasionally cause you to fumble."); break;
				case ART_NEANDERTHAL_SOCCER_CLUB:
					pline("Artifact specs: kicking an item with them will exercise strength and dexterity if the item moved. The neanderthals played soccer with them."); break;
				case ART_SHIN_KICK_OF_LOVE:
					pline("Artifact specs: kicking a monster with them may very rarely cause it to become peaceful, lawful."); break;
				case ART_ABSOLUTE_AUTOCURSE:
					pline("Artifact specs: curses your entire inventory if you put it on."); break;
				case ART_LUCKY_GADGET:
					pline("Artifact specs: acts as a luckstone when worn."); break;
				case ART_CLEARVISION:
					pline("Artifact specs: hallucination resistance when worn."); break;
				case ART_GUARANTEED_HIT_POWER:
					pline("Artifact specs: wearing it improves your dexterity by its enchantment value."); break;
				case ART_UNBELIEVABLY_STRONG_PUNCH:
					pline("Artifact specs: wearing it improves your strength by its enchantment value."); break;
				case ART_DEATHLY_COLD:
					pline("Artifact specs: fire resistance when worn, but disables cold resistance."); break;
				case ART_MAGICAL_SHOCK:
					pline("Artifact specs: magic and level-drain resistance when worn, neutral."); break;
				case ART_GOOD_THINGS_WILL_HAPPEN_EV:
					pline("Artifact specs: wearing them causes the turn counter to advance half as fast, lawful."); break;
				case ART_REQUIRED_GLADNESS:
					pline("Artifact specs: speed when worn."); break;
				case ART_NEVER_NEEDED:
					pline("Artifact specs: reflection when worn."); break;
				case ART_KNOWLEDGEABLE_FAILURE:
					pline("Artifact specs: ESP when worn, lawful."); break;
				case ART_SCRAWNY_PIPSQUEAK:
					pline("Artifact specs: shock resistance and 5 extra points of AC when worn, chaotic."); break;
				case ART_TSCHOECK_KLOECK:
					pline("Artifact specs: if you're foolish enough to put it on, your polymorph control will be disabled for 1 million turns. And I can't guarantee that your game will even take that long."); break;
				case ART_SPELLCASTER_S_DREAM:
					pline("Artifact specs: energy regeneration and half spell damage when worn, but disables sleep resistance."); break;
				case ART_LOW_ZERO_NUMBER:
					pline("Artifact specs: You are not sure what this amulet does. Maybe it allows you to divide by zero?"); break;
				case ART_DYNAMITUS:
					pline("Artifact specs: fire resistance when worn, but will cause explosions centered on you every once in a while, chaotic."); break;
				case ART_I_NEVER_TAKE_DRUGS:
					pline("Artifact specs: hallucination resistance when worn, lawful."); break;
				case ART_UOY_OT_KCAB_DNES:
					pline("Artifact specs: reflection when worn."); break;
				case ART_GOODBYE_TROLLS:
					pline("Artifact specs: warns of trolls when worn."); break;
				case ART_PRIAMOS__TREASURE:
					pline("Artifact specs: It can hold even more stuff than a regular chest of holding."); break;
				case ART_ICE_BLOCK_HARHARHARHARHAR:
					pline("Artifact specs: using it as a melee weapon will add 2 points of damage for every corpse in it, up to a maximum of 15 corpses."); break;
				case ART_RECYCLER_BIN:
					pline("Artifact specs: you gain 1 point of alignment record for every item it deletes. If it deletes an artifact, you gain 50 points of alignment and +5 to your maximum alignment."); break;
				case ART_SURFING_FUN:
					pline("Artifact specs: completely prevents its contents from getting wet, even if it's cursed."); break;
				case ART_MONSTERATOR:
					pline("Artifact specs: if it deletes at least 10 items at once, monsters are created proportional to the # of items that were in it, but it will also greatly increase your prayer timeout to thwart the inevitable altar scumming that you want to do."); break;
				case ART_GO_AWAY_YOU_BASTARD:
					pline("Artifact specs: if it's not cursed, applying it causes a phase door effect, but it will often curse itself after you used it."); break;
				case ART_BATTLEHORN_OF_SESCHERON:
					pline("Artifact specs: Has no special effect beyond its base item."); break;
				case ART_HELLISH_WARTUBE:
					pline("Artifact specs: Has no special effect beyond its base item."); break;
				case ART_HEAVEN_S_CALL_TO_ARMS:
					pline("Artifact specs: Has no special effect beyond its base item."); break;
				case ART_BIMMEL_BIMMEL:
					pline("Artifact specs: applying it tries to tame all 'x' adjacent to you, but they get a resistance roll."); break;
				case ART_SCRABBLE_BABBLE:
					pline("Artifact specs: engraving with it will only consume charges 1 out of 10 turns."); break;
				case ART_I_THE_SAGE:
					pline("Artifact specs: Well, it's a switcher like every other one..."); break;
				case ART_F_PROT:
					pline("Artifact specs: Who knows what the contents of these disks is?"); break;
				case ART_PANIC_IN_GOTHAM_FOREST:
					pline("Artifact specs: fills the entire level with trees if you read it."); break;
				case ART_ENSBADEB_FRAUSED:
					pline("Artifact specs: Has no special effect beyond its base item, chaotic."); break;
				case ART_MOVIE_ANALOGY:
					pline("Artifact specs: Has no special effect beyond its base item."); break;
				case ART_SUPERGIRL_S_JUMP_AND_RUN_F:
					pline("Artifact specs: jumping when wielded, lawful."); break;
				case ART_AUTOMATIC_POKE_BALL:
					pline("Artifact specs: having it in your inventory gives a low chance of pokemon being spawned tame."); break;
				case ART_CUBIC_SODIUM_CHLORIDE:
					pline("Artifact specs: dissolving it will free the monsters contained within, and they will be grateful."); break;
				case ART_MAGIC_RESISTANCE_GET:
					pline("Artifact specs: magic resistance when carried. Hooray if you are a giant."); break;
				case ART_SHOCKING_THERAPY:
					pline("Artifact specs: +5 to-hit and +14 damage to shock-susceptible monsters, disables shock resistance when wielded, chaotic."); break;
				case ART_ENIGMATIC_RIDDLE:
					pline("Artifact specs: +7 to-hit and +2 damage."); break;
				case ART_DO_YOU_EVEN_LIFT:
					pline("Artifact specs: double damage, reflection and drain resistance when wielded, magic resistance when carried. But it's actually too heavy to lift, so I wonder, how did you get it in your inventory to be able to read these lines???"); break;
				case ART_GANGBANGING_LIKE_A_BOSS:
					pline("Artifact specs: +20 damage, chaotic."); break;
				case ART_FREEZEMETAL:
					pline("Artifact specs: +7 to-hit and +10 damage to cold-susceptible monsters."); break;
				case ART_KINGS_RANSOM_FOR_YOU:
					pline("Artifact specs: +5 to-hit and +10 stun damage, protection and reflection when wielded but also halves your movement speed and gains a Topi Ylinen curse."); break;
				case ART_DO_NOT_THROW_ME:
					pline("Artifact specs: DO NOT throw it. If you do, YOU WILL LOSE YOUR CHARACTER. This is not a joke."); break;
				case ART_WATERS_OF_OBLIVION:
					pline("Artifact specs: swimming when worn, demons are almost always spawned peaceful and occasionally tame, but it will repeatedly cause amnesia."); break;
				case ART_JONADAB_S_WINTER_WEAR:
					pline("Artifact specs: cold resistance when worn. According to Jonadab they're also hideously ugly."); break;
				case ART_MADMAN_S_POWER:
					pline("Artifact specs: manaleech and energy regeneration when worn, chaotic."); break;
				case ART_REMEMBERING_THE_BAD_TIMES:
					pline("Artifact specs: keen memory when worn."); break;
				case ART_EIGHTH_DEADLY_SIN:
					pline("Artifact specs: magic resistance when worn, but the Sins will eventually get back at you to exact punishment!"); break;
				case ART_PERMANENTITIS:
					pline("Artifact specs: disables polymorph control when worn, and your polymorphs will never time out."); break;
				case ART_WATERFORCE____:
					pline("Artifact specs: swimming and unbreathing when worn."); break;
				case ART_NOW_IT_S_FOR_REAL:
					pline("Artifact specs: conflict, regeneration and energy regeneration when worn."); break;
				case ART_BLACK_VEIL_OF_BLACKNESS:
					pline("Artifact specs: magic resistance when worn, produces an anti-magic shell, carries an ancient Morgothian curse."); break;
				case ART_CLAPCLAP:
					pline("Artifact specs: acts as a luckstone when worn."); break;
				case ART_MORE_HIGHER:
					pline("Artifact specs: ESP and magic resistance when worn, monsters often spawn with the escalation egotype, and the escalation counter will automatically increase over time."); break;
				case ART_FILTHY_MORTALS_WILL_DIE:
					pline("Artifact specs: 10 points of negative protection whenever you put it on (which means they add up, ruining your armor class), so don't do it!!!"); break;
				case ART_DSCHLSCHLSCHLSCHLSCH:
					pline("Artifact specs: You should not put this on, because it deactivates your magic resistance for the turn it takes to wear it..."); break;
				case ART_HAHAHA_HA_HAHAHAHAHA:
					pline("Artifact specs: It's funny looking."); break;
				case ART_CLEANLINESS_LAB:
					pline("Artifact specs: sickness resistance when worn."); break;
				case ART_FLOATING_FLAME:
					pline("Artifact specs: fire resistance when worn."); break;
				case ART_DEMONIC_UNDEAD_RADAR:
					pline("Artifact specs: warning of demons when worn."); break;
				case ART_SEXY_STROKING_UNITS:
					pline("Artifact specs: 10 extra points of AC and +5 charisma when worn. By the way, Amy's roommate has a rainbow-colored one which looks very feminine and incredibly cuddly-fleecy!"); break;
				case ART_WAR_ME_NEVERTHELESS:
					pline("Artifact specs: reflection when worn, neutral."); break;
				case ART_JEDI_MIND_POWER:
					pline("Artifact specs: reflection when worn. If you wear it, you may learn telekinesis, but if you say yes, three random intrinsics are deactivated for 1 million turns!"); break;
				case ART_ARABELLA_S_LIGHTNINGROD:
					pline("Artifact specs: Apparently it's what Arabella uses to be safe from her own cursed items, so she can safely prepare them until they're ready to be used on hapless victims. Neutral."); break;
				case ART_KA_BLAMMO:
					pline("Artifact specs: If you trigger a trap while wielding it, its enchantment may go up or down, although it won't go above +10 or below -20. Bless it to increase the chance of the enchantment going up! If it's cursed, the chance of the enchantment going down is increased instead."); break;
				case ART_RNG_S_FUN:
					pline("Artifact specs: putting it on while it's +0 will randomize its enchantment value to something between -5 and +5."); break;
				case ART_YOU_RE_THE_BEST:
					pline("Artifact specs: hallucination resistance when worn."); break;
				case ART_ANASTASIA_S_SOFT_CLOTHES:
					pline("Artifact specs: half physical damage and 10 extra points of AC when worn. It's also made of very soft velvet."); break;
				case ART_PLENTYHORN_OF_FAMINE:
					pline("Artifact specs: applying it makes you more hungry."); break;
				case ART_MARINE_THREAT_NEUTERED:
					pline("Artifact specs: double damage to monsters that can swim."); break;
				case ART_BANG_BANG:
					pline("Artifact specs: +5 to-hit and +20 stun damage, wielding it sets it to +2 if its enchantment was lower, but also causes deafness. Chaotic."); break;
				case ART_TUNA_CANNON:
					pline("Artifact specs: +20 damage to cold-susceptible monsters, +1 multishot, neutral."); break;
				case ART_PFIIIIIIIIET:
					pline("Artifact specs: No specialties."); break;
				case ART_DOGWALK:
					pline("Artifact specs: No specialties."); break;
				case ART_FRIEND_CALL:
					pline("Artifact specs: Creates two familiars at once."); break;
				case ART_FOR_THE_GOOD_CAUSE:
					pline("Artifact specs: The effect applies to the confused radius even if you're not confused."); break;
				case ART_WARPCHANGE:
					pline("Artifact specs: teleportitis when worn."); break;
				case ART_SEMI_SHAPE_CONTROL:
					pline("Artifact specs: polymorphitis when worn."); break;
				case ART_FORMTAKER:
					pline("Artifact specs: putting it on gives intrinsic polymorphitis, chaotic."); break;
				case ART_COLORLESS_VARIETY:
					pline("Artifact specs: shades of grey effect when worn, neutral."); break;
				case ART_MEDICAL_OPHTHALMOSCOPE:
					pline("Artifact specs: displays extra information."); break;
				case ART_SEARCH_AND_YOU_WILL_FIND:
					pline("Artifact specs: acts as a luckstone when worn."); break;
				case ART_LOTS_OF_KNOWLEDGE:
					pline("Artifact specs: ESP when worn."); break;
				case ART_HOYO_HOYO_WOLOLO:
					pline("Artifact specs: does not need to be blessed in order to work."); break;
				case ART_HAAAAAAAAAAAAA_LELUJA:
					pline("Artifact specs: picking it up will bless it and identify its BUC status."); break;
				case ART_GRUUM_GRUUM:
					pline("Artifact specs: no specialties."); break;
				case ART_ANCIENT_SACRIFICE:
					pline("Artifact specs: no specialties."); break;
				case ART_ZEN_BUDDHISM:
					pline("Artifact specs: no specialties."); break;
				case ART_CHRRRRRRRRRRRRR:
					pline("Artifact specs: no specialties."); break;
				case ART_USELESS_ELEMENT:
					pline("Artifact specs: no specialties."); break;
				case ART_INVERSION_THERAPY:
					pline("Artifact specs: no specialties."); break;
				case ART_MAGICAL_BLINDFOLDING:
					pline("Artifact specs: no specialties."); break;
				case ART_TOOL_ASSISTED_MAGIC:
					pline("Artifact specs: no specialties."); break;
				case ART_ADD_ACID_TO_WATER:
					pline("Artifact specs: no specialties."); break;
				case ART_OHMYGODHELPME:
					pline("Artifact specs: no specialties."); break;
				case ART_CURSEBREAKING____:
					pline("Artifact specs: no specialties."); break;
				case ART_MALIGNANT_AURA:
					pline("Artifact specs: no specialties."); break;
				case ART_COATED_FOR_GOOD:
					pline("Artifact specs: no specialties."); break;
				case ART_ARABELLA_S_ESCAPE_ROUTE:
					pline("Artifact specs: no specialties."); break;
				case ART_NIGHT_MOVEMENT:
					pline("Artifact specs: no specialties."); break;
				case ART_SPAMMING_DEFENSE_MAGIC:
					pline("Artifact specs: no specialties."); break;
				case ART_FARTBOLT:
					pline("Artifact specs: no specialties."); break;
				case ART_JONADAB_S_EVIL_PATCH_ARTIF:
					pline("Artifact specs: +2 damage, chaotic."); break;
				case ART_WHAT_IT_SAYS_ON_THE_TIN:
					pline("Artifact specs: fire resistance when wielded, +2 to-hit and +20 damage to fire-susceptible monsters."); break;
				case ART_DEADLY_GAMBLING:
					pline("Artifact specs: +d30 damage, but wielding it has a small chance of instakilling you."); break;
				case ART_PRISMATIC_PROTECTION:
					pline("Artifact specs: resist cold, fire, poison and lightning when wielded."); break;
				case ART_IRRESISTIBLE_OFFENSE:
					pline("Artifact specs: no specialties."); break;
				case ART_EURGH:
					pline("Artifact specs: no specialties."); break;
				case ART_QUICK_SLOWNESS:
					pline("Artifact specs: no specialties."); break;
				case ART_FINAL_EXPLOSION:
					pline("Artifact specs: no specialties."); break;
				case ART_STALWART_BUNKER:
					pline("Artifact specs: no specialties."); break;
				case ART_FROZEN_POLAR_BEAR:
					pline("Artifact specs: no specialties."); break;
				case ART_DOENERTELLER_VERSACE:
					pline("Artifact specs: Eating it gives temporary resistance to level drain and magic, as well as reflection."); break;
				case ART_PROZACELF_SHATTERHAND:
					pline("Artifact specs: reflection when worn, lawful."); break;
				case ART_PROZACELF_S_AUTOHEALER:
					pline("Artifact specs: wearing it for a prolonged time will slowly increase your maximum health, but also cause temporary nastiness whenever you do get a health up."); break;
				case ART_PROZACELF_S_POOPDECK:
					pline("Artifact specs: chaos terrain when wielded, chaotic."); break;
				case ART_DIKKIN_S_DEADLIGHT:
					pline("Artifact specs: yellow spells when wielded, zapping it causes temporary yellow spells, zapping yourself allows you to control the polymorph or choose a polyform effect instead."); break;
				case ART_DIKKIN_S_DRAGON_TEETH:
					pline("Artifact specs: yellow spells when worn unless you're a kobold bard, flying and prevents you from wearing body armor when worn, allows you to choose a polyform effect if you polymorph, can be invoked for dragon breath."); break;
				case ART_DIKKIN_S_FAVORITE_SPELL:
					pline("Artifact specs: +8 intelligence and yellow spells when wielded, and zapping yourself with the spell while wielding it allows you to choose a polyform effect but if you do, you get temporary yellow spells."); break;
				case ART_SOULCALIBUR:
					pline("Artifact specs: +5 to-hit and +10 damage, searching and drain resistance when wielded, lawful."); break;
				case ART_UNDEADBANE:
					pline("Artifact specs: +5 to-hit and double damage to undead, lawful."); break;
				case ART_RAINBOWSWANDIR:
					pline("Artifact specs: +5 to-hit and double damage, hallucination resistance when wielded, lawful."); break;
				case ART_WIZARDBANE:
					pline("Artifact specs: +3 to-hit and +4 stun damage, magic resistance when wielded, neutral."); break;
				case ART_VORPAL_EDGE:
					pline("Artifact specs: +5 to-hit and +2 damage, beheads enemies, neutral."); break;
				case ART_DARK_MAGIC:
					pline("Artifact specs: magic resistance when worn."); break;
				case ART_BEAM_CONTROL:
					pline("Artifact specs: +2 to-hit and +10 damage, teleport control when wielded."); break;
				case ART_SANDRA_S_SECRET_WEAPON:
					pline("Artifact specs: +12 damage, searching, shock resistance, aggravate monster and can cause amnesia when wielded, chaotic."); break;
				case ART_DUMBOAK_S_HEW:
					pline("Artifact specs: +8 damage, blindness resistance when wielded."); break;
				case ART_POWER_AMMO:
					pline("Artifact specs: +5 to-hit and double damage, lawful."); break;
				case ART_BLOBLOBLOBLOBLO:
					pline("Artifact specs: +14 damage."); break;
				case ART_PSCHIUDITT:
					pline("Artifact specs: +2 to-hit and +10 damage."); break;
				case ART_RATTATTATTATTATT:
					pline("Artifact specs: +16 damage, aggravate monster when wielded."); break;
				case ART_FLAM_R:
					pline("Artifact specs: +10 to-hit and +2 damage to fire-susceptible monsters, fire resistance when wielded."); break;
				case ART_SURESHOT:
					pline("Artifact specs: +20 to-hit and +2 damage."); break;
				case ART_STINGWING:
					pline("Artifact specs: +7 to-hit and double damage."); break;
				case ART_NOBILE_MOBILITY:
					pline("Artifact specs: energy regeneration when worn."); break;
				case ART_ANTIMAGIC_FIELD:
					pline("Artifact specs: magic resistance and prevents spellcasting when worn."); break;
				case ART_NATALIA_IS_LOVELY_BUT_DANG:
					pline("Artifact specs: polymorph control and manaleech when worn, chaotic."); break;
				case ART_TAPE_ARMAMENT:
					pline("Artifact specs: reflection, magic resistance and superscrolling when worn."); break;
				case ART_CATHAN_S_SIGIL:
					pline("Artifact specs: regeneration when worn, increases strength by its enchantment value +3."); break;
				case ART_FLEEING_MINE_MAIL:
					pline("Artifact specs: It looks like a standard suit of armor."); break;
				case ART_GREY_FUCKERY:
					pline("Artifact specs: warning, magic resistance and shades of grey when worn."); break;
				case ART_LITTLE_PENIS_WANKER:
					pline("Artifact specs: Prevents your penis from contracting slexually transmitted diseases while you wear it, even if you're female. :-)"); break;
				case ART_D_TYPE_EQUIPMENT:
					pline("Artifact specs: allows you to swim in lava without burning up when worn."); break;
				case ART_INCREDIBLE_SWEETNESS:
					pline("Artifact specs: half physical damage when worn."); break;
				case ART_QUEEN_ARTICUNO_S_HULL:
					pline("Artifact specs: magic resistance, aggravate monster and conflict when worn, neutral."); break;
				case ART_DON_SUICUNE_USED_SELFDESTR:
					pline("Artifact specs: reflection, drain resistance, aggravate monster and nasty effects when worn."); break;
				case ART_WONDERCLOAK:
					pline("Artifact specs: drain resistance when worn, chaotic."); break;
				case ART_EVELINE_S_CIVIL_MANTLE:
					pline("Artifact specs: stealth and shock resistance and acts as a luckstone when worn, neutral."); break;
				case ART_INA_S_OVERCOAT:
					pline("Artifact specs: cold, disintegration and sickness resistance, searching, hunger and random fainting when worn, autocurses."); break;
				case ART_GROUNDBUMMER:
					pline("Artifact specs: aggravate monster and freezing when worn, disables flying, autocurses."); break;
				case ART_RITA_S_LOVELY_OVERGARMENT:
					pline("Artifact specs: It is very lovely! Rita made it in her lingerie studio. Chaotic."); break;
				case ART_LUNAR_ECLIPSE_TONIGHT:
					pline("Artifact specs: acts as a luckstone when worn."); break;
				case ART_LORSKEL_S_SPEED:
					pline("Artifact specs: very fast speed when worn and even adds extra speed sometimes. Made in honor of Lorskel who likes to wish for another artifact helmet with the same properties."); break;
				case ART____DOT__ALIEN_RADIO:
					pline("Artifact specs: shock resistance and allows you to listen to the radio when worn."); break;
				case ART_NADJA_S_DARKNESS_GENERATOR:
					pline("Artifact specs: +5 to-hit and repeatedly darkens areas when worn, autocurses."); break;
				case ART_THA_WALL:
					pline("Artifact specs: Improves your armor class by 9 points."); break;
				case ART_LLLLLLLLLLLLLM:
					pline("Artifact specs: free action, drain resistance and low local memory when worn."); break;
				case ART_ARABELLA_S_GREAT_BANISHER:
					pline("Artifact specs: Hmm... does this pair of gloves allow you to banish monsters if you wear it?"); break;
				case ART_NO_FUTURE_BUT_AGONY:
					pline("Artifact specs: aggravate monster and conflict when worn, autocurses, lawful."); break;
				case ART_BONUS_HOLD:
					pline("Artifact specs: conflict and sustain ability when worn, autocurses, chaotic."); break;
				case ART_GREXIT_IS_NEAR:
					pline("Artifact specs: keen memory and speeds up monster respawn when worn, autocurses, lawful."); break;
				case ART_REAL_MEN_WEAR_PSYCHOS:
					pline("Artifact specs: psi resistance, hate and farlook bug when worn, autocurses, chaotic."); break;
				case ART_AMYBSOD_S_NEW_FOOTWEAR:
					pline("Artifact specs: drain resistance and blood loss when worn."); break;
				case ART_MANUELA_S_UNKNOWN_HEELS:
					pline("Artifact specs: magic resistance, ESP, aggravate monster and conflict when worn, heavily autocurses, count as block heels, chaotic."); break;
				case ART_HADES_THE_MEANIE:
					pline("Artifact specs: aggravate monster and unbreathing when worn, newly spawned monsters are always hostile."); break;
				case ART_AMY_LOVES_AUTOCURSING_ITEM:
					pline("Artifact specs: reflection, searching and itemcursing when worn, and you probably know that they will autocurse too."); break;
				case ART_ALLYNONE:
					pline("Artifact specs: reflection, conflict, unbreathing and aggravate monster when worn."); break;
				case ART_KHOR_S_REQUIRED_IDEA:
					pline("Artifact specs: free action and auto destruct when worn, because Khor says that SLEX needs an autodestruct de vice."); break;
				case ART_ERROR_IN_PLAY_ENCHANTMENT:
					pline("Artifact specs: fire resistance, half physical damage, polymorphitis, teleportitis, regeneration and speed bug when worn."); break;
				case ART_WHOA_HOLD_ON_DUDE:
					pline("Artifact specs: magic resistance when worn."); break;
				case ART_ACHROMANTIC_RING:
					pline("Artifact specs: disintegration resistance when worn, chaotic."); break;
				case ART_GOLDENIVY_S_ENGAGEMENT_RIN:
					pline("Artifact specs: teleport control, teleportitis, aggravate monster, sickness resistance and flying when worn."); break;
				case ART_TYRANITAR_S_OWN_GAME:
					pline("Artifact specs: prism reflection when worn."); break;
				case ART_ONE_MOMENT_IN_TIME:
					pline("Artifact specs: psi, stun, petrification and sickness resistance, warp reflection and nastiness when worn."); break;
				case ART_BUEING:
					pline("Artifact specs: sight bonus, poison resistance and right mouse button loss when worn."); break;
				case ART_NAZGUL_S_REVENGE:
					pline("Artifact specs: regeneration, half physical damage, free action and manaleech when worn, heavily autocurses, disables drain resistance and prevents you from gaining experience points."); break;
				case ART_HARRY_S_BLACKTHORN_WAND:
					pline("Artifact specs: No specialties."); break;
				case ART_PROFESSOR_SNAPE_S_DILDO:
					pline("Artifact specs: No specialties."); break;
				case ART_FRENCH_MAGICAL_DEVICE:
					pline("Artifact specs: No specialties."); break;
				case ART_SAGGITTII:
					pline("Artifact specs: +8 to-hit and +6 damage, neutral."); break;
				case ART_BENTSHOT:
					pline("Artifact specs: +10 to-hit and +2 damage, neutral."); break;
				case ART_JELLYWHACK:
					pline("Artifact specs: +10 to-hit and double damage to jellies, hallucination resistance when wielded, lawful."); break;
				case ART_ONE_THROUGH_FOUR_SCEPTER:
					pline("Artifact specs: +5 to-hit and double damage to crossaligned monsters, drain and hallucination resistance, regeneration, warning, half spell damage, fast dungeon regrowth and increased difficulty when wielded, lawful. Whoa that was a long description."); break;
				case ART_AL_CANONE:
					pline("Artifact specs: +5 to-hit and +6 damage, fire resistance, warning, stealth and acts as a luckstone when wielded, chaotic."); break;
				case ART_VEIL_OF_MINISTRY:
					pline("Artifact specs: magic and drain resistance, reflection, superscroller, black ng walls and confusion when worn, heavily autocurses, neutral."); break;
				case ART_ZANKAI_HUNG_ZE_TUNG_DO_HAI:
					pline("Artifact specs: +1 to-hit and double damage, half spell damage and speed when worn, massively increases hunger and damages the wielder, neutral."); break;
				case ART_AWKWARDNESS:
					pline("Artifact specs: +1 to-hit and +2 damage to fire-susceptible monsters, fire resistance when wielded, chaotic."); break;
				case ART_SCHWANZUS_LANGUS:
					pline("Artifact specs: magic resistance, half physical and spell damage, reflection, stun, confusion, hallucination and freezing when wielded, lawful."); break;
				case ART_TRAP_DUNGEON_OF_SHAMBHALA:
					pline("Artifact specs: sets itself to +10 when worn, creates traps and causes random bad effects, neutral."); break;
				case ART_ZERO_PERCENT_FAILURE:
					pline("Artifact specs: half spell damage and improved spellcasting chances when worn."); break;
				case ART_HENRIETTA_S_HEAVY_CASTER:
					pline("Artifact specs: aggravate monster and improved spellcasting chances when worn, chaotic."); break;
				case ART_ROFLCOPTER_WEB:
					pline("Artifact specs: magic and drain resistance, warning of elves, improved spellcasting chances and half speed when worn, chaotic."); break;
				case ART_SHIVANHUNTER_S_UNUSED_PRIZ:
					pline("Artifact specs: magic resistance, reflection, displacement, blood mana and stun when worn."); break;
				case ART_ARABELLA_S_ARTIFACT_CREATI:
					pline("Artifact specs: +20 to-hit and +12 damage, energy regeneration, searching, acts as a luckstone when wielded and beheads monsters, lawful. You somehow get the suspicion that there's a terrible curse on this weapon though..."); break;
				case ART_TIARA_OF_AMNESIA:
					pline("Artifact specs: causes amnesia every once in a while, lawful."); break;
				case ART_FLUE_FLUE_FLUEFLUE_FLUE:
					pline("Artifact specs: undead warning and flying when worn, lawful."); break;
				case ART_LIXERTYPIE:
					pline("Artifact specs: +9 to-hit and +18 damage to humans, elves and dwarves, magic resistance when wielded, lawful."); break;
				case ART_SAMENESS_OF_CHRIS:
					pline("Artifact specs: +7 to-hit and +10 damage, blindness resistance and searching when wielded, lawful."); break;
				case ART_DONALD_TRUMP_S_RAGE:
					pline("Artifact specs: +5 to-hit and +2 level-drain damage, beheads humans, chaotic."); break;
				case ART_PRICK_PASS:
					pline("Artifact specs: +5 to-hit and +16 damage to elves, chaotic."); break;
				case ART_THRANDUIL_LOSSEHELIN:
					pline("Artifact specs: +5 to-hit and double level-drain damage, drain resistance and fleecy-colored glyphs when wielded, bloodthirsty, may spontaneously disintegrate, heavily autocurses, chaotic."); break;
				case ART_FEANARO_SINGOLLO:
					pline("Artifact specs: +10 to-hit and +12 damage to fire-susceptible monsters, stealth, confusion and speed when wielded, neutral."); break;
				case ART_WINSETT_S_BIG_DADDY:
					pline("Artifact specs: Multishot bonus, randomly fires 1 or 2 more missiles per turn."); break;
				case ART_FEMINIST_GIRL_S_PURPLE_WEA:
					pline("Artifact specs: magic resistance when worn, +5 charisma if you're a feminist but -5 to all stats if you're male."); break;
				case ART_LEA_S_SPOKESWOMAN_UNIFORM:
					pline("Artifact specs: It's the uniform that Lea wears when officially working as a spokeswoman. Chaotic."); break;
				case ART_HERETICAL_FIGURE:
					pline("Artifact specs: reduces your spellcasting chances if you wear it while at less than full health."); break;
				case ART_JANA_S_SECRET_CAR:
					pline("Artifact specs: speed and unbreathing when worn. You don't know if it does anything else though..."); break;
				case ART_UNIMPLEMENTED_FEATURE:
					pline("Artifact specs: confusion when worn, and potions have a chance of not working."); break;
				case ART_FLAT_INSANITY:
					pline("Artifact specs: reflection, magic resistance and inventorylessness when worn, autocurses, chaotic."); break;
				case ART_FREEZE_OR_YES:
					pline("Artifact specs: cold resistance when worn, putting them on for the first time grants intrinsic freezopathy."); break;
				case ART_PRINCESS_BITCH:
					pline("Artifact specs: sitting on a throne while wearing them always gives the princess bitch effect."); break;
				case ART_WOULD_YOU_RAIGHT_THAT:
					pline("Artifact specs: wearing them while not having the searching skill will unlock it and cap it at expert, but also prime curse the gloves."); break;
				case ART_DIFFICULTY__:
					pline("Artifact specs: shock resistance and flying when worn, but you will see more monsters and traps, and the monster difficulty will be increased."); break;
				case ART_SWARM_SOFT_HIGH_HEELS:
					pline("Artifact specs: They're high-heeled and incredibly soft! The block heels in particular are sooooo kind and gentle! <3"); break;
				case ART_WEAK_FROM_HUNGER:
					pline("Artifact specs: conflict and weakness effect when worn, chaotic."); break;
				case ART_ARABELLA_S_RESIST_COLD:
					pline("Artifact specs: It should give cold resistance while worn, right?"); break;
				case ART_RATSCH_WATSCH:
					pline("Artifact specs: putting it on will disable your teleport control for 1 million turns. Who knows if you would survive long enough for that to time out..."); break;
				case ART_ARABELLA_S_PRECIOUS_GADGET:
					pline("Artifact specs: Wow! It must be worth a fortune!"); break;
				case ART_ARABELLA_S_WARDING_HOE:
					pline("Artifact specs: +5 to-hit and +10 stun damage, reflection and protection when wielded. That's all that you can make out."); break;
				case ART_SHAPETAKE_NUMBER_FIVE:
					pline("Artifact specs: disables polymorph control and gives polymorphitis when worn, and prevents your polymorphs from timing out."); break;
				case ART_ARABELLA_S_WAND_BOOSTER:
					pline("Artifact specs: You're not sure how it would boost wands, but it definitely grants magic resistance when worn."); break;
				case ART_INTELLIGENT_POPE:
					pline("Artifact specs: if you trigger a trap while wielding it, its enchantment will go up or down. If it's blessed, positive enchantment is more likely; cursed, and negative enchantment is more likely."); break;
				case ART_RNG_S_PRIDE:
					pline("Artifact specs: Putting it on while it's +0 will set its enchantment to a random value."); break;
				case ART_RNG_S_JOY:
					pline("Artifact specs: Putting it on while it's +0 will set its enchantment to a random value."); break;
				case ART_RNG_S_SEXINESS:
					pline("Artifact specs: Putting it on while it's +0 will set its enchantment to a random value."); break;
				case ART_RNG_S_EMBRACE:
					pline("Artifact specs: Putting it on while it's +0 will set its enchantment to a random value."); break;
				case ART_RNG_S_GRIMACE:
					pline("Artifact specs: Putting it on while it's +0 will set its enchantment to a random value."); break;
				case ART_COMPLETE_MON_DIEU:
					pline("Artifact specs: No specialties."); break;
				case ART_AGATHE_BAUER:
					pline("Artifact specs: No specialties."); break;
				case ART_ANNELIESE_BROWN:
					pline("Artifact specs: No specialties."); break;
				case ART_I_WILL_THINK_ABOUT_YOU:
					pline("Artifact specs: No specialties."); break;
				case ART_DEL_OLELONG:
					pline("Artifact specs: No specialties."); break;
				case ART_JUBELJUBIJEEAH:
					pline("Artifact specs: No specialties."); break;
				case ART_DUEDELDUEDELDUEDELDUEDELDU:
					pline("Artifact specs: No specialties."); break;
				case ART_CAUSE_I_M_A_CHEATER:
					pline("Artifact specs: No specialties."); break;
				case ART_BATMAN_NIGHT:
					pline("Artifact specs: No specialties."); break;
				case ART_NIKKENIKKENIK:
					pline("Artifact specs: No specialties."); break;
				case ART_JANA_S_GRAVE_WALL:
					pline("Artifact specs: speed and unbreathing when worn. You do not know if it does anything else though..."); break;
				case ART_HENRIETTA_S_DOGSHIT_BOOTS:
					pline("Artifact specs: The former owner stepped into a huge pile of dog shit with them. So if you put them on, you will aggravate monsters and your stealth is disabled, monsters will always spawn hostile and always know where you are, and of course they autocurse as well. Chaotic."); break;
				case ART_FIREPROOF_WALL:
					pline("Artifact specs: +4 to-hit and +4 damage to fire-susceptible monsters, fire resistance when wielded, lawful."); break;
				case ART_SPEARBLADE:
					pline("Artifact specs: +9 to-hit and +2 damage, lawful."); break;
				case ART_RADIATOR_AREA:
					pline("Artifact specs: +5 to-hit and double damage to undead, blindness resistance when wielded, lawful."); break;
				case ART_JESSICA_S_WINNING_STRIKE:
					pline("Artifact specs: +8 damage, lawful."); break;
				case ART_MARKUS_S_JUSTICE:
					pline("Artifact specs: +5 to-hit and +12 damage to crossaligned monsters, lawful."); break;
				case ART_KATHARINA_S_MELEE_PROWESS:
					pline("Artifact specs: +3 to-hit and +10 damage, lawful."); break;
				case ART_LICHBANE:
					pline("Artifact specs: +5 to-hit and double damage to liches."); break;
				case ART_FORKED_TONGUE:
					pline("Artifact specs: +2 to-hit and +6 damage, chaotic."); break;
				case ART_ALL_SERIOUSNESS:
					pline("Artifact specs: +3 to-hit and +6 damage, neutral."); break;
				case ART_SPECIAL_LACK:
					pline("Artifact specs: +10 damage, chaotic."); break;
				case ART_WING_WING:
					pline("Artifact specs: +2 to-hit and +4 damage to fire-susceptible monsters."); break;
				case ART_CARMOUFALSCH:
					pline("Artifact specs: No specialties."); break;
				case ART_WIE_ES_AUCH_SEI:
					pline("Artifact specs: No specialties."); break;
				case ART_MORTON_THEIRS_OF_RAVEL_RAD:
					pline("Artifact specs: No specialties."); break;
				case ART_DEEP_FRIENDS:
					pline("Artifact specs: No specialties."); break;
				case ART_HAE_HAE_HIIII:
					pline("Artifact specs: No specialties."); break;
				case ART_FOR_MOMMY_EVER_FORSELESSAU:
					pline("Artifact specs: No specialties."); break;
				case ART_LAWFIRE:
					pline("Artifact specs: No specialties."); break;
				case ART_WAE_WAE_WAE_DAEDELDAEDELDA:
					pline("Artifact specs: No specialties."); break;
				case ART_PRESIDENT_SCHIESSKANISTA:
					pline("Artifact specs: No specialties."); break;
				case ART_KNBLOELOELOELODRIO:
					pline("Artifact specs: No specialties."); break;
				case ART_DESERT_MAID:
					pline("Artifact specs: +20 damage."); break;
				case ART_CYGNISWAN:
					pline("Artifact specs: hallucination resistance when wielded."); break;
				case ART_TALKATOR:
					pline("Artifact specs: +2 to-hit and +6 damage."); break;
				case ART_MAGESOOZE:
					pline("Artifact specs: energy regeneration when wielded."); break;
				case ART_RESISTANT_RESISTOR:
					pline("Artifact specs: half spell damage and half physical damage when wielded."); break;
				case ART_VERNON_S_POTTERBASHER:
					pline("Artifact specs: +4 to-hit and +16 damage, neutral."); break;
				case ART_SCHWOINGELOINGELOING_OOOAR:
					pline("Artifact specs: +24 to-hit and +2 damage."); break;
				case ART_FEMMY_S_LASH:
					pline("Artifact specs: +5 to-hit and +24 damage to male monsters, warns of male monsters and changes your character's name to 'ThatFeministGirl' :-) Just kidding!"); break;
				case ART_CASQUE_OUTLOOK:
					pline("Artifact specs: +2 to-hit and +10 damage, teleport control when wielded."); break;
				case ART_UNFAIR_PEE:
					pline("Artifact specs: +2 to-hit and +12 damage, warning and acid resistance when wielded."); break;
				case ART_SEA_CAPTAIN_PIERCER:
					pline("Artifact specs: +2 to-hit and +10 damage, acts as a luckstone when wielded."); break;
				case ART_END_OF_LOOK_WORSE:
					pline("Artifact specs: +2 to-hit and +8 damage."); break;
				case ART_SPACE_BEGINS_AFTER_HERE:
					pline("Artifact specs: +2 to-hit and +12 damage."); break;
				case ART_CORINA_S_THUNDER:
					pline("Artifact specs: +2 to-hit and +12 damage to shock-susceptible monsters."); break;
				case ART_INNER_TUBE:
					pline("Artifact specs: reflection when wielded."); break;
				case ART_SOLO_SLACKER:
					pline("Artifact specs: +1 to-hit and +2 damage."); break;
				case ART_AMMO_OF_THE_MACHINE:
					pline("Artifact specs: +10 to-hit and +40 damage to golems."); break;
				case ART_DAE_OE_OE_OE_OE_OE:
					pline("Artifact specs: +14 damage."); break;
				case ART_CANNONEER:
					pline("Artifact specs: +2 to-hit and +10 damage."); break;
				case ART_SPEEDHACK:
					pline("Artifact specs: very fast speed when wielded."); break;
				case ART_EARTH_GAS_GUN:
					pline("Artifact specs: +10 damage, reflection when wielded."); break;
				case ART_FIRE_ALREADY:
					pline("Artifact specs: +2 to-hit and +10 damage."); break;
				case ART_HUMAN_WIPEOUT:
					pline("Artifact specs: can behead humans."); break;
				case ART_SPLINTER_ARMAMENT:
					pline("Artifact specs: magic resistance, reflection and superscrolling when worn."); break;
				case ART_ABSOLUTE_MONSTER_MAIL:
					pline("Artifact specs: It looks like a normal suit of armor."); break;
				case ART_RITA_S_TENDER_STILETTOS:
					pline("Artifact specs: It's an incredibly sweeeeeeeeeeet pair of female high heels! Chaotic."); break;
				case ART_HALF_MOON_TONIGHT:
					pline("Artifact specs: acts as a luckstone when worn."); break;
				case ART_PANTAP:
					pline("Artifact specs: ESP and warning when worn."); break;
				case ART_RUTH_S_DARK_FORCE:
					pline("Artifact specs: +5 to-hit and causes darkness every once in a while when worn, autocurses."); break;
				case ART_HAMBURG_ONE:
					pline("Artifact specs: hunger and hallucination resistance when worn."); break;
				case ART_ARABELLA_S_MELEE_POWER:
					pline("Artifact specs: +10 to-hit and +20 damage to humans, neutral. You doubt that's all it does though..."); break;
				case ART_ASBESTOS_MATERIAL:
					pline("Artifact specs: always poisoned."); break;
				case ART_TANKS_A_LOT:
					pline("Artifact specs: regeneration, energy regeneration, 5 extra points of AC and slippery hands when worn."); break;
				case ART_DIMVISION:
					pline("Artifact specs: magic resistance when worn, putting them on allows you to learn the eddy wind technique at the cost of permanent weak sight. However, you also get weak sight if you already know eddy wind!"); break;
				case ART_I_M_GETTING_HUNGRY:
					pline("Artifact specs: free action and greatly increased chance to block when worn, chaotic."); break;
				case ART_CCC_CCC_CCCCCCC:
					pline("Artifact specs: confusion when worn, autocurses."); break;
				case ART_FIVE_STAR_PARTY:
					pline("Artifact specs: uninformation, resist confusion and stun, autocurses, lawful."); break;
				case ART_GUDRUN_S_STOMPING:
					pline("Artifact specs: cold resistance when worn."); break;
				case ART_GOEFFELBOEFFEL:
					pline("Artifact specs: regeneration, ESP and half speed when worn."); break;
				case ART_TEMPERATOR:
					pline("Artifact specs: cold and fire resistance when worn."); break;
				case ART_GREEN_COLOR:
					pline("Artifact specs: poison resistance when worn."); break;
				case ART_SCARAB_OF_ADORNMENT:
					pline("Artifact specs: +10 charisma when worn."); break;
				case ART_SCHWUEU:
					pline("Artifact specs: teleport control, ESP and teleporting items when worn."); break;
				case ART_FULLY_THE_LONG_PENIS:
					pline("Artifact specs: No specialties."); break;
				case ART_WORLD_OF_COQ:
					pline("Artifact specs: No specialties."); break;
				case ART_WHOOSHZAP:
					pline("Artifact specs: No specialties."); break;
				case ART_NADJA_S_SILENCE:
					pline("Artifact specs: +5 to-hit and +6 damage, stealth when wielded, chaotic."); break;
				case ART_A_SWORD_NAMED_SWORD:
					pline("Artifact specs: +8 to-hit and +6 damage, neutral."); break;
				case ART_HERITAGE_IGNORER:
					pline("Artifact specs: +4 to-hit and +4 damage, neutral."); break;
				case ART_MIMICBANE:
					pline("Artifact specs: +10 to-hit and double damage to mimics, hallucination resistance when wielded, lawful."); break;
				case ART_HAHAHAHAHAHAHAAAAAAAAAAAA:
					pline("Artifact specs: +3 to-hit and +6 damage to cold-susceptible monsters, neutral."); break;
				case ART_POISON_PEN_LETTER:
					pline("Artifact specs: +9 to-hit and +10 level-drain damage, regeneration and half physical damage when wielded, poisons you each turn you wield it, neutral."); break;
				case ART_SUNALI_S_SUMMONING_STORM:
					pline("Artifact specs: improves your spellcasting chances when worn."); break;
				case ART_FILTHY_PRESS:
					pline("Artifact specs: +5 to-hit and +10 damage, searching and resistance to hallucination and level drain when wielded, replaces messages with random ones, lawful."); break;
				case ART_MUB_PUH_MUB_DIT_DIT:
					pline("Artifact specs: +7 to-hit and +10 damage, blindness resistance and searching when wielded, lawful."); break;
				case ART_DONNNNNNNNNNNNG:
					pline("Artifact specs: +40 damage, using it in melee has a considerable chance of reducing its enchantment."); break;
				case ART_PROVOCATEUR:
					pline("Artifact specs: conflict when wielded, chaotic."); break;
				case ART_FOEOEOEOEOEOEOE:
					pline("Artifact specs: increases multishot by up to 3, but also causes your projectiles to misfire occasionally."); break;
				case ART_NEVER_WILL_THIS_BE_USEFUL:
					pline("Artifact specs: increases your damage by 4 points per rank in your trident skill, lawful."); break;
				case ART_QUARRY:
					pline("Artifact specs: 5 extra points of AC, increases the chances of ammos made of mineral to avoid breakage."); break;
				case ART_CONNY_S_COMBAT_COAT:
					pline("Artifact specs: while wearing it, your kicks do 5 extra points of damage and can occasionally stun and paralyze the target, heavily autocurses, chaotic."); break;
				case ART_ACIDSHOCK_CASTLECRUSHER:
					pline("Artifact specs: shock and acid resistance and 5 extra points of AC when worn"); break;
				case ART_LAURA_S_SWIMSUIT:
					pline("Artifact specs: swimming and unbreathing when worn, prevents eels and similar monsters from wrapping you, but reduces your AC by 5 points."); break;
				case ART_PROTECT_WHAT_CANNOT_BE_PRO:
					pline("Artifact specs: while wearing it, you may occasionally be able to erodeproof an object, but the object in question must be made of a non-erodable material, which greatly limits its usefulness."); break;
				case ART_GIRLFUL_FARTING_NOISES:
					pline("Artifact specs: Attracts farting monsters when worn."); break;
				case ART_YOU_SEE_HERE_AN_ARTIFACT:
					pline("Artifact specs: bigscript when worn."); break;
				case ART_NUMB_OR_MAYBE:
					pline("Artifact specs: shock resistance when worn, putting them on may give intrinsic numbopathy but also has a chance of doing bad stuff instead."); break;
				case ART_DEAD_SLAM_THE_TIME_SHUT:
					pline("Artifact specs: wearing them while not having the device skill will unlock it and cap it at expert, but also prime curse the gloves."); break;
				case ART_ANASTASIA_S_UNEXPECTED_ABI:
					pline("Artifact specs: It's a pair of high heels that may do something very unexpected if you put them on. Did you know that Anastasia is capable of walking in cone heels?"); break;
				case ART_ELIANE_S_SHIN_SMASH:
					pline("Artifact specs: When AmyBSOD was little, she called them the 'most beautiful shoes in the world'. Anyway, if you kick a monster with them, it will do double damage and paralyze the monster. Also, you aren't affected by heaps of shit while wearing them and cannot have wounded legs. However, they will be vaporized instantly if they ever come into contact with water, and also if something farts. Lawful."); break;
				case ART_MYSTERIOUS_MAGIC:
					pline("Artifact specs: energy regeneration and weakened magic effects when worn."); break;
				case ART_BANGCOCK:
					pline("Artifact specs: if you trigger a trap while wielding it, its enchantment will go up or down; if it's blessed, positive enchantments are more likely, and if it's cursed, negative enchantments are more likely."); break;
				case ART_RNG_S_COMPLETION:
					pline("Artifact specs: Putting it on while it's +0 will set its enchantment to a random value."); break;
				case ART_RNG_S_BEAUTY:
					pline("Artifact specs: Putting it on while it's +0 will set its enchantment to a random value."); break;
				case ART_RNG_S_SAFEGUARD:
					pline("Artifact specs: Putting it on while it's +0 will set its enchantment to a random value."); break;
				case ART_BLACK_DARKNESS:
					pline("Artifact specs: every glyph is black while you wear it, neutral."); break;
				case ART_FLEECY_GREEN:
					pline("Artifact specs: every glyph is green while you wear it, neutral."); break;
				case ART_PEEK:
					pline("Artifact specs: +5 to-hit and +16 damage to elves, chaotic."); break;
				case ART_TAILCUTTER:
					pline("Artifact specs: +5 to-hit and double damage to worm tails, neutral."); break;

				case ART_PHANTOM_OF_THE_OPERA:
					pline("Artifact specs: 5 extra points of AC."); break;
				case ART_HIGH_DESIRE_OF_FATALITY:
					pline("Artifact specs: very fast speed when worn."); break;
				case ART_CHOICE_OF_MATTER:
					pline("Artifact specs: cold resistance when worn."); break;
				case ART_MELISSA_S_BEAUTY:
					pline("Artifact specs: +10 charisma, +5 AC and +5 to-hit when worn, chaotic."); break;
				case ART_CORINA_S_SNOWY_TREAD:
					pline("Artifact specs: cold resistance, aggravate monster and prevents your potions from shattering due to cold attacks when worn. Also improves the chance of the disarm technique working, and they speed up when walking on snow."); break;
				case ART_NUMBER___:
					pline("Artifact specs: psi resistance when worn."); break;
				case ART_HAUNTNIGHT:
					pline("Artifact specs: monsters are always spawned permanently invisible while you wear this."); break;
				case ART_LORSKEL_S_SPECIAL_PROTECTI:
					pline("Artifact specs: Greatly reduces the risk of getting your stuff stolen by monsters."); break;
				case ART_ROBBERY_GONE_RIGHT:
					pline("Artifact specs: randomly spawned gold has three times the normal amount, chaotic."); break;
				case ART_JOSEFINE_S_EVILNESS:
					pline("Artifact specs: fire, cold, shock and psi resistance when worn and also cause random fainting, lawful."); break;
				case ART_WHINY_MARY:
					pline("Artifact specs: ranged attacks done by you will fire up to 5 extra projectiles per turn while wearing them, but the weapon in your hand will automatically curse itself each turn, lawful."); break;
				case ART_WARP_SPEED:
					pline("Artifact specs: increases your speed by 60 while you're on a water square."); break;
				case ART_GRENEUVENIA_S_HUG:
					pline("Artifact specs: flying, fire resistance and sight bonus when worn and also spouts torrents of nasty messages to drive you nuts. Neutral."); break;
				case ART_SHELLY:
					pline("Artifact specs: magic resistance when worn, prevents spellcasting (both yours and monsters') 2 out of 3 times, chaotic."); break;
				case ART_SPREAD_YOUR_LEGS_WIDE:
					pline("Artifact specs: nakedness effect, autocurses."); break;
				case ART_GREEB:
					pline("Artifact specs: Attracts green monsters every once in a while."); break;
				case ART_PRINCE_OF_PERSIA:
					pline("Artifact specs: jumping and 50%% chance of life saving when worn, lawful."); break;
				case ART_ANASTASIA_S_PLAYFULNESS:
					pline("Artifact specs: acid resistance when worn, and they loooooooooove to step into dog shit because you certainly want to clean them again and again. :-)"); break;
				case ART_KATIE_MELUA_S_FEMALE_WEAPO:
					pline("Artifact specs: cold resistance when worn, count as high heels because the white stilettos are just so lovely and tender. And they're also so sharp-edged that it should be illegal to own them without a weapons license."); break;
				case ART_COCKUETRY:
					pline("Artifact specs: petrification resistance when worn."); break;
				case ART_PERCENTIOEOEPSPERCENTD_THI:
					pline("Artifact specs: It might have something to do with thieves."); break;
				case ART_PEEPING_GROOVE:
					pline("Artifact specs: if you use a shotgun while wearing them, you can fire up to 7 extra shots per turn. Chaotic."); break;
				case ART_RARE_ASIAN_LADY:
					pline("Artifact specs: cold resistance, reflection, +20 charisma and you can always resist foocubi's attempts to undress you while wearing them."); break;
				case ART_JANA_S_FAIRNESS_CUP:
					pline("Artifact specs: speed, stealth, flying and magic resistance when worn. Chaotic. You do not know if it does anything else though..."); break;
				case ART_OUT_OF_TIME:
					pline("Artifact specs: +5 strength and dexterity and turn limitation when worn."); break;
				case ART_PALEOLITHIC_ELBOW_CONTRACT:
					pline("Artifact specs: +5 multishot with bows, all your skills count as 'unskilled' while wearing it, lawful."); break;
				case ART_NUCLEAR_BOMB:
					pline("Artifact specs: fire resistance and golems always spawn with the bomber and exploder egotypes when worn, chaotic."); break;
				case ART_BEEEEEEEANPOLE:
					pline("Artifact specs: Improves the range of arrows that you fire from a bow by 5 squares while worn."); break;
				case ART_LEGMA:
					pline("Artifact specs: magic resistance when worn, lawful."); break;
				case ART_TERRY_PRATCHETT_S_INGENUIT:
					pline("Artifact specs: every time you reflect a beam, it will be reflected in a 90 degree angle. Take note that this robe does not actually grant extrinsic reflection though."); break;
				case ART_ARABELLA_S_SEXY_GIRL_BUTT:
					pline("Artifact specs: It makes you want to feel up the sexy butt cheeks of an asian girl with your soft, fleecy hands. Chaotic."); break;
				case ART_LONG_LASTING_JOY:
					pline("Artifact specs: Allows you to enjoy your polymorphs for a longer time before they time out."); break;
				case ART_LIGHT_ABSORPTION:
					pline("Artifact specs: Can be invoked to light up areas."); break;
				case ART_CATHERINE_S_SEXUALITY:
					pline("Artifact specs: half physical damage, half spell damage and reflection when worn, but if you ever give birth to children, you'll die instantly. Lawful."); break;
				case ART_POKEWALKER:
					pline("Artifact specs: Displays all pokemon on the current dungeon level when worn."); break;
				case ART_WINDS_OF_CHANGE:
					pline("Artifact specs: confusing problem when worn and occasionally increases your movement speed."); break;
				case ART_LIGHTSPEED_TRAVEL:
					pline("Artifact specs: Completely prevents you from being interrupted, which means that doing multi-turn actions becomes extremely dangerous if there are monsters around. It also grants blinking speed, which is even faster than 'very fast' speed."); break;
				case ART_T_O_M_E:
					pline("Artifact specs: entering a new dungeon level while wearing it will give you either a fumblefingers quest or a princess bitch quest. By the way, you should give ToME-SX a try - it's an Angband variant made by Amy! :-)"); break;
				case ART_FEMMY_FATALE:
					pline("Artifact specs: weakness problem, diarrhea, slow digestion and monsters do not leave corpses while worn, lawful."); break;
				case ART_ARTIFICIAL_FAKE_DIFFICULTY:
					pline("Artifact specs: halves experience points gained, your techniques become re-usable twice as quickly, but many standard actions like attacking monsters or casting spells will have a failure rate, making the game much harder."); break;
				case ART_JUNETHACK______WINNER:
					pline("Artifact specs: if (trophy_get == TRUE) increase_player_stats :-) Seriously, getting a trophy while wearing it improves your maximum HP and Pw. Now go ahead and win the Junethack tournament, we're all counting on you!"); break;
				case ART_YOG_SOTHOTH_HELP_ME:
					pline("Artifact specs: psi resistance when worn. By the way, Adeon really loves to update Pinobot for the new SLEX monsters because Yog-Sothoth actually is one of them! :-)"); break;
				case ART_WHISTLE_OF_PROTECTION:
					pline("Artifact specs: energy regeneration and magic resistance while carried, can be invoked for energy boost, chaotic, occult master quest artifact."); break;
				case ART_BLADE_OF_GOTHMOG:
					pline("Artifact specs: +13 to-hit and +14 damage to fire-susceptible monsters, fire resistance while wielded, heavily autocurses, can be invoked to summon a fire elemental, chaotic, chaos sorceror quest artifact."); break;
				case ART_BEAM_MULTIPLIER:
					pline("Artifact specs: fire, cold and shock resistance when worn, increases the average range of all beams, neutral, elementalist quest artifact."); break;
				case ART_ELLI_S_PSEUDOBAND_OF_POS:
					pline("Artifact specs: reflection when wielded, +8 to-hit and +8 drain life damage, chaotic, wild talent quest artifact."); break;
				case ART_HIGHEST_FEELING:
					pline("Artifact specs: teleport control, half physical damage and fire resistance when worn, 50%% chance of extra speed, neutral, prostitute quest artifact."); break;
				case ART_LORSKEL_S_INTEGRITY:
					pline("Artifact specs: reflection and magic resistance when worn, spawns fart traps every once in a while, chaotic, kurwa quest artifact. It also retains the lolita boots effect of monsters wanting to have sex with you, even if the base item type is changed to something else."); break;

				case ART_SOFTNESS_OF_TELEPORTATION:
					pline("Artifact specs: no specialties."); break;
				case ART_JOY_RIDE:
					pline("Artifact specs: no specialties."); break;
				case ART_FISSILITY:
					pline("Artifact specs: much higher chance of breaking when used."); break;
				case ART_BLOCKING_EXTREME:
					pline("Artifact specs: 10%% higher chance to block projectiles."); break;
				case ART_EWSCRATCH:
					pline("Artifact specs: +2 to-hit and +8 damage."); break;
				case ART_TARMAC_CHAMPION:
					pline("Artifact specs: very fast speed when wielded, lawful."); break;
				case ART_GAUGE_O_METER:
					pline("Artifact specs: +2 to-hit and +4 stun damage, neutral."); break;
				case ART_U_ARE_A_CHEATER:
					pline("Artifact specs: magic resistance when wielded, chaotic."); break;
				case ART_COOL_CHAMBER:
					pline("Artifact specs: +4 to-hit and +8 damage to cold-susceptible monsters."); break;
				case ART_ANIMALBANE:
					pline("Artifact specs: +5 to-hit and double damage to animals, warns of animals when wielded."); break;
				case ART_ALWAYS_HIT_FOR_LITTLE_DAMA:
					pline("Artifact specs: +10 to-hit and +2 damage."); break;
				case ART_GRINDER:
					pline("Artifact specs: +4 to-hit and +12 damage."); break;
				case ART_OUCHFIRE:
					pline("Artifact specs: +1 to-hit and +16 damage to fire-susceptible monsters, fire resistance when wielded."); break;
				case ART_ANTIVAMP_WHOOSH:
					pline("Artifact specs: +8 to-hit and +20 damage to vampires, neutral."); break;
				case ART_RESISTOMATIC:
					pline("Artifact specs: shock and acid resistance when wielded."); break;
				case ART_GOODIE_OF_USE:
					pline("Artifact specs: +12 damage."); break;
				case ART_ARRRRRR_MATEY:
					pline("Artifact specs: +4 damage, korsair speak when wielded."); break;
				case ART_SPAMBAIT_FIRE:
					pline("Artifact specs: +8 damage to fire-susceptible monsters, and a general +2 increase damage when wielded."); break;
				case ART_ARABSTREET_SOUND:
					pline("Artifact specs: +6 to-hit and +2 damage to fire-susceptible monsters, 1 out of 50 monsters is spawned with the sounder egotype while you wield it."); break;
				case ART_HALLUDUCKDIR:
					pline("Artifact specs: +5 to-hit and +10 damage, hallucination resistance when wielded."); break;
				case ART_WHACKDOCK:
					pline("Artifact specs: +2 to-hit and +4 damage."); break;
				case ART_JONADAB_S_IDEA_GENERATOR:
					pline("Artifact specs: +2 damage."); break;
				case ART_RATTLECLINKER:
					pline("Artifact specs: +6 to-hit and +2 damage."); break;
				case ART_SEE_ANIMALS:
					pline("Artifact specs: warning of animals when wielded."); break;
				case ART_WILD_HEAVY_SWINGS:
					pline("Artifact specs: double damage, reduces your general accuracy by 10 when wielded."); break;
				case ART_ORANGERY:
					pline("Artifact specs: while wielding it, orange monsters are almost always spawned peaceful and sometimes tame."); break;
				case ART_MELISSA_S_PEACEBRINGER:
					pline("Artifact specs: magic resistance when wielded. If you don't dual-wield, wielding it allows you to attack twice per turn. Chaotic."); break;
				case ART_MANUELA_S_PRACTICANT_TERRO:
					pline("Artifact specs: +5 to-hit and double damage, fire resistance when wielded, autocurses, aggravate monster when wielded, all monsters are spawned hostile and their levels are higher if more of their species have been generated already and you can walk in lava unharmed, chaotic."); break;
				case ART_THOR_S_STRIKE:
					pline("Artifact specs: +6 to-hit and +12 damage to shock-susceptible monsters, and if you wield it while having a strength of 25, you get +5 increase damage. Lawful."); break;
				case ART_BLACK_POISON_INSIDE:
					pline("Artifact specs: beheads monsters, always poisoned, chaotic."); break;
				case ART_LUISA_S_CHARMING_BEAUTY:
					pline("Artifact specs: +5 to-hit and double damage, autocurses, occasionally spawns shit traps and you trigger them even if you fly. Wielding it causes diarrhea and thirst and prevents you from being kicked in the nuts or ripped into your breasts. Neutral."); break;
				case ART_AMY_S_FIRST_GIRLFRIEND:
					pline("Artifact specs: +5 to-hit and +4 stun damage, cold resistance and ESP when wielded, autocurses and bonks you every once in a while. Lawful. *sigh* She was such a sweet, wing-tufted girl..."); break;
				case ART_PATRICIA_S_FEMININITY:
					pline("Artifact specs: wielding it makes you thick-skinned. It is bloodthirsty, and less likely than ordinary steel-capped sandals to lose enchantment if you hit something with it. Neutral."); break;
				case ART_HENRIETTA_S_MISTAKE:
					pline("Artifact specs: +2 to-hit and +16 damage to acid-susceptible monsters, but if you move around, you will constantly step into heaps of shit. Heavily autocurses when wielded and also gives aggravate monster and disables stealth. Chaotic."); break;
				case ART_TEACHING_STICK:
					pline("Artifact specs: +4 to-hit and +8 damage, drain resistance when wielded."); break;
				case ART_WETTING_WEATHER:
					pline("Artifact specs: cold resistance when wielded."); break;
				case ART_MANA_METER_BOOSTER:
					pline("Artifact specs: Slightly reduces the cost of casting spells when wielded."); break;
				case ART_GRANDPA:
					pline("Artifact specs: +5 to-hit and double damage."); break;
				case ART_ROAD_TRASH:
					pline("Artifact specs: +2 to-hit and +14 damage to acid-susceptible monsters, chaotic."); break;
				case ART_ALSO_MATTE_MASK:
					pline("Artifact specs: poison resistance when wielded, always poisoned."); break;
				case ART_CRUSHING_IMPACT:
					pline("Artifact specs: If you don't dual-wield, wielding it allows you to attack twice per turn."); break;
				case ART_WHY_DO_YOU_HAVE_SUCH_A_LIT:
					pline("Artifact specs: +4 to-hit and +12 damage, aggravate monster when wielded."); break;
				case ART_LONGEST_STICK:
					pline("Artifact specs: +20 damage."); break;
				case ART_HEALHEALHEALHEAL:
					pline("Artifact specs: Every time you apply it successfully at a monster, you will heal one hit point. Wielding it also doubles the effectivity of healing potions and spells, or quadruples if you're a healer."); break;
				case ART_TYPE_OF_ARMS_DISCOVERY:
					pline("Artifact specs: +10 damage."); break;
				case ART_EXTREMELY_HARD_MODE:
					pline("Artifact specs: +10 to-hit and +6 damage, magic resistance when wielded, bosses spawn more often, uncommon monsters are no longer uncommon and high-level ones aren't either."); break;
				case ART_EQUALHIT:
					pline("Artifact specs: +15 to-hit and +2 damage, neutral."); break;
				case ART_SOFTSPIRE:
					pline("Artifact specs: +6 to-hit and +12 damage to shock-susceptible monsters."); break;
				case ART_EXPLOSION_MISSILE:
					pline("Artifact specs: +8 to-hit and +2 damage to fire-susceptible monsters."); break;
				case ART_AND_IT_KEEPS_ON_MOVING:
					pline("Artifact specs: autocurses, you are constantly pushed around while wielding it."); break;
				case ART_HIGH_ROLLER_S_LUCK:
					pline("Artifact specs: applying it successfully can sometimes greatly increase its enchantment, but also has a greater chance of reducing the enchantment. Chaotic."); break;
				case ART_LONG_ROD_OF_EST:
					pline("Artifact specs: +16 damage."); break;
				case ART_MINOLONG_ELBOW:
					pline("Artifact specs: beam wands and spells have a longer range while you're wielding it."); break;
				case ART_ALCHEMICAL_PROHIBITION:
					pline("Artifact specs: +4 to-hit and +8 damage, fire resistance while carried."); break;
				case ART_AUNTIE_HILDA:
					pline("Artifact specs: magic resistance when wielded, neutral."); break;
				case ART_RED_GAS_BULLET:
					pline("Artifact specs: +10 to-hit and double damage, chaotic."); break;
				case ART_FLYSTING:
					pline("Artifact specs: +5 to-hit and +2 damage."); break;
				case ART_STREAMSHOOTER:
					pline("Artifact specs: multishot bonus, and an increased chance to fire more than 3 shots per turn."); break;
				case ART_LUCK_VERSUS_BAD:
					pline("Artifact specs: grants a significant chance of avoiding bad effects while wielded, lawful."); break;
				case ART_ELOPLUS_STAT:
					pline("Artifact specs: +1 to-hit and +2 damage, 1 extra point of AC while wielded."); break;
				case ART_IMMOBILASER:
					pline("Artifact specs: +5 to-hit and +2 stun damage, free action when wielded."); break;
				case ART_SNIPER_CROSSHAIR:
					pline("Artifact specs: bolts fired from it have their range increased by 30 squares."); break;
				case ART_ATOMIC_MISSING:
					pline("Artifact specs: -20 to-hit and +40 damage, autocurses when wielded."); break;
				case ART_LEATHER_SOFT_STING:
					pline("Artifact specs: +6 to-hit and +4 damage."); break;
				case ART_SUPER_EFFECTIVE_ROCK:
					pline("Artifact specs: +16 damage."); break;
				case ART_ANTI_INTELLIGENCE:
					pline("Artifact specs: +10 to-hit and double damage to humanoids and animals, lawful."); break;
				case ART_COMPLETELY_OFF:
					pline("Artifact specs: double damage, always misfires."); break;
				case ART_OW_WOW_WOW:
					pline("Artifact specs: +5 to-hit and +10 damage."); break;
				case ART_RACER_PROJECTILE:
					pline("Artifact specs: can be thrown at double the usual range."); break;
				case ART_INDIGENOUS_FUN:
					pline("Artifact specs: +5 to-hit and +20 damage to humanoids, warns of humanoids when wielded, lawful."); break;
				case ART_VEST_REPLACEMENT:
					pline("Artifact specs: +5 AC when wielded, and if you also wear a shield, another +5 AC and 10%% increased chance to block."); break;
				case ART_WHEREABOUT_OF_X:
					pline("Artifact specs: +2 to-hit and +4 damage, invisibility when wielded."); break;
				case ART_PRISMATIC_SHIRT:
					pline("Artifact specs: fire, cold, sleep and poison resistance when worn, lawful."); break;
				case ART_NON_BLADETURNER:
					pline("Artifact specs: reflection and aggravate monster when worn."); break;
				case ART_ARMOR_OF_ISILDUR:
					pline("Artifact specs: shock resistance when worn, putting it on while it's +0 or lower will set its enchantment to a random positive value, lawful."); break;
				case ART_ESSENTIALITY_EXTREME:
					pline("Artifact specs: free action when worn."); break;
				case ART_NONEXISTANCE:
					pline("Artifact specs: no specialties."); break;
				case ART_FORMULA_ONE_SUIT:
					pline("Artifact specs: Wearing it makes you move a little bit faster."); break;
				case ART_INCREDIBLY_FREQUENT_CAGE:
					pline("Artifact specs: fire resistance and half physical damage when worn."); break;
				case ART_MITHRAL_CANCELLATION:
					pline("Artifact specs: +1 magic cancellation and recurring disenchantment when worn, lawful."); break;
				case ART_IMPRACTICAL_COMBAT_WEAR:
					pline("Artifact specs: +5 charisma and +1 magic cancellation when worn, reduces your armor class by half."); break;
				case ART_BLUEFORM:
					pline("Artifact specs: 2 extra points of AC when worn."); break;
				case ART_LIGHT_OF_DECEPTION:
					pline("Artifact specs: 1 out of 10 monsters are spawned peaceful. Something seems to be wrong with that, though..."); break;
				case ART_ENEMIES_SHALL_LAUGH_TOO:
					pline("Artifact specs: +10 increase accuracy when worn, chaotic."); break;
				case ART_ARMOR_CLASS_WALL:
					pline("Artifact specs: improves your AC by 5 points, and even more if your shield skill is high enough."); break;
				case ART_SOMEPROTECTOR:
					pline("Artifact specs: half physical damage and half spell damage when worn."); break;
				case ART_CUTTING_THROUGH:
					pline("Artifact specs: 5 extra points of AC and +5%% chance to block."); break;
				case ART_ANTINSTANT_DEATH:
					pline("Artifact specs: resist disintegration and death rays when worn, warns of ants, poison cannot instakill you, neutral."); break;
				case ART_BLIND_PILOT:
					pline("Artifact specs: weak sight, random fainting, -10 increase damage and +10 increase accuracy when worn, neutral."); break;
				case ART_GUANTANAMERA:
					pline("Artifact specs: Applying it puts monsters in your proximity to sleep, but also you, even if you are sleep resistant."); break;
				case ART_DERRSCH:
					pline("Artifact specs: +6 damage."); break;
				case ART_STEEL_GREATER_ROCK:
					pline("Artifact specs: +12 damage."); break;
				case ART_HELLBRINGER:
					pline("Artifact specs: +8 to-hit and +8 damage to fire-susceptible monsters, fire resistance when wielded."); break;
				case ART_KILLER_PIANO:
					pline("Artifact specs: +6 to-hit and double damage, beheads monster, aggravate monster and gridbug conduct when wielded, applying it gives a permanent intrinsic nasty effect."); break;
				case ART_SOUNDTONE_FM:
					pline("Artifact specs: half spell damage and sound effects when worn."); break;
				case ART_STABLE_STUNT:
					pline("Artifact specs: disintegration resistance when worn."); break;
				case ART_CRAWLING_FROM_THE_WOODWORK:
					pline("Artifact specs: bosses spawn more often while you wear it."); break;
				case ART_BREATHER_SHOW:
					pline("Artifact specs: wearing it displays all monsters with breath attacks."); break;
				case ART_TRUE_GRIME:
					pline("Artifact specs: binning a corpse with it improves your alignment and maximum alignment, and displays their values."); break;
				case ART_SUPERMARKET_FU:
					pline("Artifact specs: +1-10 damage, and another 1-10 if you're a supermarket cashier."); break;
				case ART_GREASE_YOUR_BUTT:
					pline("Artifact specs: Applying it while it has charges will polymorph you into something very sexy."); break;
				case ART_TOO_PRECIOUS_TO_EAT:
					pline("Artifact specs: +6 to-hit and +12 damage."); break;
				case ART_MMMMMMMMMMMM_X____:
					pline("Artifact specs: +20 to-hit and +2 damage."); break;
				case ART_ARVOGENIA_S_HIGH_HEELSES:
					pline("Artifact specs: This pair of cream-colored high heels used to be worn by a beautiful topmodel with long hair. Wearing these cone-heeled lady pumps grants disintegration resistance and infravision."); break;
				case ART_NOW_YOU_HAVE_LOST:
					pline("Artifact specs: +10 increase damage when worn. Carries an ancient Morgothian curse."); break;
				case ART_FINGERMASH:
					pline("Artifact specs: if you kick a monster with them, that monster's weapon becomes cursed and loses all positive enchantment."); break;
				case ART_NULL_THE_LIVING_DATABASE:
					pline("Artifact specs: wearing it gives a slight chance each turn that you are cancelled."); break;
				case ART_ARCHEOLOGIST_SONG:
					pline("Artifact specs: wearing it as an archeologist will grant +2 increase damage and accuracy, 2 extra points of AC and slightly better spellcasting success rates, because the archeologist is the best class in the game. Lawful."); break;
				case ART_DIFFICULT_:
					pline("Artifact specs: ESP, half spell damage and half physical damage when wielded, doubles the level difficulty, chaotic."); break;
				case ART_LUISA_S_IRRESISTIBLE_CHARM:
					pline("Artifact specs: Wearing them reduces the chance that your items are damaged by erosion effects."); break;
				case ART_JANA_S_DECEPTIVE_MASK:
					pline("Artifact specs: It seems to do nothing obvious."); break;
				case ART_NOW_IT_BECOMES_DIFFERENT:
					pline("Artifact specs: Wearing it causes the monster selection algorithm to be replaced by a different one. You might notice the difference, or you might not."); break;
				case ART_NATASCHA_S_STROKING_UNITS:
					pline("Artifact specs: wearing them causes monsters that melee you to take thorns damage, unless they require a +1 or greater weapon to hit. Neutral."); break;
				case ART_SPEAK_TO_OJ:
					pline("Artifact specs: wearing them causes farting monsters to be spawned peaceful most of the time and sometimes tame. Chatting to a hostile farting monster will make it peaceful."); break;
				case ART_DUE_DUE_DUE_DUE_BRMMMMMMM:
					pline("Artifact specs: very fast speed when worn and 2 extra points of AC."); break;
				case ART_TOILET_NOISES:
					pline("Artifact specs: Wearing it enables monsters to use toilets."); break;
				case ART_LINE_CAN_PLAY_BY_YOURSELF:
					pline("Artifact specs: double speed and gridbug conduct when worn."); break;
				case ART_TOO_FAST__TOO_FURIOUS:
					pline("Artifact specs: prevents you from being interrupted when worn."); break;
				case ART_NOUROFIBROMA:
					pline("Artifact specs: free action when worn."); break;
				case ART_MADELINE_S_STUPID_GIRL:
					pline("Artifact specs: +3 increase damage when worn, but also spawns shit traps every once in a while and you trigger them even if you fly."); break;
				case ART_ARABELLA_S_RADAR:
					pline("Artifact specs: It allows you to DETECT MONSTERS when worn. Permanently! That is, until you take it off. You don't care about the other effects, because detect monsters is the grand daddy, nothing else allows you to simply see all the monsters, all the time!"); break;
				case ART_HENRIETTA_S_MAGICAL_AID:
					pline("Artifact specs: Wearing it greatly reduces your failure rates for all spells, and they also cost less mana, but it also grants teleportitis and disables teleport control. You don't know if it has any other effects."); break;
				case ART_JONADAB_S_HEAVYLOAD:
					pline("Artifact specs: ESP and invisibility while carried."); break;
				case ART_HANGING_CALL:
					pline("Artifact specs: +5 to-hit and +4 damage to acid-susceptible monsters, acid resistance when wielded, neutral. This is a 'temporary' artifact created by Soviet5lo, also known as the type of ice block whose variant is constantly being mocked in this one because Slash'EM Extended is just far better in every single way."); break;
				case ART_BLUE_SCREEN_OF_DEATH:
					pline("Artifact specs: shock resistance when worn, makes everything blue and occasionally spawns blue monsters."); break;
				case ART_SADDLE_OF_REFLECTION:
					pline("Artifact specs: Grants reflection to a monster, but both applying it and riding the creature that is wearing it will decrease your wisdom."); break;
				case ART____SHADES_OF_GRAYSWANDIR:
					pline("Artifact specs: +5 to-hit and double damage, hallucination resistance and shades of grey effect when wielded, lawful."); break;
				case ART_PUDDINGBANE:
					pline("Artifact specs: +5 to-hit and double damage to puddings."); break;
				case ART_FADED_USELESSNESS:
					pline("Artifact specs: +4 damage to imps, autocurses when wielded and causes slippery fingers and hallucination."); break;

				case ART_MOST_CHARISMATIC_PRESIDENT:
					pline("Artifact specs: +10 charisma and wall trap effect when worn, chaotic. Do you agree that Donald Trump is the best president that the United States ever had? :D"); break;
				case ART_MAGICAINT:
					pline("Artifact specs: magic resistance when worn."); break;
				case ART_SECANT_WHERELOCATION:
					pline("Artifact specs: teleportitis and teleport control when worn."); break;
				case ART_DUFFDUFFDUFF:
					pline("Artifact specs: +3 increase damage when worn."); break;
				case ART_INSANE_MIND_SCREW:
					pline("Artifact specs: reflection, magic and psi resistance when worn, chaotic."); break;
				case ART_RESISTANT_PUNCHING_BAG:
					pline("Artifact specs: drain resistance and +1 magic cancellation when worn."); break;
				case ART_HONORED_FAIRNESS:
					pline("Artifact specs: stealth, ESP and shock resistance when worn, lawful."); break;
				case ART_FAST_SPEED_BUMP:
					pline("Artifact specs: very fast speed when worn."); break;
				case ART_TURN_LOSS_EXTREME:
					pline("Artifact specs: searching and ESP when worn, but you cannot remove unseen monster markers."); break;
				case ART_CAN_T_TOUCH_THIS:
					pline("Artifact specs: reflection and improves your AC by 10 points when worn, neutral."); break;
 				case ART_CHANGED_RANDOM_NUMBERS:
					pline("Artifact specs: fiddles with the RNG when worn, causing the random numbers to behave slightly different. But chances are you'll never notice any effect from this."); break;
				case ART_UBB_RUPTURE:
					pline("Artifact specs: ESP, random fainting and stun resistance when worn, displays garbage strings from time to time and deactivates your resistances to confusion and disintegration."); break;
				case ART_NO_RMB_VACATION:
					pline("Artifact specs: right mouse button loss when worn."); break;
				case ART_SINCERE_MANIA:
					pline("Artifact specs: hallucination resistance when worn."); break;
				case ART_TELEVISION_WONDER:
					pline("Artifact specs: fleecescript when worn."); break;
				case ART_VIDEO_DECODER:
					pline("Artifact specs: flicker strips when worn."); break;
				case ART_UNIMPORTANT_ELEMENTS:
					pline("Artifact specs: resist acid, petrification and sleep when worn."); break;
				case ART_MARLENA_S_SONG:
					pline("Artifact specs: invisibility and displacement when worn."); break;
				case ART_DRELITT:
					pline("Artifact specs: recurring amnesia and recurring disenchantment when worn."); break;
				case ART_RUSSIAN_ICE_BLOCKS:
					pline("Artifact specs: cold resistance when worn."); break;
				case ART_BLACKY_S_BACK_WITHOUT_L:
					pline("Artifact specs: black NG walls when worn, and all liches are spawned peaceful."); break;
				case ART_DISENCHANTING_BLACKNESS:
					pline("Artifact specs: half spell damage and half physical damage when worn, recurring severe disenchantment."); break;
				case ART_RAAAAAAAARRRRRRGH:
					pline("Artifact specs: improves your damage output and AC by 5 points, but you can't tell monsters apart while wearing them."); break;
				case ART_UNEVEN_ENGINE:
					pline("Artifact specs: makes you very fast when worn but also causes the speed bug."); break;
				case ART_INDIAN_SMOKE_SYMBOL:
					pline("Artifact specs: If you wear them while having negative armor class, the damage reduction you get from your AC is better."); break;
				case ART_POWERWARP:
					pline("Artifact specs: teleport control when worn, increases the spawn chance for covetous monsters."); break;
				case ART_EQUIPPED_FOR_TROUBLE:
					pline("Artifact specs: flying when worn."); break;
				case ART_STONEWALL_CHECKERBOARD_DIS:
					pline("Artifact specs: petrification resistance when worn, allows you to chew through solid rock."); break;
				case ART_BLUEDE:
					pline("Artifact specs: reflection when worn and resistance to psi, fire, cold, shock, poison and sleep, but the game will occasionally spawn natalje traps. Don't let her step on them!"); break;
				case ART_SHORTFALL:
					pline("Artifact specs: fire resistance and magical breathing when worn."); break;
				case ART_BRIDGE_SHITTE:
					pline("Artifact specs: can walk on snow (and speed up in the process), but shit traps will spawn constantly and you trigger them even if you fly."); break;
				case ART_SUCH_A_WONDERFUL_ROOMMATE:
					pline("Artifact specs: The Amy her wonderful roommate will fill your stomach if you get paralyzed while hungry or worse."); break;
				case ART_VRRRRRRRRRRRR:
					pline("Artifact specs: makes you very fast and sometimes adds extra speed, so you'll become very fast indeed!"); break;
				case ART_NASTIST:
					pline("Artifact specs: fire, cold, petrification and sleep resistance when worn but also causes nastiness. Chaotic."); break;
				case ART_ZERDROY_GUNNING:
					pline("Artifact specs: If you put them on while not knowing the create ammo technique, you will learn it. Using the technique while wearing this artifact creates 3 times as many bullets but will also curse the boots again. Chaotic."); break;
				case ART_HYPOCRITICAL_FUN:
					pline("Artifact specs: polymorph control when worn, lawful."); break;
				case ART_LIKE_A_REAL_SERVER:
					pline("Artifact specs: disconnected stairs when worn. Be glad that I'm not evil enough to make this thing randomly disconnect you from the game! :-P"); break;
				case ART_EVIL_DETECTOR:
					pline("Artifact specs: if the blesscursing effect causes an item to be cursed, you will then know that the item in question is cursed."); break;
				case ART_WOUUU:
					pline("Artifact specs: 5 extra point of AC and clairvoyance when worn."); break;
				case ART_GAGARIN_S_TRANSLATOR:
					pline("Artifact specs: shock resistance, warning and infravision when worn, spells cost less mana to cast, all items are renamed to Russian."); break;
				case ART_SURTERSTAFF:
					pline("Artifact specs: If your wielded weapon uses the quarterstaff skill, it grants detect monsters but also carries a Topi Ylinen curse. But if you wield any other weapon, it fills you with the Black Breath instead."); break;
				case ART_HELIOKOPIS_S_WIZARDING_AID:
					pline("Artifact specs: +10 damage, petrification resistance when wielded, neutral. Tailor-made for Heliokopis who always plays a neutral male gnomish Wizard."); break;
				case ART_TOTAL_GENOCIDE:
					pline("Artifact specs: Genocides everything that can be genocided. The idea is by Lorskel. Don't worry, your own role and race are exempt, so this will not instakill you. However, it means that only monsters that cannot be genocided will be spawned, and those are rather more dangerous than the ones who can."); break;
				case ART_JANA_S_ROULETTE_OF_LIFE:
					pline("Artifact specs: It multiplies your chances of getting a wish from fountain quaffing by a factor 10! You're not sure why it's called 'roulette', because fountain quaffing has always been that..."); break;
				case ART_MAGIC_JUGGULATE:
					pline("Artifact specs: energy regeneration when worn."); break;
				case ART_HIGH_KING_OF_SKIRIM:
					pline("Artifact specs: cold resistance when worn and +5 AC, strength and charisma."); break;
				case ART_ALLCOLOR_PRISM:
					pline("Artifact specs: prism reflection when worn."); break;
				case ART_MARY_INSCRIPTION:
					pline("Artifact specs: reflection when worn and +10 charisma as well as +5 AC, but all monsters always spawn hostile because they hate Mary Sues with a fiery passion. Lawful."); break;
				case ART_FATHIEN_ELDER_S_SECRET_POW:
					pline("Artifact specs: prevents your occult spells from causing backlash, neutral."); break;
				case ART_SI_OH_WEE:
					pline("Artifact specs: Improves your damage and accuracy by 2 points each."); break;
				case ART_JOHANETTA_S_ROUGH_GENTLENE:
					pline("Artifact specs: cold and shock resistance when worn, neutral."); break;
				case ART_JANA_S_VAGINAL_FUN:
					pline("Artifact specs: They prevent your inventory from getting wet! What else can go wrong now? You're certain that this is the only thing it does, apart from having a stupid name."); break;
				case ART_VERY_NICE_PERSON:
					pline("Artifact specs: aggravate monster when worn, and monsters in special rooms always spawn awake because they want to tell you what a nice person you are. However, other monsters have a certain chance of spawning peaceful."); break;
				case ART_JULIA_S_REAL_LOVE:
					pline("Artifact specs: regeneration, fire resistance and +3 charisma when worn."); break;
				case ART_ELIANE_S_COMBAT_SNEAKERS:
					pline("Artifact specs: +20 charisma when worn, and kicking a monster has a small chance of instakilling it. However, you become highly vulnerable to farting attacks."); break;
				case ART_MAILIE_S_CHALLENGE:
					pline("Artifact specs: poison, psi and drain resistance as well as aggravate monster when worn, prevents your kick from being clumsy."); break;
				case ART_MERLOT_FUTURE:
					pline("Artifact specs: They can walk on 'snow' and speed up when they do."); break;
				case ART_MADELEINE_S_GIRL_FOOTSTEPS:
					pline("Artifact specs: resist disintegration and death rays when worn, kicking a monster with them gives +1 alignment because it's such a nice thing to do."); break;
				case ART_RUTH_S_MORTAL_ENEMY:
					pline("Artifact specs: flying and teleport control when worn. They are called that because Ruth engaged them in a duel once and lost."); break;
				case ART_LARISSA_S_ANGER:
					pline("Artifact specs: aggravate monster, cold, shock and disintegration resistance when worn, and your kick does 5 extra points of damage."); break;
				case ART_PRETTY_ROOMMAID:
					pline("Artifact specs: The Amy her roommaid is such a wonderful woman! <3 Shock and petrification resistance when worn."); break;
				case ART_ALISEH_S_RED_COLOR:
					pline("Artifact specs: fire resistance and +10 charisma when worn, but increases the chance of monsters stealing your items, and if you have sex, the chance of having children is higher."); break;
				case ART_KATIE_MELUA_S_FLEECINESS:
					pline("Artifact specs: prevents your potions from being destroyed by cold and also gives cold resistance, increases the effect of healing potions and spells, but you cannot tame new pets and existing ones can spontaneously rebel. Lawful."); break;
				case ART_ELONA_S_SNAIL_TRAIL:
					pline("Artifact specs: Wearing it as a snail will give +10 constitution and make you very fast, but if you're not a snail, it slows you down to half speed instead."); break;
				case ART_GENDER_INSPECIFIC_WHIP:
					pline("Artifact specs: +8 to-hit and +20 damage to monsters that are either always male or always female, feminist quest artifact."); break;
				case ART_EXTRA_CONTROL:
					pline("Artifact specs: never explodes when recharged, form changer quest artifact."); break;
				case ART_METEORIC_AC:
					pline("Artifact specs: 15 extra points of AC when worn, gang scholar quest artifact."); break;
				case ART_NUCLEAR_SPEAR:
					pline("Artifact specs: +14 damage to fire-susceptible monsters, can be invoked for dragon breath, nuclear physicist quest artifact."); break;
				case ART_SWORD_OF_GILGAMESH:
					pline("Artifact specs: warning and reflection when wielded, +5 to-hit and +10 damage, tracer quest artifact."); break;
				case ART_MOTHERFUCKER_TROPHY:
					pline("Artifact specs: reflection, magic resistance, +5 increase damage and accuracy, 20%% better spellcasting chances and increased skill training when worn. Congratulations. You beat the motherfucking ELDER PRIEST to get this artifact, and he's the most dangerous monster that exists in any NetHack variant!!! Well done, you truly are a master at this game!");
				case ART_HELM_OF_KNOWLEDGE:
					pline("Artifact specs: can be invoked for identify. Congratulations, you finished the Illusory Castle boss!"); break;
				case ART_BOOTS_OF_THE_MACHINE:
					pline("Artifact specs: aggravate monster and confusion resistance when worn and displays all golems and unliving monsters on the level. This artifact is found on the special level 'Machine' in the Illusory Castle."); break;
				case ART_ARKENSTONE_OF_THRAIN:
					pline("Artifact specs: can be invoked for perilous identify. This artifact is found on the special level 'Orc Barracks' in the Deep Mines."); break;
				case ART_BIZARRO_ORGASMATRON:
					pline("Artifact specs: can be invoked for branchporting, but if you use it too often it will inflict long-lasting inertia on you. Congratulations, you finished the Mainframe boss!"); break;
				case ART_KATIA_S_SOFT_COTTON:
					pline("Artifact specs: taking a crap while wearing it can occasionally increase your charisma. Congratulations, you finished the Hell's Bathroom boss! And fighting her was probably not disgusting at all!"); break;
				case ART_ANASTASIA_S_PERILOUS_GAMBL:
					pline("Artifact specs: Reading it teaches you a random technique, unless you get really unlucky and it rolls one you already know. But you'll also start getting random nasty trap effects intrinsically."); break;
				case ART_ERASE_ALL_DATA:
					pline("Artifact specs: data delete if you make the mistake of reading it. Doing so would be a terrible idea. Thankfully it doesn't have that effect if it's read by a monster - unless you're playing in evil variant mode, har har har!"); break;
				case ART_GAROK_S_HAMMER_KIT:
					pline("Artifact specs: This material kit may be used several times before it is used up. Hopefully it's of a useful material type!"); break;
				case ART_ACTUAL_PRECISION:
					pline("Artifact specs: +5 increase accuracy when worn, and an additional +5 if you're in a form that lacks hands."); break;
				case ART_HENRIETTA_S_TENACIOUSNESS:
					pline("Artifact specs: acid resistance and prevents your gear from being destroyed by erosion when worn. If you're in a form that lacks hands, it makes you extra tenacious by adding 10 points of AC and 1 point of MC. But somehow you get the feeling that there are massive drawbacks to using it..."); break;
				case ART_HEALENERATION:
					pline("Artifact specs: regeneration when worn, wearing it while in a form that lacks hands applies an uncursed unicorn horn effect every turn, even if you don't actually have a unicorn horn."); break;
				case ART_CAN_T_BRING_US_DOWN:
					pline("Artifact specs: free action when worn, and discount action if you are in a form without hands."); break;
				case ART_SCROOGE_S_MONEY_MEMORY:
					pline("Artifact specs: lets you find more gold, especially if you wear it while in a form without hands. Chaotic."); break;
				case ART_WHITE_WHALE_HATH_COME:
					pline("Artifact specs: cold resistance when worn. If you wear it while in a form that lacks hands, your potions cannot be destroyed by cold and you cannot slip on ice and are unaffected by snowstorms. Neutral."); break;
				case ART_BRRRRRRRRRRRRRMMMMMM:
					pline("Artifact specs: adds 50%% movement speed if you're on a highway. If you're in a form without hands, it adds the movement speed bonus regardless of the terrain you're on. However, it also constantly drains your mana while worn."); break;
				case ART_KATRIN_S_SUDDEN_APPEARANCE:
					pline("Artifact specs: teleport control, very fast speed and fainting when worn, disables free action and discount action. Wearing it without hands reduces the mana cost of controlled teleports (usually invoked via Ctrl-T) by half."); break;
				case ART_SINFUL_REPENTER:
					pline("Artifact specs: bad alignment when worn, but if you do something that increases your alignment, it will be increased more than usual. If you wear it while in a form without hands, the alignment bonus will be extra big and also increase your maximum alignment."); break;
				case ART_GYMNASTIC_LOVE:
					pline("Artifact specs: allows you to occasionally avoid melee attacks. If you wear it while not having hands, you can also sometimes avoid missile attacks."); break;
				case ART_SLEX_WANTS_YOU_TO_DIE_A_PA:
					pline("Artifact specs: It is probably not a good idea to put this on. But if you do it while not having hands, you will be magic resistant and reflecting."); break;
				case ART_FUKROSION:
					pline("Artifact specs: While wearing it, you occasionally get a prompt that allows you to repair a damaged item in your open inventory. If you are in a form without hands, the item you select will also be erosionproofed. Yes, you can select something that isn't eroded! Of course! :-)"); break;
				case ART_YES_YOU_CAN:
					pline("Artifact specs: teleport control and polymorph control while worn, and if you wear it while in a form without hands, you may sometimes use inertia control. Lawful."); break;
				case ART_RHEA_S_MISSING_EYESIGHT:
					pline("Artifact specs: greatly reduces your to-hit when worn. If you wear it while not having hands, your attacks will do more damage (at least if you manage to hit despite the penalty) and you will also have poison resistance. Neutral."); break;
				case ART_RUBBER_SHOALS:
					pline("Artifact specs: Randomly turns floor tiles into ash when worn, and grants shock resistance. If you wear it in a form without hands, it also grants fire resistance and allows you to swim in lava unharmed."); break;
				case ART_THAI_S_EROTIC_BITCH_FU:
					pline("Artifact specs: -10 constitution and +5 strength when worn. If you are in a form without hands, you also get +5 charisma, intelligence and wisdom and +10 dexterity. Neutral."); break;
				case ART_DOMPFINATION:
					pline("Artifact specs: While wearing it, you can successfully read cursed spellbooks. If in a form without hands, it also reduces the mana cost of all spells. Chaotic."); break;
				case ART_BURN_BABY_BURN:
					pline("Artifact specs: Burns you when worn, so you can't regenerate HP and Pw. While in a form without hands, it allows you to regenerate some HP and Pw whenever you kill a monster."); break;
				case ART_TIMEAGE_OF_REALMS:
					pline("Artifact specs: drain resistance when worn. If you lack hands, it also grants time resistance."); break;
				case ART_WARY_PROTECTORATE:
					pline("Artifact specs: half physical damage when worn, and if you are in a form without hands, it also grants half spell damage."); break;
				case ART_SOME_LITTLE_AID:
					pline("Artifact specs: +1 increase accuracy when worn, and if you are in a form without hands it also grants +1 increase damage."); break;
				case ART_HO_YOO_YOYO:
					pline("Artifact specs: If you try to pray while wearing it, the game will tell you whether it's safe to do so. Lack of hands is not considered a trouble if you pray while wearing it."); break;
				case ART_DECAPITATION_UP:
					pline("Artifact specs: your life will be saved while wearing it, but if it actually saves your life it will be destroyed. If you're in a form without hands, it also protects you from the beheading attacks of Vorpal Blade and similar weapons."); break;
				case ART_WONDERLOVELINESS:
					pline("Artifact specs: +5 charisma, and an additional +5 if you're in a form without hands."); break;
				case ART_MIGHTY_MOLASS:
					pline("Artifact specs: freezes you while worn. If you're in a form without hands, adjacent monsters will occasionally be slowed down."); break;
				case ART_UNFORGETTABLE_EVENT:
					pline("Artifact specs: keen memory when worn. If you lack hands, it also grants extra saving throws against amnesia effects."); break;
				case ART_DUBAI_TOWER_BREAK:
					pline("Artifact specs: causes the 'freeze' status effect to end more quickly when worn. If you wear it while not having hands, you also gain cold resistance and your potions cannot be destroyed by cold."); break;
				case ART_ARRGH_OUCH:
					pline("Artifact specs: Wearing it will continuously damage you. If you don't have hands, each of these damaging incidents will train your healing spell skill."); break;
				case ART_ETHERATORGARDEN:
					pline("Artifact specs: nastiness effect and magic resistance when worn. Wearing it while in a form without hands gives a 20%% speed boost. Chaotic."); break;
				case ART_READY_FOR_A_RIDE:
					pline("Artifact specs: increases your movement speed while riding when worn. If you're in a form without hands, your steed will also regenerate hit points faster depending on your riding skill, and you can pick up things while riding even if you're unskilled."); break;
				case ART_JANA_S_MAKE_UP_PUTTY:
					pline("Artifact specs: flying and unbreathing when worn. It might do something else too, probably depending on whether you are in a form that has hands because all implants seem to behave differently depending on whether you do..."); break;
				case ART_POTATOROK:
					pline("Artifact specs: fire and contamination resistance, see invisible and teleportitis when worn, and all monsters can cause Ragnarok with their melee attacks. If you have hands, it disables teleport control. But if you don't have hands, it gives half physical and spell damage instead. Lawful."); break;
				case ART_THEY_RE_ALL_YELLOW:
					pline("Artifact specs: acid resistance when worn, and regeneration if you're in a form without hands."); break;
				case ART_GELMER_KELANA_TWIN:
					pline("Artifact specs: monsters from vanilla NetHack spawn more often when you wear it, which means that all non-vanilla monsters become more rare. But if you're in a form with hands, it also makes the game behave like SLASHTHEM, which will fuck you up. The type of ice block laughs, 'Harharharharhar HARR-HARR!'"); break;
				case ART_NO_ABNORMAL_FUTURE:
					pline("Artifact specs: whenever a monster hits you in melee while you wear this, there's a slight chance that your items are randomly damaged. The individual chance of this happening may be small but consider how many hits you're going to take over the course of a single game... If you wear it while in a form without hands, it also allows you to use your techniques 4 times as often."); break;
				case ART_SIGNIFICANT_RNG_JITTER:
					pline("Artifact specs: polymorphitis and regeneration when worn. If you are in a form without hands, it also grants swimming, magical breathing and polymorph control, plus allows you to walk through iron bars, mountains and farmland."); break;
				case ART_LAUGHING_AT_MIDNIGHT:
					pline("Artifact specs: intrinsic loss when worn. If you're in a form without hands, it also grants poison and acid resistance and boosts your AC by 5 points."); break;
				case ART_YOU_SHOULD_SURRENDER:
					pline("Artifact specs: multiplies monster spawn frequency by a factor 5 and all monsters can respawn when killed, making the game almost impossible because you'll be bombarded with endless streams of monsters. If you wear it while in a form without hands, your speed is increased significantly and you can attack twice per turn, but that probably won't be enough to handle those hordes either."); break;
				case ART_ARABELLA_S_SEXY_CHARM:
					pline("Artifact specs: teleportitis when worn. If you lack hands, it also gives teleport control, full nutrients, technicality, contamination resistance and 20 extra points of AC. This artifact is definitely not a trap, it's awesome! Polymorph into something without hands and wear it!!!"); break;
				case ART_NEWFOUND_AND_USEFUL:
					pline("Artifact specs: free action when worn. If you wear it while in a form without hands, it also grants swimming and magical breathing, plus it will protect your items from becoming wet."); break;
				case ART_MAGICAL_PURPOSE:
					pline("Artifact specs: magic resistance when worn."); break;
				case ART_LUXIDREAM_S_ASCENSION:
					pline("Artifact specs: 10%% chance of life saving and 10%% increased speed when worn."); break;
				case ART_GOOD_GUYS_ALWAYS_WIN:
					pline("Artifact specs: +7 to-hit and +6 damage, lawful."); break;
				case ART_NEX_XUS:
					pline("Artifact specs: +5 to-hit and +2 damage, teleportitis and teleport control when wielded."); break;
				case ART_LITEBANE:
					pline("Artifact specs: +3 to-hit and +4 stun damage, magic resistance when wielded."); break;
				case ART_JUUPAD_STYLE:
					pline("Artifact specs: +1 to-hit and double damage. For some reason, Chris refuses to add a Vaapad style to dnethack, which would be an alternate form 7; he says it's the same as Juyo, yet he has both Shien and Djem So as form 5... *headscratch*"); break;

				default:
					pline("Missing artifact description (this is a bug). Tell Amy about it, including the name of the artifact in question, so she can add it!"); break;
			}

		} else if (obj->fakeartifact) {

			pline("This is not a real artifact, and therefore it doesn't actually do anything special.");

		}

	}

	return 0; /* a "return 1" would consume time --Amy */
}

#endif /* OVL1 */

/*invent.c*/

