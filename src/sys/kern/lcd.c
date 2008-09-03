#include "punix.h"
#include "lcd.h"
#include "proc.h"
#include "buf.h"
#include "dev.h"
#include "filsys.h"
#include "inode.h"
#include "mount.h"
#include "queue.h"
#include "globals.h"

/* input: cont = contrast level between 0 and CONTRASTMAX, inclusive
 *        higher values => darker screen
 */
/* FIXME: this should be improved */
STARTUP(int lcd_set_contrast(int cont))
{
	int ch;
	if (cont < 0)
		cont = 0;
	if (CONTRASTMAX < cont)
		cont = CONTRASTMAX;
#ifdef TI89
	ch = CONTRAST_VMUL | cont;
#else
	ch = CONTRAST_VMUL | ~cont;
#endif
	CONTRASTPORT = ch;
	G.contrast = cont;
	return cont;
}

STARTUP(int lcd_inc_contrast())
{
	return lcd_set_contrast(G.contrast+1);
}

STARTUP(int lcd_dec_contrast())
{
	return lcd_set_contrast(G.contrast-1);
}

STARTUP(void lcdinit())
{
	lcd_set_contrast(CONTRASTMAX/2+1);
}
