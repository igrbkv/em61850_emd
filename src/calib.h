#ifndef CALIB_H_
#define CALIB_H_

#include <stdint.h>
#include "sv_read.h"
#include "proto.h"

int make_calib_null(calc_req *req, struct dvalue *vals);
int make_calib_scale(calc_req *req, struct dvalue *vals);
int make_calib_angle(calc_req *req, struct dvalue *vals);
#endif
