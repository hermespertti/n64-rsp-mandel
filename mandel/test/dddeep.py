#!/usr/bin/env python3
# Stage S host model: DD (hi,lo) view center + complex-DD reference orbit +
# plain-double pixel deltas, verified against a Decimal(60) oracle.
# This is the exact arithmetic the ROM will run (Dekker two_prod/two_sum).
from decimal import Decimal as D, getcontext
import sys
getcontext().prec=60

SPLIT=134217729.0  # 2^27+1
def split(a):
    t=a*SPLIT
    ah=t-(t-a)
    return ah,a-ah
def two_prod(a,b):
    p=a*b
    ah,al=split(a); bh,bl=split(b)
    return p,((ah*bh-p)+ah*bl+al*bh)-al*bl
def two_sum(a,b):
    s=a+b
    bb=s-a
    return s,(a-(s-bb))+(b-bb)

def dd_sqr(xh,xl):
    ph,pl=two_prod(xh,xh)
    pl+=2.0*xh*xl
    return two_sum(ph,pl)

def add_dd(ah,al,bh,bl):
    th,te=two_sum(ah,bh); te+=al+bl
    return two_sum(th,te)

def model(span,IT,W=24,H=18,cxy=None):
    if cxy: cd,cid=cxy
    else:
        cd=D("0.2825570234941095832911021559400860119212")
        cid=D("0.010005571509958194316882151419115636630895")
    ch=float(cd); cyh=float(cid)
    clo=float(cd-D(repr(ch))); cyl=float(cid-D(repr(cyh)))
    # complex DD reference orbit: z^2 + C
    rh=rl=ih=il=0.0
    ref=[]; esc=0
    for k in range(IT):
        # real: rh^2 - ih^2 (+errors) + C
        ar,br=two_prod(rh,rh); br+=2.0*rh*rl   # keep 2*rh*rl cross term
        ai,bi=two_prod(ih,ih); bi+=2.0*ih*il
        # cross terms 2*rh*il, 2*rl*ih folded into imag low
        xr,xl=two_prod(rh,ih)
        rr,er=add_dd(ar,br,-ai,-bi)
        rr,er=add_dd(rr,er,ch,clo)
        ii,ei=add_dd(2.0*xr,2.0*xl,0.0,2.0*(rh*il+rl*ih))
        ii,ei=add_dd(ii,ei,cyh,cyl)
        rh,rl=rr,er; ih,il=ii,ei
        ref.append((rh,rl,ih,il))
        if not esc and rh*rh+ih*ih>=4.0: esc=k+1
    def esc_true(c,ci):  # c,ci are Decimals (exact center + exact offset)
        z=(D(0),D(0))
        for k in range(IT):
            if z[0]*z[0]+z[1]*z[1]>=D(4): return k+1
            z=(z[0]*z[0]-z[1]*z[1]+c,2*z[0]*z[1]+ci)
        return 0
    dx=span/W; dy=span/H
    pat=tot=0; bad=0; escn=0
    for i in range(H):
        for j in range(W):
            cdx=dx*(j-W/2+0.5); cdy=dy*(i-H/2+0.5)
            drx=dri=0.0; cnt=0
            for k in range(IT):
                rh,rl,ih,il=ref[k]
                odx,odi=drx,dri        # old d (both components)
                # t=2R+d (2*rl+2*il carried): t = (2rh+drx) + (2rl), (2ih+dri)+(2il)
                tx,te=two_sum(2*rh,odx)  # te ~ ulp residual; lo also += 2*rl
                tx_l=te+2*rl
                ty,te=two_sum(2*ih,odi); ty_l=te+2*il
                # d' = t*d + dC   (t=(tx,tx_l), d=(odx,odi)) — old d both sides
                ph,pl=two_prod(tx,odx)
                qh,ql=two_prod(ty,odi)
                sh,sl=add_dd(ph,pl,-qh,-ql)
                dd,dlo=add_dd(sh,sl,cdx,0.0)
                dd,dlo=add_dd(dd,dlo,tx_l*odx-ty_l*odi,0.0)   # real cross errs
                nx,nlo=add_dd(dd,dlo,0.0,0.0)
                ph,pl=two_prod(tx,odi)
                qh,ql=two_prod(ty,odx)
                sh2,sl2=add_dd(ph,pl,qh,ql)
                ny,nlo2=add_dd(sh2,sl2,cdy,tx_l*odi+ty_l*odx)  # imag cross errs
                drx, dri = nx, ny      # store both AFTER products
                # escape: |z+d| with two_sum
                zh,_=two_sum(rh,drx); zi,_=two_sum(ih,dri)
                if zh*zh+zi*zi>=4.0: cnt=k+1; break
            ct=esc_true(cd+D(repr(cdx)),cid+D(repr(cdy)))  # exact DD center
            tot+=1
            ok=(cnt>0)==(ct>0)
            pat+=ok
            if ok and cnt and ct and abs(cnt-ct)>16: bad+=1
    print("span=%-8g IT=%d  DDref-esc@%-5s pattern %d/%d=%5.1f%%  iterdiff>16 %d"%(
        span,IT,esc,pat,tot,100*pat/tot,bad))

if __name__=="__main__":
    span=float(sys.argv[1]) if len(sys.argv)>1 else None
    if span: model(span,4600)
    else:
        for sp in (5e-15,1e-16,1e-17,1e-18):
            model(sp,4600)
