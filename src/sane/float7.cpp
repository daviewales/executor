/* Copyright 1990, 1996 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

/* Forward declarations in SANE.h (DO NOT DELETE THIS LINE) */

#include <base/common.h>
#include <SANE.h>
#include <sane/float.h>
#include <ctype.h>

using namespace Executor;

#define LOWER(x) ((x) | 0x20) /* converts to lower case if x is A-Z */


void Executor::C_ROMlib_Fdec2str(DecForm *volatile sp2, Decimal *volatile sp,
                                 Decstr volatile dp)

{
    int i, j, exponent, expsize, beforedecimal, afterdecimal;
    int digits, style;

#define MAXEXPSIZE 8 /* maybe 6 or 7, but just in case. */
#define MAXDECSTRLEN 80
    char backwardsexp[MAXEXPSIZE];

    warning_floating_point("");
    digits = sp2->digits;
    style = sp2->style;

    switch(style & DECIMALTYPEMASK)
    {
        case FloatDecimal:
            if(sp->sgn)
                dp[1] = '-';
            else
                dp[1] = ' ';
            dp[2] = sp->sig[1];
            if((sp->sig[0] > 1) || digits > 1)
            {
                dp[3] = '.';
                for(i = 2; i <= sp->sig[0]; i++)
                    dp[i + 2] = sp->sig[i];
                while(i < digits)
                {
                    dp[i + 2] = '0';
                    i++;
                }
            }
            else
            {
                i = 1;
            }
            dp[i + 2] = 'e';
            exponent = sp->exp + sp->sig[0] - 1;
            if(exponent < 0)
            {
                dp[i + 3] = '-';
                exponent = -exponent;
            }
            else
            {
                dp[i + 3] = '+';
            }
            expsize = 1;
            backwardsexp[1] = (exponent % 10) + '0';
            exponent /= 10;
            while(exponent)
            {
                expsize++;
                backwardsexp[expsize] = (exponent % 10) + '0';
                exponent /= 10;
            }
            i += 3;
            for(j = expsize; j; j--)
            {
                i++;
                dp[i] = backwardsexp[j];
            }
            dp[0] = i;
            break;
        case FixedDecimal:
            if(sp->sig[1] == '0')
            {
                beforedecimal = 0;
                sp->sig[0] = 0;
            }
            else
            {
                beforedecimal = sp->sig[0] + sp->exp;
            }
            afterdecimal = (-sp->exp > digits) ? -sp->exp : digits;
            if(sp->sgn)
            {
                dp[1] = '-';
                i = 1;
            }
            else
                i = 0;
            if(beforedecimal <= 0)
            {
                i++;
                dp[i] = '0';
            }
            j = 1;
            while((j <= sp->sig[0]) && (beforedecimal > 0))
            {
                beforedecimal--;
                i++;
                dp[i] = sp->sig[j];
                j++;
            }
            while(beforedecimal > 0)
            {
                beforedecimal--;
                i++;
                dp[i] = '0';
            }
            if(j <= sp->sig[0])
            {
                i++;
                dp[i] = '.';
            }
            afterdecimal = 0;
            while((beforedecimal < 0)
                  && (!digits || (afterdecimal < digits)))
            {
                afterdecimal++;
                beforedecimal++;
                i++;
                dp[i] = '0';
            }
            while(j <= sp->sig[0])
            {
                afterdecimal++;
                i++;
                dp[i] = sp->sig[j];
                j++;
            }
            if((afterdecimal == 0) && digits > 0)
            {
                i++;
                dp[i] = '.';
            }
            for(; afterdecimal < digits; afterdecimal++)
            {
                i++;
                dp[i] = '0';
            }
            dp[0] = i;
            break;
#if !defined(NDEBUG)
        default:
            gui_abort();
            break;
#endif /* NDEBUG */
    };
}

static void C_ROMlib_Fxstr2dec(Decstr volatile sp2, GUEST<INTEGER> *volatile sp,
                                  Decimal *volatile dp2, Byte *volatile dp,
                                  INTEGER lastchar)
{
    int index, expsgn, implicitexp;

    index = *sp;
    warning_floating_point("xstr2dec(\"%.*s\")",
                           lastchar - index + 1, (const char *)sp2 + index);
    while(((sp2[index] == ' ') || (sp2[index] == '\t')) && (index <= lastchar))
        index++;
    if(sp2[index] == '-')
    {
        dp2->sgn = 1;
        index++;
    }
    else if(sp2[index] == '+')
    {
        dp2->sgn = 0;
        index++;
    }
    else
        dp2->sgn = 0;

    dp2->sig[0] = 0;
    while(isdigit(sp2[index]) && (index <= lastchar))
    {
        dp2->sig[0]++;
        dp2->sig[dp2->sig[0]] = sp2[index];
        index++;
    }
    implicitexp = dp2->sig[0];
    if(sp2[index] == '.')
    {
        index++;
        while(isdigit(sp2[index]) && (index <= lastchar))
        {
            dp2->sig[0]++;
            dp2->sig[dp2->sig[0]] = sp2[index];
            index++;
        }
    }
    if(!dp2->sig[0])
    {
        dp2->sig[0] = 0;
        dp2->sig[1] = 'N';
        /*-->*/ goto abortlookahead; /* should I use a break or return instead? */
    }
    *sp = index; /* The base is legit.  Check exponent. */
    dp2->exp = 0;
    if(LOWER(sp2[index]) == 'e')
    {
        index++;
        if(sp2[index] == '-')
        {
            expsgn = 1;
            index++;
        }
        else if(sp2[index] == '+')
        {
            expsgn = 0;
            index++;
        }
        else
            expsgn = 0;
        if(!isdigit(sp2[index]))
        {
            /*-->*/ goto abortlookahead; /* should I use a break or return instead? */
        }
        while(isdigit(sp2[index]) && (index <= lastchar))
        {
            INTEGER newexp = dp2->exp;
            newexp *= 10;
            newexp += sp2[index] - '0';
            dp2->exp = newexp;
            index++;
        }
        if(expsgn)
            dp2->exp = -1 * dp2->exp;
    }
    *sp = index;
abortlookahead:
    dp2->exp = dp2->exp + implicitexp - dp2->sig[0];
    *dp = !sp2[index] || (index > lastchar);
    while(dp2->sig[0] > 1 && dp2->sig[1] == '0') /* gunch leading */
        memmove(dp2->sig + 1, dp2->sig + 2, --dp2->sig[0]); /* zeros */

    warning_floating_point("xstr2dec returning %s%.*s * 10**%d",
                           dp2->sgn ? "-" : "",
                           dp2->sig[0], dp2->sig + 1, toHost(dp2->exp));
}

void Executor::C_ROMlib_Fcstr2dec(Decstr volatile sp2, GUEST<INTEGER> *volatile sp,
                                  Decimal *volatile dp2, Byte *volatile dp)
{
    C_ROMlib_Fxstr2dec(sp2, sp, dp2, dp, 32767);
}

void Executor::C_ROMlib_Fpstr2dec(Decstr volatile sp2, GUEST<INTEGER> *volatile sp,
                                  Decimal *volatile dp2, Byte *volatile dp)
{
    C_ROMlib_Fxstr2dec(sp2, sp, dp2, dp, sp2[0]);
}

