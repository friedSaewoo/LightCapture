"""Author the embedded pen cursor using only Python's standard library.
The .cur is committed as a build asset; Python is not needed to run the app.
"""
from pathlib import Path
import struct
import zlib

LAYERS = [
    ([(2, 30), (5, 18), (21, 2), (30, 11), (14, 27)], (255, 255, 255)),
    ([(4, 28), (7, 18), (21, 4), (28, 11), (13, 26)], (24, 31, 43)),
    ([(8, 18), (21, 5.5), (26.5, 11), (14, 23.5)], (37, 99, 235)),
    ([(10, 18), (22, 6), (23.5, 7.5), (11.5, 19.5)], (147, 197, 253)),
    ([(4, 28), (7.5, 19), (13, 24.5)], (242, 221, 181)),
    ([(4, 28), (5.5, 23.5), (8.5, 26.5)], (24, 31, 43)),
    ([(19, 7.5), (21, 5.5), (26.5, 11), (24.5, 13)], (226, 232, 240)),
]

def inside(poly, x, y):
    result = False
    prev = poly[-1]
    for point in poly:
        ax, ay = prev; bx, by = point
        if (ay > y) != (by > y) and x < (bx-ax)*(y-ay)/(by-ay)+ax:
            result = not result
        prev = point
    return result

def render(n, layers=LAYERS):
    output = bytearray()
    samples = 4
    for y in range(n):
        for x in range(n):
            total = [0, 0, 0]; count = 0
            for sy in range(samples):
                for sx in range(samples):
                    xx = (x+(sx+.5)/samples)*32/n
                    yy = (y+(sy+.5)/samples)*32/n
                    color = None
                    for poly, fill in layers:
                        if inside(poly, xx, yy): color = fill
                    if color:
                        count += 1
                        for i in range(3): total[i] += color[i]
            output.extend([*(round(v/count) if count else 0 for v in total), round(255*count/(samples*samples))])
    return output

def dib(n, rgba):
    color = bytearray(); mask = bytearray()
    stride = ((n+31)//32)*4
    for y in reversed(range(n)):
        row = bytearray(stride)
        for x in range(n):
            r,g,b,a = rgba[(y*n+x)*4:(y*n+x+1)*4]
            color.extend((b,g,r,a))
            if not a: row[x//8] |= 0x80 >> (x%8)
        mask.extend(row)
    header = struct.pack('<IiiHHIIiiII',40,n,n*2,1,32,0,len(color)+len(mask),0,0,0,0)
    return header+color+mask

def main():
    root = Path(__file__).resolve().parent
    sizes = [32,48,64,96]
    def write(name,layers,hotspot):
        images = [dib(n,render(n,layers)) for n in sizes]
        header = struct.pack('<HHH',0,2,len(sizes)); entries = bytearray(); offset=6+16*len(sizes)
        for n,data in zip(sizes,images):
            entries.extend(struct.pack('<BBBBHHII',n,n,0,0,round(hotspot[0]*n/32),round(hotspot[1]*n/32),len(data),offset))
            offset += len(data)
        (root/name).write_bytes(header+entries+b''.join(images))
    write('pen.cur',LAYERS,(4,28))
    write('eraser.cur',[
        ([(2,25),(18,4),(30,13),(20,29),(5,29)],(255,255,255)),
        ([(4,25),(18,6),(28,14),(18,27),(7,27)],(24,31,43)),
        ([(5.5,24.5),(13,14),(23,22),(17,26),(7.5,26)],(226,232,240)),
        ([(14,13),(18.5,7.5),(26.5,14.5),(23.5,20.5)],(249,168,212)),
    ],(4,25))
    write('marker.cur',[
        ([(2,30),(3,23),(11,13),(19,3),(31,13),(21,23),(10,29)],(255,255,255)),
        ([(4,28),(5,23),(12,14),(19,5),(29,13),(20,21),(9,27)],(24,31,43)),
        ([(6,25),(11,19),(17,23),(10,26)],(255,214,0)),
        ([(12,16),(20,7),(27,13),(19,21)],(255,214,0)),
        ([(15,15),(21,9),(23,11),(17,17)],(255,247,160)),
    ],(4,28))
    write('mosaic.cur',[
        ([(0,0),(10,0),(10,2),(4,2),(4,8),(2,8),(2,4),(0,4)],(255,255,255)),
        ([(2,0),(4,0),(4,10),(2,10)],(24,31,43)),
        ([(0,2),(10,2),(10,4),(0,4)],(24,31,43)),
        ([(9,9),(31,9),(31,31),(9,31)],(255,255,255)),
        ([(10,10),(30,10),(30,30),(10,30)],(24,31,43)),
        *[( [(11+x*6,11+y*6),(16+x*6,11+y*6),(16+x*6,16+y*6),(11+x*6,16+y*6)],
             (148,163,184) if (x+y)%2 else (226,232,240)) for y in range(3) for x in range(3)],
    ],(3,3))

if __name__ == '__main__': main()
