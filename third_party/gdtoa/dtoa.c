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
#include "libc/runtime/fenv.h"
#include "third_party/gdtoa/gdtoa.internal.h"

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
dtoa(double d0, int mode, int ndigits, int *decpt, int *sign, char **rve)
{
	/*	Arguments ndigits, decpt, sign are similar to those
		of ecvt and fcvt; trailing zeros are suppressed from
		the returned string.  If not null, *rve is set to point
		to the end of the return value.  If d is +-Infinity or NaN,
		then *decpt is set to 9999.

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
		4,5 ==> similar to 2 and 3, respectively, but (in
		round-nearest mode) with the tests of mode 0 to
		possibly return a shorter string that rounds to d.
		With IEEE arithmetic and compilation with
		-DHonor_FLT_ROUNDS, modes 4 and 5 behave the same
		as modes 2 and 3 when FLT_ROUNDS != 1.
		6-9 ==> Debugging modes similar to mode - 4:  don't try
		fast floating-point estimate (if applicable).

		Values of mode other than 0-9 are treated as mode 0.

		Sufficient space is allocated to the return value
		to hold the suppressed trailing zeros.
	*/
	ThInfo *TI = 0;
	int bbits, b2, b5, be, dig, i, ieps, ilim, ilim0, ilim1,
		j, j1, k, k0, k_check, leftright, m2, m5, s2, s5,
		spec_case, try_quick;
	Long L;
	int denorm;
	ULong x;
	Bigint *b, *b1, *delta, *mlo, *mhi, *S;
	U d, d2, eps, eps1;
	double ds;
	char *s, *s0;
	int Rounding;
	Rounding = FLT_ROUNDS;
	d.d = d0;
	if (word0(&d) & Sign_bit) {
		/* set sign for everything, including 0's and NaNs */
		*sign = 1;
		word0(&d) &= ~Sign_bit;	/* clear sign bit */
	}
	else
		*sign = 0;
	if ((word0(&d) & Exp_mask) == Exp_mask)
	{
		/* Infinity or NaN */
		*decpt = 9999;
		if (!word1(&d) && !(word0(&d) & 0xfffff))
			return __gdtoa_nrv_alloc("Infinity", rve, 8, &TI);
		return __gdtoa_nrv_alloc("NaN", rve, 3, &TI);
	}
	if (!dval(&d)) {
		*decpt = 1;
		return __gdtoa_nrv_alloc("0", rve, 1, &TI);
	}
	if (Rounding >= 2) {
		if (*sign)
			Rounding = Rounding == 2 ? 0 : 2;
		else
			if (Rounding != 2)
				Rounding = 0;
	}
	b = __gdtoa_d2b(dval(&d), &be, &bbits, &TI);
	if (( i = (int)(word0(&d) >> Exp_shift1 & (Exp_mask>>Exp_shift1)) )!=0) {
		dval(&d2) = dval(&d);
		word0(&d2) &= Frac_mask1;
		word0(&d2) |= Exp_11;
		/* log(x)	~=~ log(1.5) + (x-1.5)/1.5
		 * log10(x)	 =  log(x) / log(10)
		 *		~=~ log(1.5)/log(10) + (x-1.5)/(1.5*log(10))
		 * log10(&d) = (i-Bias)*log(2)/log(10) + log10(&d2)
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
		i -= Bias;
		denorm = 0;
	}
	else {
		/* d is denormalized */
		i = bbits + be + (Bias + (P-1) - 1);
		x = i > 32  ? word0(&d) << (64 - i) | word1(&d) >> (i - 32)
			: word1(&d) << (32 - i);
		dval(&d2) = x;
		word0(&d2) -= 31*Exp_msk1; /* adjust exponent */
		i -= (Bias + (P-1) - 1) + 1;
		denorm = 1;
	}
	ds = (dval(&d2)-1.5)*0.289529654602168 + 0.1760912590558 + i*0.301029995663981;
	k = (int)ds;
	if (ds < 0. && ds != k)
		k--;	/* want k = floor(ds) */
	k_check = 1;
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
	try_quick = Rounding == 1;
	if (mode > 5) {
		mode -= 4;
		try_quick = 0;
	}

	leftright = 1;
	ilim = ilim1 = -1;	/* Values for cases 0 and 1; done here to */
				/* silence erroneous "gcc -Wall" warning. */
	switch(mode) {
	case 0:
	case 1:
		i = 18;
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
	if (mode > 1 && Rounding != 1)
		leftright = 0;
	if (ilim >= 0 && ilim <= Quick_max && try_quick) {
		/* Try to get by with floating-point arithmetic. */
		i = 0;
                j1 = 0;
		dval(&d2) = dval(&d);
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
			dval(&d) /= ds;
		}
		else if (( j1 = -k )!=0) {
			dval(&d) *= __gdtoa_tens[j1 & 0xf];
			for(j = j1 >> 4; j; j >>= 1, i++)
				if (j & 1) {
					ieps++;
					dval(&d) *= __gdtoa_bigtens[i];
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
			dval(&eps) = 0.5/__gdtoa_tens[ilim-1] - dval(&eps);
			if (k0 < 0 && j1 >= 307) {
				eps1.d = 1.01e256; /* 1.01 allows roundoff in the next few lines */
				word0(&eps1) -= Exp_msk1 * (Bias+P-1);
				dval(&eps1) *= __gdtoa_tens[j1 & 0xf];
				for(i = 0, j = (j1-256) >> 4; j; j >>= 1, i++)
					if (j & 1)
						dval(&eps1) *= __gdtoa_bigtens[i];
				if (eps.d < eps1.d)
					eps.d = eps1.d;
				if (10. - d.d < 10.*eps.d && eps.d < 1.) {
					/* eps.d < 1. excludes trouble with the tiniest denormal */
					*s++ = '1';
					++k;
					goto ret1;
				}
			}
			for(i = 0;;) {
				L = dval(&d);
				dval(&d) -= L;
				*s++ = '0' + (int)L;
				if (dval(&d) < dval(&eps))
					goto retc;
				if (1. - dval(&d) < dval(&eps))
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
				L = (Long)(dval(&d));
				if (!(dval(&d) -= L))
					ilim = i;
				*s++ = '0' + (int)L;
				if (i == ilim) {
					if (dval(&d) > 0.5 + dval(&eps))
						goto bump_up;
					else if (dval(&d) < 0.5 - dval(&eps))
						goto retc;
					break;
				}
			}
		}
	fast_failed:
		s = s0;
		dval(&d) = dval(&d2);
		k = k0;
		ilim = ilim0;
	}

	/* Do we have a "small" integer? */
	if (be >= 0 && k <= Int_max) {
		/* Yes. */
		ds = __gdtoa_tens[k];
		if (ndigits < 0 && ilim <= 0) {
			S = mhi = 0;
			if (ilim < 0 || dval(&d) <= 5*ds)
				goto no_digits;
			goto one_digit;
		}
		for(i = 1;; i++, dval(&d) *= 10.) {
			L = (Long)(dval(&d) / ds);
			dval(&d) -= L*ds;
			/* If FLT_ROUNDS == 2, L will usually be high by 1 */
			if (dval(&d) < 0) {
				L--;
				dval(&d) += ds;
			}
			*s++ = '0' + (int)L;
			if (!dval(&d)) {
				break;
			}
			if (i == ilim) {
				if (mode > 1)
					switch(Rounding) {
					case 0: goto retc;
					case 2: goto bump_up;
					}
				dval(&d) += dval(&d);
				if (dval(&d) > ds || (dval(&d) == ds && L & 1)) {
				bump_up:
					while(*--s == '9')
						if (s == s0) {
							k++;
							*s = '0';
							break;
						}
					++*s++;
				}
				break;
			}
		}
		goto retc;
	}
	m2 = b2;
	m5 = b5;
	mhi = mlo = 0;
	if (leftright) {
		i = denorm ? be + (Bias + (P-1) - 1 + 1) : 1 + P - bbits;
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
			if (( j = b5 - m5 )!=0)
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
	if ((mode < 2 || leftright) && Rounding == 1) {
		if (!word1(&d) && !(word0(&d) & Bndry_mask) &&
		    word0(&d) & (Exp_mask & ~Exp_msk1)) {
			/* The special case */
			b2 += Log2P;
			s2 += Log2P;
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
	if (( i = ((s5 ? 32 - hi0bits(S->x[S->wds-1]) : 1) + s2) & 0x1f )!=0)
		i = 32 - i;
	if (i > 4) {
		i -= 4;
		b2 += i;
		m2 += i;
		s2 += i;
	}
	else if (i < 4) {
		i += 28;
		b2 += i;
		m2 += i;
		s2 += i;
	}
	if (b2 > 0)
		b = __gdtoa_lshift(b, b2, &TI);
	if (s2 > 0)
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
	if (ilim <= 0 && (mode == 3 || mode == 5)) {
		if (ilim < 0 || __gdtoa_cmp(b,S = __gdtoa_multadd(S,5,0,&TI)) <= 0) {
			/* no digits, fcvt style */
		no_digits:
			k = -1 - ndigits;
			goto ret;
		}
	one_digit:
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
			mhi = __gdtoa_lshift(mhi, Log2P, &TI);
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
			if (j1 == 0 && mode != 1 && !(word1(&d) & 1) && Rounding >= 1) {
				if (dig == '9')
					goto round_9_up;
				if (j > 0)
					dig++;
				*s++ = dig;
				goto ret;
			}
			if (j < 0 || (j == 0 && mode != 1 && !(word1(&d) & 1)
				    )) {
				if (!b->x[0] && b->wds <= 1) {
					goto accept_dig;
				}
				if (mode > 1)
					switch(Rounding) {
					case 0: goto accept_dig;
					case 2: goto keep_dig;
					}
				if (j1 > 0) {
					b = __gdtoa_lshift(b, 1, &TI);
					j1 = __gdtoa_cmp(b, S);
					if ((j1 > 0 || (j1 == 0 && dig & 1))
					    && dig++ == '9')
						goto round_9_up;
				}
			accept_dig:
				*s++ = dig;
				goto ret;
			}
			if (j1 > 0) {
				if (!Rounding && mode > 1)
					goto accept_dig;
				if (dig == '9') { /* possible if i == 1 */
				round_9_up:
					*s++ = '9';
					goto roundoff;
				}
				*s++ = dig + 1;
				goto ret;
			}
		keep_dig:
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
	else {
		for(i = 1;; i++) {
			*s++ = dig = __gdtoa_quorem(b,S) + '0';
			if (!b->x[0] && b->wds <= 1) {
				goto ret;
			}
			if (i >= ilim)
				break;
			b = __gdtoa_multadd(b, 10, 0, &TI);
		}
	}

	/* Round off last digit */
	switch(Rounding) {
	case 0: goto trimzeros;
	case 2: goto roundoff;
	}
	b = __gdtoa_lshift(b, 1, &TI);
	j = __gdtoa_cmp(b, S);
	if (j > 0 || (j == 0 && dig & 1))
	{
	roundoff:
		while(*--s == '9')
			if (s == s0) {
				k++;
				*s++ = '1';
				goto ret;
			}
		++*s++;
	}
	else {
	trimzeros:
		while(*--s == '0');
		s++;
	}
ret:
	__gdtoa_Bfree(S, &TI);
	if (mhi) {
		if (mlo && mlo != mhi)
			__gdtoa_Bfree(mlo, &TI);
		__gdtoa_Bfree(mhi, &TI);
	}
retc:
	while(s > s0 && s[-1] == '0')
		--s;
ret1:
	__gdtoa_Bfree(b, &TI);
	*s = 0;
	*decpt = k + 1;
	if (rve)
		*rve = s;
	return s0;
}
