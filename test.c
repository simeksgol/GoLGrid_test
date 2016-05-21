#include <stdlib.h>
#include <inttypes.h>
#include <memory.h>
#include <time.h>
#include <stdio.h>

#include "lib.c"
#include "rect.c"
#include "randomarray.c"
#include "golgrid.c"
#include "gridmisc.c"

static char *Lidka = ".A$A.A$.A8$8.A$6.A.A$5.2A.A2$4.3A!";

void main_do ()
{
	Rect r1;
	Rect_make (&r1, -256, -256, 512, 512);
	
	GoLGrid gg [2];
	GoLGrid_create (&gg [0], &r1);
	GoLGrid_create (&gg [1], &r1);
	
	GoLGrid_parse_life_history (Lidka, -4, -7, &gg [0], NULL, NULL, NULL, NULL, NULL);
	
	int gen;
	for (gen = 0; gen < 29056; gen++)
		GoLGrid_evolve (&gg [gen % 2], &gg [(gen + 1) % 2]);
	
	GoLGrid_print_life_history (&gg [0]);
}

int main ()
{
	if (!verify_cpu_type (TRUE, TRUE))
		return 0;
	
	main_do ();
	
	return 0;
}
