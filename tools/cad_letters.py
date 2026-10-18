# Block-letter generator for CAD signage: prints box strokes for a string, meant to sit inside
# a "for s = -1, 1, 2 ... end" loop (s = -1 mirrors it onto the back face).
# Usage: python3 tools/cad_letters.py "TEXT" centerY size strokeThickness faceZ
# Emit CAD box strokes for block letters. Cell 1 x 1.4 units; strokes as (x0,y0,x1,y1) segments.
import math, sys
G = {
 'M': [(0,0,0,1.4),(1,0,1,1.4),(0,1.4,0.5,0.7),(1,1.4,0.5,0.7)],
 'U': [(0,0.1,0,1.4),(1,0.1,1,1.4),(0,0,1,0)],
 'D': [(0,0,0,1.4),(0,1.4,0.7,1.4),(0,0,0.7,0),(1,0.25,1,1.15),(0.7,1.4,1,1.15),(0.7,0,1,0.25)],
 'B': [(0,0,0,1.4),(0,1.4,0.8,1.4),(0,0.7,0.8,0.7),(0,0,0.85,0),(0.95,0.8,0.95,1.3),(1,0.1,1,0.6)],
 'I': [(0.5,0,0.5,1.4),(0.15,1.4,0.85,1.4),(0.15,0,0.85,0)],
 'E': [(0,0,0,1.4),(0,1.4,1,1.4),(0,0.7,0.75,0.7),(0,0,1,0)],
 'S': [(0,1.4,1,1.4),(0,0.7,1,0.7),(0,0,1,0),(0,0.7,0,1.4),(1,0,1,0.7)],
 'O': [(0,0,0,1.4),(1,0,1,1.4),(0,1.4,1,1.4),(0,0,1,0)],
 'F': [(0,0,0,1.4),(0,1.4,1,1.4),(0,0.7,0.75,0.7)],
 'R': [(0,0,0,1.4),(0,1.4,0.9,1.4),(0,0.7,0.9,0.7),(1,0.75,1,1.35),(0.4,0.7,1,0)],
 'A': [(0,0,0,1.3),(1,0,1,1.3),(0,1.4,1,1.4),(0,0.7,1,0.7)],
 'P': [(0,0,0,1.4),(0,1.4,1,1.4),(0,0.7,1,0.7),(1,0.7,1,1.4)],
 'K': [(0,0,0,1.4),(0.05,0.7,1,1.4),(0.3,0.9,1,0)],
 '-': [(0.2,0.7,0.8,0.7)],
 ' ': [],
}
def emit(text, cx, cy, size, t, z, out):
    adv = 1.45
    n = len(text); width = (n*adv - 0.45)*size
    x0 = cx - width/2
    for i,ch in enumerate(text):
        for (a,b,c,d) in G[ch]:
            X0 = x0 + (i*adv + a)*size; Y0 = cy + (b-0.7)*size
            X1 = x0 + (i*adv + c)*size; Y1 = cy + (d-0.7)*size
            L = math.hypot(X1-X0, Y1-Y0) + t
            ang = math.degrees(math.atan2(Y1-Y0, X1-X0))
            mx, my = (X0+X1)/2, (Y0+Y1)/2
            out.append(f"  box size={L:.3f},{t:.3f},0.03 pos=s*{mx:.3f},{my:.3f}+post_h,s*{z} rot=0,0,s*{ang:.1f}")
out = []
emit(sys.argv[1], 0, float(sys.argv[2]), float(sys.argv[3]), float(sys.argv[4]), sys.argv[5], out)
print("\n".join(out))
