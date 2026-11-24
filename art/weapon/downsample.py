from PIL import Image
import numpy as np, sys
import os
HERE=os.path.dirname(os.path.abspath(__file__))
BODY=os.path.join(HERE,"breathless_induction_cannon_body.png")
ORB=os.path.join(HERE,"orb-canvas.png")
def load(p):
    im=Image.open(p).convert('RGBA')
    # pad/crop to body canvas 1672x941 centered
    c=Image.new('RGBA',(1672,941),(0,0,0,0)); c.paste(im,((1672-im.width)//2,(941-im.height)//2)); return np.array(c).astype(float)
def down(a,W,H,hard):
    h,w=a.shape[:2]; out=np.zeros((H,W,4))
    fx=w/W; fy=h/H
    for j in range(H):
        for i in range(W):
            x0,x1=i*fx,(i+1)*fx; y0,y1=j*fy,(j+1)*fy
            cell=a[int(y0):int(np.ceil(y1)),int(x0):int(np.ceil(x1))]
            ix0=int(x0+fx*0.25); ix1=max(ix0+1,int(x1-fx*0.25)); iy0=int(y0+fy*0.25); iy1=max(iy0+1,int(y1-fy*0.25))
            inner=a[iy0:iy1,ix0:ix1].reshape(-1,4)
            if hard:
                cov=(cell[:,:,3]>128).mean()
                if cov<0.5: continue
                op=inner[inner[:,3]>128]
                if len(op)==0: op=cell.reshape(-1,4); op=op[op[:,3]>128]
                out[j,i,:3]=np.median(op[:,:3],axis=0); out[j,i,3]=255
            else:
                al=inner[:,3]; m=al.mean()
                if m<16: continue
                out[j,i,:3]=(inner[:,:3]*al[:,None]).sum(0)/al.sum(); out[j,i,3]=min(255,m*1.0)
    return out.round().clip(0,255).astype(np.uint8)
for f in map(float,sys.argv[1:]):
    W=round(1672/f); H=round(941/f)
    b=down(load(BODY),W,H,True); o=down(load(ORB),W,H,False)
    Image.fromarray(b).save(f'body_{W}x{H}.png'); Image.fromarray(o).save(f'orb_{W}x{H}.png')
    bg=Image.new('RGBA',(W,H),(60,50,70,255)); bg.alpha_composite(Image.fromarray(b)); bg.alpha_composite(Image.fromarray(o))
    bg.resize((W*6,H*6),Image.NEAREST).save(f'preview_{W}x{H}.png'); print(W,H)
