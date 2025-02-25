/*-*- mode:c;indent-tabs-mode:t;c-basic-offset:8;tab-width:8;coding:utf-8   -*-│
│ vi: set noet ft=c ts=8 tw=8 fenc=utf-8                                   :vi │
╚──────────────────────────────────────────────────────────────────────────────╝
│                                                                              │
│  The author of this software is David M. Gay.                                │
│  Please send bug reports to David M. Gay <dmg@acm.org>                       │
│                          or Justine Tunney <jtunney@gmail.com>               │
│                                                                              │
│  Copyright (C) 1998, 1999 by Lucent Technologies                             │
│  All Rights Reserved                                                         │
│                                                                              │
│  Permission to use, copy, modify, and distribute this software and           │
│  its documentation for any purpose and without fee is hereby                 │
│  granted, provided that the above copyright notice appear in all             │
│  copies and that both that the copyright notice and this                     │
│  permission notice and warranty disclaimer appear in supporting              │
│  documentation, and that the name of Lucent or any of its entities           │
│  not be used in advertising or publicity pertaining to                       │
│  distribution of the software without specific, written prior                │
│  permission.                                                                 │
│                                                                              │
│  LUCENT DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,               │
│  INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS.            │
│  IN NO EVENT SHALL LUCENT OR ANY OF ITS ENTITIES BE LIABLE FOR ANY           │
│  SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES                   │
│  WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER             │
│  IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION,              │
│  ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF              │
│  THIS SOFTWARE.                                                              │
│                                                                              │
╚─────────────────────────────────────────────────────────────────────────────*/
#include "third_party/gdtoa/gdtoa.internal.h"

static Bigint *
bitstob(ULong *bits, int nbits, int *bbits, ThInfo **PTI)
{
	int i, k;
	Bigint *b;
	ULong *be, *x, *x0;
	i = ULbits;
	k = 0;
	while(i < nbits) {
		i <<= 1;
		k++;
	}
	b = __gdtoa_Balloc(k, PTI);
	be = bits + ((nbits - 1) >> kshift);
	x = x0 = b->x;
	do {
		*x++ = *bits & ALL_ON;
	} while(++bits <= be);
	i = x - x0;
	while(!x0[--i])
		if (!i) {
			b->wds = 0;
			*bbits = 0;
			goto ret;
		}
	b->wds = i + 1;
	*bbits = i*ULbits + 32 - hi0bits(b->x[i]);
ret:
	return b;
}

/* dtoa for IEEE arithmetic (dmg): convert double to ASCII string.
 *
 * Inspired by "How to Print Floating-Point Numbers Accurately" by
 * Guy L. Steele, Jr. and Jon L. White [Proc. ACM SIGPLAN '90, pp. 112-126].
 *
 * Modifications:
 *	1. Rather than iterating, we use a simple numeric overestimate
 *	   to determine k = floor(log10(d)).  We scale relevant
 *	   quantities using O(log2(k)) rather than O(k) __gdtoa_multiplications.
 *	2. For some modes > 2 (corresponding to ecvt and fcvt), we don't
 *	   try to generate digits strictly left to right.  Instead, we
 *	   compute with fewer bits and propagate the carry if necessary
 *	   when rounding the final digit up.  This is often faster.
 *	3. Under the as__gdtoa_sumption that input will be rounded nearest,
 *	   mode 0 renders 1e23 as 1e23 rather than 9.999999999999999e22.
 *	   That is, we allow equality in stopping tests when the
 *	   round-nearest rule will give the same floating-point value
 *	   as would satisfaction of the stopping test with strict
 *	   inequality.
 *	4. We remove common factors of powers of 2 from relevant
 *	   quantities.
 *	5. When converting floating-point integers less than 1e16,
 *	   we use floating-point arithmetic rather than resorting
 *	   to __gdtoa_multiple-precision integers.
 *	6. When asked to produce fewer than 15 digits, we first try
 *	   to get by with floating-point arithmetic; we resort to
 *	   __gdtoa_multiple-precision integer arithmetic only if we cannot
 *	   guarantee that the floating-point calculation has given
 *	   the correctly rounded result.  For k requested digits and
 *	   "uniformly" distributed input, the probability is
 *	   something like 10^(k-15) that we must resort to the Long
 *	   calculation.
 */

char *
gdtoa(const FPI *fpi, int be, ULong *bits, int *kindp, int mode, int ndigits, int *decpt, char **rve)
{
 /*	Arguments ndigits and decpt are similar to the second and third
	arguments of ecvt and fcvt; trailing zeros are suppressed from
	the returned string.  If not null, *rve is set to point
	to the end of the return value.  If d is +-Infinity or NaN,
	then *decpt is set to 9999.
	be = exponent: value = (integer represented by bits) * (2 to the power of be).

	mode:
		0 ==> shortest string that yields d when read in
			and rounded to nearest.
		1 ==> like 0, but with Steele & White stopping rule;
			e.g. with IEEE P754 arithmetic , mode 0 gives
			1e23 whereas mode 1 gives 9.999999999999999e22.
		2 ==> max(1,ndigits) significant digits.  This gives a
			return value similar to that of ecvt, except
			that trailing zeros are suppressed.
		3 ==> through ndigits past the decimal point.  This
			gives a return value similar to that from fcvt,
			except that trailing zeros are suppressed, and
			ndigits can be negative.
		4-9 should give the same return values as 2-3, i.e.,
			4 <= mode <= 9 ==> same return as mode
			2 + (mode & 1).  These modes are mainly for
			debugging; often they run slower but sometimes
			faster than modes 2-3.
		4,5,8,9 ==> left-to-right digit gene__gdtoa_ration.
		6-9 ==> don't try fast floating-point estimate
			(if applicable).

		Values of mode other than 0-9 are treated as mode 0.

		Sufficient space is allocated to the return value
		to hold the suppressed trailing zeros.
	*/

	ThInfo *TI = 0;
	int bbits, b2, b5, be0, dig, i, ieps, ilim, ilim0, ilim1, inex;
	int j, j1, k, k0, k_check, kind, leftright, m2, m5, nbits;
	int rdir, s2, s5, spec_case, try_quick;
	Long L;
	Bigint *b, *b1, *delta, *mlo, *mhi, *mhi1, *S;
	double d2, ds;
	char *s, *s0;
	U d, eps;
	inex = 0;
	kind = *kindp &= ~STRTOG_Inexact;
	switch(kind & STRTOG_Retmask) {
	case STRTOG_Zero:
		goto ret_zero;
	case STRTOG_Normal:
	case STRTOG_Denormal:
		break;
	case STRTOG_Infinite:
		*decpt = -32768;
		return __gdtoa_nrv_alloc("Infinity", rve, 8, &TI);
	case STRTOG_NaN:
		*decpt = -32768;
		return __gdtoa_nrv_alloc("NaN", rve, 3, &TI);
	default:
		return 0;
	}
	b = bitstob(bits, nbits = fpi->nbits, &bbits, &TI);
	be0 = be;
	if ( (i = __gdtoa_trailz(b)) !=0) {
		__gdtoa_rshift(b, i);
		be += i;
		bbits -= i;
	}
	if (!b->wds) {
		__gdtoa_Bfree(b, &TI);
	ret_zero:
		*decpt = 1;
		return __gdtoa_nrv_alloc("0", rve, 1, &TI);
	}
	dval(&d) = __gdtoa_b2d(b, &i);
	i = be + bbits - 1;
	word0(&d) &= Frac_mask1;
	word0(&d) |= Exp_11;
	/* log(x)	~=~ log(1.5) + (x-1.5)/1.5
	 * log10(x)	 =  log(x) / log(10)
	 *		~=~ log(1.5)/log(10) + (x-1.5)/(1.5*log(10))
	 * log10(&d) = (i-Bias)*log(2)/log(10) + log10(d2)
	 *
	 * This suggests computing an approximation k to log10(&d) by
	 *
	 * k = (i - Bias)*0.301029995663981
	 *	+ ( (d2-1.5)*0.289529654602168 + 0.176091259055681 );
	 *
	 * We want k to be too large rather than too small.
	 * The error in the first-order Taylor series approximation
	 * is in our favor, so we just round up the constant enough
	 * to compensate for any error in the __gdtoa_multiplication of
	 * (i - Bias) by 0.301029995663981; since |i - Bias| <= 1077,
	 * and 1077 * 0.30103 * 2^-52 ~=~ 7.2e-14,
	 * adding 1e-13 to the constant term more than suffices.
	 * Hence we adjust the constant term to 0.1760912590558.
	 * (We could get a more accurate k by invoking log10,
	 *  but this is probably not worthwhile.)
	 */
	ds = (dval(&d)-1.5)*0.289529654602168 + 0.1760912590558 + i*0.301029995663981;
	/* correct as__gdtoa_sumption about exponent range */
	if ((j = i) < 0)
		j = -j;
	if ((j -= 1077) > 0)
		ds += j * 7e-17;
	k = (int)ds;
	if (ds < 0. && ds != k)
		k--;	/* want k = floor(ds) */
	k_check = 1;
        // TODO: word0(&d) += (be + bbits - 1) << Exp_shift;
        // error: third_party/gdtoa/gdtoa.c:244: left shift of negative value -6 'int' 20 'int'
        // 4161d8: __die at libc/log/die.c:33
        // 463165: __ubsan_abort at libc/intrin/ubsan.c:270
        // 4632d6: __ubsan_handle_shift_out_of_bounds at libc/intrin/ubsan.c:299
        // 421d42: gdtoa at third_party/gdtoa/gdtoa.c:244
        // 420449: g_dfmt_p at third_party/gdtoa/g_dfmt_p.c:105
        // 413947: ConvertMatrixToStringTable at tool/viz/lib/formatmatrix-double.c:40
        // 413a5f: FormatMatrixDouble at tool/viz/lib/formatmatrix-double.c:55
        // 413b13: StringifyMatrixDouble at tool/viz/lib/formatmatrix-double.c:65
        // 464923: GetChromaticAdaptationMatrix_testD65ToD50_soWeCanCieLab at test/dsp/core/illumination_test.c:39
        // 4650c2: testlib_runtestcases at libc/testlib/testrunner.c:94
        // 464676: testlib_runalltests at libc/testlib/runner.c:37
        // 46455e: main at libc/testlib/testmain.c:84
        // 401d30: cosmo at libc/runtime/cosmo.S:65
        // 401173: _start at libc/crt/crt.S:67
	word0(&d) += (unsigned)(be + bbits - 1) << Exp_shift;
	if (k >= 0 && k <= Ten_pmax) {
		if (dval(&d) < __gdtoa_tens[k])
			k--;
		k_check = 0;
	}
	j = bbits - i - 1;
	if (j >= 0) {
		b2 = 0;
		s2 = j;
	}
	else {
		b2 = -j;
		s2 = 0;
	}
	if (k >= 0) {
		b5 = 0;
		s5 = k;
		s2 += k;
	}
	else {
		b2 -= k;
		b5 = -k;
		s5 = 0;
	}
	if (mode < 0 || mode > 9)
		mode = 0;
	try_quick = 1;
	if (mode > 5) {
		mode -= 4;
		try_quick = 0;
	}
	else if (i >= -4 - Emin || i < Emin)
		try_quick = 0;
	leftright = 1;
	ilim = ilim1 = -1;	/* Values for cases 0 and 1; done here to */
				/* silence erroneous "gcc -Wall" warning. */
	switch(mode) {
	case 0:
	case 1:
		i = (int)(nbits * .30103) + 3;
		ndigits = 0;
		break;
	case 2:
		leftright = 0;
		/* no break */
	case 4:
		if (ndigits <= 0)
			ndigits = 1;
		ilim = ilim1 = i = ndigits;
		break;
	case 3:
		leftright = 0;
		/* no break */
	case 5:
		i = ndigits + k + 1;
		ilim = i;
		ilim1 = i - 1;
		if (i <= 0)
			i = 1;
	}
	s = s0 = __gdtoa_rv_alloc(i, &TI);
	if (mode <= 1)
		rdir = 0;
	else if ( (rdir = fpi->rounding - 1) !=0) {
		if (rdir < 0)
			rdir = 2;
		if (kind & STRTOG_Neg)
			rdir = 3 - rdir;
	}
	/* Now rdir = 0 ==> round near, 1 ==> round up, 2 ==> round down. */
	if (ilim >= 0 && ilim <= Quick_max && try_quick && !rdir && k == 0) {
		/* Try to get by with floating-point arithmetic. */
		i = 0;
		d2 = dval(&d);
		k0 = k;
		ilim0 = ilim;
		ieps = 2; /* conservative */
		if (k > 0) {
			ds = __gdtoa_tens[k&0xf];
			j = k >> 4;
			if (j & Bletch) {
				/* prevent overflows */
				j &= Bletch - 1;
				dval(&d) /= __gdtoa_bigtens[n___gdtoa_bigtens-1];
				ieps++;
			}
			for(; j; j >>= 1, i++)
				if (j & 1) {
					ieps++;
					ds *= __gdtoa_bigtens[i];
				}
		}
		else  {
			ds = 1.;
			if ( (j1 = -k) !=0) {
				dval(&d) *= __gdtoa_tens[j1 & 0xf];
				for(j = j1 >> 4; j; j >>= 1, i++)
					if (j & 1) {
						ieps++;
						dval(&d) *= __gdtoa_bigtens[i];
					}
			}
		}
		if (k_check && dval(&d) < 1. && ilim > 0) {
			if (ilim1 <= 0)
				goto fast_failed;
			ilim = ilim1;
			k--;
			dval(&d) *= 10.;
			ieps++;
		}
		dval(&eps) = ieps*dval(&d) + 7.;
		word0(&eps) -= (P-1)*Exp_msk1;
		if (ilim == 0) {
			S = mhi = 0;
			dval(&d) -= 5.;
			if (dval(&d) > dval(&eps))
				goto one_digit;
			if (dval(&d) < -dval(&eps))
				goto no_digits;
			goto fast_failed;
		}
		if (leftright) {
			/* Use Steele & White method of only
			 * generating digits needed.
			 */
			dval(&eps) = ds*0.5/__gdtoa_tens[ilim-1] - dval(&eps);
			for(i = 0;;) {
				L = (Long)(dval(&d)/ds);
				dval(&d) -= L*ds;
				*s++ = '0' + (int)L;
				if (dval(&d) < dval(&eps)) {
					if (dval(&d))
						inex = STRTOG_Inexlo;
					goto ret1;
				}
				if (ds - dval(&d) < dval(&eps))
					goto bump_up;
				if (++i >= ilim)
					break;
				dval(&eps) *= 10.;
				dval(&d) *= 10.;
			}
		}
		else {
			/* Generate ilim digits, then fix them up. */
			dval(&eps) *= __gdtoa_tens[ilim-1];
			for(i = 1;; i++, dval(&d) *= 10.) {
				if ( (L = (Long)(dval(&d)/ds)) !=0)
					dval(&d) -= L*ds;
				*s++ = '0' + (int)L;
				if (i == ilim) {
					ds *= 0.5;
					if (dval(&d) > ds + dval(&eps))
						goto bump_up;
					else if (dval(&d) < ds - dval(&eps)) {
						if (dval(&d))
							inex = STRTOG_Inexlo;
						goto ret1;
					}
					break;
				}
			}
		}
	fast_failed:
		s = s0;
		dval(&d) = d2;
		k = k0;
		ilim = ilim0;
	}
	/* Do we have a "small" integer? */
	if (be >= 0 && k <= fpi->int_max) {
		/* Yes. */
		ds = __gdtoa_tens[k];
		if (ndigits < 0 && ilim <= 0) {
			S = mhi = 0;
			if (ilim < 0 || dval(&d) <= 5*ds)
				goto no_digits;
			goto one_digit;
		}
		for(i = 1;; i++, dval(&d) *= 10.) {
			L = dval(&d) / ds;
			dval(&d) -= L*ds;
			/* If FLT_ROUNDS == 2, L will usually be high by 1 */
			if (dval(&d) < 0) {
				L--;
				dval(&d) += ds;
			}
			*s++ = '0' + (int)L;
			if (dval(&d) == 0.)
				break;
			if (i == ilim) {
				if (rdir) {
					if (rdir == 1)
						goto bump_up;
					inex = STRTOG_Inexlo;
					goto ret1;
				}
				dval(&d) += dval(&d);
				if (dval(&d) > ds || (dval(&d) == ds && L & 1))
				{
				bump_up:
					inex = STRTOG_Inexhi;
					while(*--s == '9')
						if (s == s0) {
							k++;
							*s = '0';
							break;
						}
					++*s++;
				}
				else
					inex = STRTOG_Inexlo;
				break;
			}
		}
		goto ret1;
	}
	m2 = b2;
	m5 = b5;
	mhi = mlo = 0;
	if (leftright) {
		i = nbits - bbits;
		if (be - i++ < fpi->emin && mode != 3 && mode != 5) {
			/* denormal */
			i = be - fpi->emin + 1;
			if (mode >= 2 && ilim > 0 && ilim < i)
				goto small_ilim;
		}
		else if (mode >= 2) {
		small_ilim:
			j = ilim - 1;
			if (m5 >= j)
				m5 -= j;
			else {
				s5 += j -= m5;
				b5 += j;
				m5 = 0;
			}
			if ((i = ilim) < 0) {
				m2 -= i;
				i = 0;
			}
		}
		b2 += i;
		s2 += i;
		mhi = __gdtoa_i2b(1, &TI);
	}
	if (m2 > 0 && s2 > 0) {
		i = m2 < s2 ? m2 : s2;
		b2 -= i;
		m2 -= i;
		s2 -= i;
	}
	if (b5 > 0) {
		if (leftright) {
			if (m5 > 0) {
				mhi = __gdtoa_pow5mult(mhi, m5, &TI);
				b1 = __gdtoa_mult(mhi, b, &TI);
				__gdtoa_Bfree(b, &TI);
				b = b1;
			}
			if ( (j = b5 - m5) !=0)
				b = __gdtoa_pow5mult(b, j, &TI);
		}
		else
			b = __gdtoa_pow5mult(b, b5, &TI);
	}
	S = __gdtoa_i2b(1, &TI);
	if (s5 > 0)
		S = __gdtoa_pow5mult(S, s5, &TI);
	/* Check for special case that d is a normalized power of 2. */
	spec_case = 0;
	if (mode < 2) {
		if (bbits == 1 && be0 > fpi->emin + 1) {
			/* The special case */
			b2++;
			s2++;
			spec_case = 1;
		}
	}
	/* Arrange for convenient computation of quotients:
	 * shift left if necessary so divisor has 4 leading 0 bits.
	 *
	 * Perhaps we should just compute leading 28 bits of S once
	 * and for all and pass them and a shift to __gdtoa_quorem, so it
	 * can do shifts and ors to compute the numerator for q.
	 */
	i = ((s5 ? hi0bits(S->x[S->wds-1]) : ULbits - 1) - s2 - 4) & kmask;
	m2 += i;
	if ((b2 += i) > 0)
		b = __gdtoa_lshift(b, b2, &TI);
	if ((s2 += i) > 0)
		S = __gdtoa_lshift(S, s2, &TI);
	if (k_check) {
		if (__gdtoa_cmp(b,S) < 0) {
			k--;
			b = __gdtoa_multadd(b, 10, 0, &TI);	/* we botched the k estimate */
			if (leftright)
				mhi = __gdtoa_multadd(mhi, 10, 0, &TI);
			ilim = ilim1;
		}
	}
	if (ilim <= 0 && mode > 2) {
		if (ilim < 0 || __gdtoa_cmp(b,S = __gdtoa_multadd(S,5,0,&TI)) <= 0) {
			/* no digits, fcvt style */
		no_digits:
			k = -1 - ndigits;
			inex = STRTOG_Inexlo;
			goto ret;
		}
	one_digit:
		inex = STRTOG_Inexhi;
		*s++ = '1';
		k++;
		goto ret;
	}
	if (leftright) {
		if (m2 > 0)
			mhi = __gdtoa_lshift(mhi, m2, &TI);
		/* Compute mlo -- check for special case
		 * that d is a normalized power of 2.
		 */
		mlo = mhi;
		if (spec_case) {
			mhi = __gdtoa_Balloc(mhi->k, &TI);
			Bcopy(mhi, mlo);
			mhi = __gdtoa_lshift(mhi, 1, &TI);
		}
		for(i = 1;;i++) {
			dig = __gdtoa_quorem(b,S) + '0';
			/* Do we yet have the shortest decimal string
			 * that will round to d?
			 */
			j = __gdtoa_cmp(b, mlo);
			delta = __gdtoa_diff(S, mhi, &TI);
			j1 = delta->sign ? 1 : __gdtoa_cmp(b, delta);
			__gdtoa_Bfree(delta, &TI);
			if (j1 == 0 && !mode && !(bits[0] & 1) && !rdir) {
				if (dig == '9')
					goto round_9_up;
				if (j <= 0) {
					if (b->wds > 1 || b->x[0])
						inex = STRTOG_Inexlo;
				}
				else {
					dig++;
					inex = STRTOG_Inexhi;
				}
				*s++ = dig;
				goto ret;
			}
			if (j < 0 || (j == 0 && !mode && !(bits[0] & 1))) {
				if (rdir && (b->wds > 1 || b->x[0])) {
					if (rdir == 2) {
						inex = STRTOG_Inexlo;
						goto accept;
					}
					while (__gdtoa_cmp(S,mhi) > 0) {
						*s++ = dig;
						mhi1 = __gdtoa_multadd(mhi, 10, 0, &TI);
						if (mlo == mhi)
							mlo = mhi1;
						mhi = mhi1;
						b = __gdtoa_multadd(b, 10, 0, &TI);
						dig = __gdtoa_quorem(b,S) + '0';
					}
					if (dig++ == '9')
						goto round_9_up;
					inex = STRTOG_Inexhi;
					goto accept;
				}
				if (j1 > 0) {
					b = __gdtoa_lshift(b, 1, &TI);
					j1 = __gdtoa_cmp(b, S);
					if ((j1 > 0 || (j1 == 0 && dig & 1)) && dig++ == '9')
						goto round_9_up;
					inex = STRTOG_Inexhi;
				}
				if (b->wds > 1 || b->x[0])
					inex = STRTOG_Inexlo;
			accept:
				*s++ = dig;
				goto ret;
			}
			if (j1 > 0 && rdir != 2) {
				if (dig == '9') { /* possible if i == 1 */
				round_9_up:
					*s++ = '9';
					inex = STRTOG_Inexhi;
					goto roundoff;
				}
				inex = STRTOG_Inexhi;
				*s++ = dig + 1;
				goto ret;
			}
			*s++ = dig;
			if (i == ilim)
				break;
			b = __gdtoa_multadd(b, 10, 0, &TI);
			if (mlo == mhi)
				mlo = mhi = __gdtoa_multadd(mhi, 10, 0, &TI);
			else {
				mlo = __gdtoa_multadd(mlo, 10, 0, &TI);
				mhi = __gdtoa_multadd(mhi, 10, 0, &TI);
			}
		}
	}
	else
		for(i = 1;; i++) {
			*s++ = dig = __gdtoa_quorem(b,S) + '0';
			if (i >= ilim)
				break;
			b = __gdtoa_multadd(b, 10, 0, &TI);
		}
	/* Round off last digit */
	if (rdir) {
		if (rdir == 2 || (b->wds <= 1 && !b->x[0]))
			goto chopzeros;
		goto roundoff;
	}
	b = __gdtoa_lshift(b, 1, &TI);
	j = __gdtoa_cmp(b, S);
	if (j > 0 || (j == 0 && dig & 1))
	{
	roundoff:
		inex = STRTOG_Inexhi;
		while(*--s == '9')
			if (s == s0) {
				k++;
				*s++ = '1';
				goto ret;
			}
		++*s++;
	}
	else {
	chopzeros:
		if (b->wds > 1 || b->x[0])
			inex = STRTOG_Inexlo;
	}
ret:
	__gdtoa_Bfree(S, &TI);
	if (mhi) {
		if (mlo && mlo != mhi)
			__gdtoa_Bfree(mlo, &TI);
		__gdtoa_Bfree(mhi, &TI);
	}
ret1:
	while(s > s0 && s[-1] == '0')
		--s;
	__gdtoa_Bfree(b, &TI);
	*s = 0;
	*decpt = k + 1;
	if (rve)
		*rve = s;
	*kindp |= inex;
	return s0;
}
