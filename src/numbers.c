/* Dali Clock - a melting digital clock for PalmOS.
 * Copyright (c) 1991-2010 Jamie Zawinski <jwz@jwz.org>
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation.  No representations are made about the suitability of this
 * software for any purpose.  It is provided "as is" without express or
 * implied warranty.
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "numbers.h"

# include "font/zeroF.xbm"
# include "font/oneF.xbm"
# include "font/twoF.xbm"
# include "font/threeF.xbm"
# include "font/fourF.xbm"
# include "font/fiveF.xbm"
# include "font/sixF.xbm"
# include "font/sevenF.xbm"
# include "font/eightF.xbm"
# include "font/nineF.xbm"
# include "font/colonF.xbm"
# include "font/slashF.xbm"
FONT(F);

# include "font/zeroD2.xbm"
# include "font/oneD2.xbm"
# include "font/twoD2.xbm"
# include "font/threeD2.xbm"
# include "font/fourD2.xbm"
# include "font/fiveD2.xbm"
# include "font/sixD2.xbm"
# include "font/sevenD2.xbm"
# include "font/eightD2.xbm"
# include "font/nineD2.xbm"
# include "font/colonD2.xbm"
# include "font/slashD2.xbm"
FONT(D2);

#if 0
# include "font/zeroD.xbm"
# include "font/oneD.xbm"
# include "font/twoD.xbm"
# include "font/threeD.xbm"
# include "font/fourD.xbm"
# include "font/fiveD.xbm"
# include "font/sixD.xbm"
# include "font/sevenD.xbm"
# include "font/eightD.xbm"
# include "font/nineD.xbm"
# include "font/colonD.xbm"
# include "font/slashD.xbm"
FONT(D);
#endif

# include "font/zeroE.xbm"
# include "font/oneE.xbm"
# include "font/twoE.xbm"
# include "font/threeE.xbm"
# include "font/fourE.xbm"
# include "font/fiveE.xbm"
# include "font/sixE.xbm"
# include "font/sevenE.xbm"
# include "font/eightE.xbm"
# include "font/nineE.xbm"
# include "font/colonE.xbm"
# include "font/slashE.xbm"
FONT(E);

const struct raw_number *get_raw_number_0 (void) { return numbers_F;  }
const struct raw_number *get_raw_number_1 (void) { return numbers_D2; }
const struct raw_number *get_raw_number_2 (void) { return numbers_E;  }
const struct raw_number *get_raw_number_3 (void) { return numbers_D2; }

/* I'd like to use numbers_D (128) for get_raw_number_3, but it makes the
   data segment too large "Signed .word overflow; switch may be too large")
 */
