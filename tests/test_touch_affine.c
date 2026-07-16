#include <assert.h>
#include <math.h>
#include <stdio.h>

#include "touch_affine.h"

static int near(double left, double right)
{
    return fabs(left - right) < 1e-9;
}

int main(void)
{
    struct touch_affine affine;
    double x;
    double y;

    touch_affine_identity(&affine);
    assert(touch_affine_valid(&affine));
    touch_affine_apply(&affine, 0.25, 0.75, &x, &y);
    assert(near(x, 0.25) && near(y, 0.75));

    affine.value[0] = 0.9;
    affine.value[2] = 0.05;
    affine.value[4] = 0.8;
    affine.value[5] = 0.1;
    assert(touch_affine_valid(&affine));
    touch_affine_apply(&affine, 0.5, 0.5, &x, &y);
    assert(near(x, 0.5) && near(y, 0.5));

    affine.value[6] = 0.01;
    assert(!touch_affine_valid(&affine));
    touch_affine_identity(&affine);
    affine.value[4] = 0.0;
    assert(!touch_affine_valid(&affine));

    puts("touch affine tests passed");
    return 0;
}
