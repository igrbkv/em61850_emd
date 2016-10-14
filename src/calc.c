#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "proto.h"
#include "sv_read.h"
#include "settings.h"
#include "calc.h"
#include "compute.h"

#include "debug.h"
#ifdef DEBUG
#include <stdio.h>
#endif


unsigned int EQ_TRAILS = 0; // На сколько уменьшать длину вектора данных чтобы учесть работу эквалайзера

double Kf = 1.0;
static int harmonics_count = 6;
//static int subharmonics_count = 6;

int calc_init()
{
	return 0;
}

int calc_close()
{
	return 0;
}
