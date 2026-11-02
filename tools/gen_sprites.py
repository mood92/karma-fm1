#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Draws the characters (the three synth tracks', the drummer, the wizard of the FX pages) and writes firmware/src/op_sprites.h: each pose a 112 x 104
picture (the drummer's smaller), 4 bits a pixel (a colour of OP_SPAL in ui_op.c), kept in flash.
  tools/gen_sprites.py [PREVIEW.png]     (the preview: every pose, 4x)
The pictures are made here, by code (ellipses, thick lines, arcs): original drawings, no artwork copied.
Needs Pillow."""
import math
import sys
from pathlib import Path

from PIL import Image, ImageDraw

W, H = 112, 104
T, INK, DIM, WHITE, COL, GREEN, ORANGE, BLUE, BLACK, SKIN = range(10)   # 0 = see-through; COL: the track's colour
RGB = {T: (12, 12, 14), INK: (142, 134, 201), DIM: (58, 50, 112), WHITE: (236, 236, 240), COL: (255, 0, 255),
       GREEN: (30, 204, 112), ORANGE: (255, 98, 26), BLUE: (40, 124, 255), BLACK: (0, 0, 0), SKIN: (92, 84, 150)}
OUT = Path(__file__).resolve().parent.parent / "firmware/src/op_sprites.h"


class Pic:
    def __init__(self, w=W, h=H):
        self.im = Image.new("P", (w, h), T)
        self.d = ImageDraw.Draw(self.im)

    def ell(self, cx, cy, rx, ry, fill=BLACK, out=INK, w=2):
        if out is not None:
            self.d.ellipse((cx - rx, cy - ry, cx + rx, cy + ry), fill=out)
            if fill is not None:
                self.d.ellipse((cx - rx + w, cy - ry + w, cx + rx - w, cy + ry - w), fill=fill)
        else:
            self.d.ellipse((cx - rx, cy - ry, cx + rx, cy + ry), fill=fill)

    def blob(self, parts, fill=BLACK, out=INK, w=2):          # ellipses that melt into one outline
        for cx, cy, rx, ry in parts:
            self.d.ellipse((cx - rx, cy - ry, cx + rx, cy + ry), fill=out)
        for cx, cy, rx, ry in parts:
            self.d.ellipse((cx - rx + w, cy - ry + w, cx + rx - w, cy + ry - w), fill=fill)

    def line(self, pts, c=INK, w=1):
        self.d.line(pts, fill=c, width=w)

    def tube(self, pts, w, fill=BLACK, out=INK, e=2):         # a limb: a thick line with an outline
        for c, ww in ((out, w + 2 * e), (fill, w)):
            self.d.line(pts, fill=c, width=ww)
            for x, y in pts:
                r = ww / 2
                self.d.ellipse((x - r, y - r, x + r, y + r), fill=c)

    def arc(self, cx, cy, rx, ry, a0, a1, c=INK, w=1):        # degrees, clockwise from 3 o'clock
        self.d.arc((cx - rx, cy - ry, cx + rx, cy + ry), a0, a1, fill=c, width=w)

    def dot(self, x, y, r, c):
        self.d.ellipse((x - r, y - r, x + r, y + r), fill=c)

    def poly(self, pts, fill=BLACK, out=INK):
        self.d.polygon(pts, fill=fill, outline=out)


def hand(p, x, y, r=5, fingers=None):
    p.ell(x, y, r, r, SKIN, INK, 1)
    for fx, fy in fingers or []:
        p.line([(x, y), (x + fx, y + fy)], INK, 2)


def buddha(lean):
    """The laughing Buddha as a sumo: bald, long ears, a wide grin, an open robe, beads, the belly. lean: he
    leans to the right of the picture, the left foot in the air."""
    p = Pic()
    dx = 7 if lean else 0
    cx = 56 + dx
    # legs first (behind the belly): a wide squat
    if lean:
        p.tube([(cx - 16, 80), (16, 66), (8, 82)], 13, SKIN)                 # the left leg: up, out to the side
        p.ell(6, 87, 9, 5, SKIN)
        p.tube([(cx + 16, 82), (96, 86), (95, 97)], 13, SKIN)
        p.ell(97, 100, 10, 4, SKIN)
    else:
        p.tube([(cx - 16, 82), (19, 85), (20, 97)], 13, SKIN)
        p.ell(18, 100, 10, 4, SKIN)
        p.tube([(cx + 16, 82), (93, 85), (92, 97)], 13, SKIN)
        p.ell(94, 100, 10, 4, SKIN)
    # the robe: open, off the shoulders, behind the body
    p.poly([(cx - 30, 40), (cx - 40, 78), (cx - 26, 90), (cx + 26, 90), (cx + 40, 78), (cx + 30, 40)], COL, INK)
    # the body: chest and belly as one
    p.blob([(cx, 47, 25, 15), (cx, 67, 30, 24)], SKIN)
    p.arc(cx - 11, 46, 11, 8, 20, 160)                                        # the chest
    p.arc(cx + 11, 46, 11, 8, 20, 160)
    p.arc(cx, 66, 22, 19, 25, 155)                                            # the belly's curve
    p.dot(cx, 72, 2, INK)                                                     # the navel
    p.arc(cx, 72, 5, 4, 200, 340)
    p.d.chord((cx - 31, 62, cx + 31, 93), 22, 158, fill=COL, outline=INK)     # the belt, its knot
    p.d.rectangle((cx - 5, 84, cx + 5, 97), fill=COL, outline=INK)
    p.line([(cx, 86), (cx, 96)], INK)
    # the robe's edges over the shoulders
    p.line([(cx - 22, 38), (cx - 30, 60), (cx - 27, 82)], COL, 3)
    p.line([(cx + 22, 38), (cx + 30, 60), (cx + 27, 82)], COL, 3)
    # the arms: fists on the thighs (leaning: one arm out for balance)
    if lean:
        p.tube([(cx - 25, 44), (cx - 40, 54), (cx - 48, 44)], 9, SKIN)
        hand(p, cx - 50, 40, 5)
    else:
        p.tube([(cx - 25, 44), (cx - 38, 60), (cx - 30, 76)], 9, SKIN)
        hand(p, cx - 29, 79, 5)
    p.tube([(cx + 25, 44), (cx + 38, 60), (cx + 30, 76)], 9, SKIN)
    hand(p, cx + 29, 79, 5)
    # the beads
    for i in range(9):
        a = math.radians(20 + i * 17.5)
        p.dot(cx + 17 * math.cos(a), 36 + 13 * math.sin(a), 2, ORANGE)
    # the head: bald and round, the ears' long lobes
    hx = cx + (3 if lean else 0)
    p.ell(hx - 17, 25, 4, 10, SKIN)
    p.ell(hx + 17, 25, 4, 10, SKIN)
    p.ell(hx, 20, 16, 17, SKIN)
    p.arc(hx, 12, 9, 5, 200, 340)                                             # the shine of the bald head
    p.dot(hx, 12, 1, COL)                                                     # the mark on the forehead
    p.arc(hx - 7, 18, 5, 4, 200, 340, WHITE, 2)                               # eyes shut, laughing
    p.arc(hx + 7, 18, 5, 4, 200, 340, WHITE, 2)
    p.arc(hx - 7, 13, 6, 3, 200, 340)                                         # the brows
    p.arc(hx + 7, 13, 6, 3, 200, 340)
    p.dot(hx, 22, 1, INK)
    p.d.chord((hx - 9, 18, hx + 9, 33), 10, 170, fill=WHITE, outline=INK)     # the grin
    p.line([(hx - 6, 28), (hx + 6, 28)], INK)
    p.arc(hx - 11, 25, 3, 3, 90, 270)                                         # the cheeks
    p.arc(hx + 11, 25, 3, 3, 270, 90)
    return p.im


def bassist(lean):
    """The bass player (lane 1): a round head in a cap and shades, a big grin, a bass slung across the body, its long neck
    up to the left. lean: he leans into it (the body to the right, one knee up)."""
    p = Pic()
    dx = 6 if lean else 0
    cx = 56 + dx
    # the legs and the shoes
    p.tube([(cx - 10, 78), (cx - 14 - (6 if lean else 0), 92), (cx - 16, 98)], 11, SKIN)
    p.tube([(cx + 10, 78), (cx + 14, 92), (cx + 16, 98)], 11, SKIN)
    p.ell(cx - 18, 100, 10, 4, SKIN)
    p.ell(cx + 18, 100, 10, 4, SKIN)
    # the body: a jacket in the lane's colour
    p.blob([(cx, 56, 22, 14), (cx, 68, 20, 14)], BLACK, COL, 2)
    p.line([(cx, 45), (cx, 80)], COL, 2)
    # the head
    hx = cx + (3 if lean else 0)
    p.ell(hx - 17, 27, 3, 5, SKIN)
    p.ell(hx + 17, 27, 3, 5, SKIN)
    p.ell(hx, 25, 16, 16, SKIN)
    p.d.chord((hx - 17, 6, hx + 17, 30), 180, 360, fill=COL, outline=INK)     # the cap
    p.d.rounded_rectangle((hx + 4, 17, hx + 24, 21), 2, fill=COL, outline=INK)   # its peak
    p.d.rounded_rectangle((hx - 13, 24, hx + 13, 31), 3, fill=BLACK, outline=INK)  # the shades
    p.line([(hx, 24), (hx, 25)], INK)
    p.d.chord((hx - 8, 28, hx + 8, 44), 10, 170, fill=WHITE, outline=INK)     # the grin
    # the bass: a body low on the right, a long neck up to the left, strings, pickups
    p.blob([(cx + 12, 66, 16, 11), (cx + 24, 70, 12, 9)], ORANGE, INK, 2)
    p.tube([(cx + 6, 62), (cx - 28, 36), (cx - 46, 22)], 6, SKIN, INK, 1)     # the neck
    p.ell(cx - 50, 19, 4, 5, ORANGE, INK, 1)                                  # the head stock
    p.line([(cx + 12, 66), (cx - 46, 21)], WHITE, 1)
    p.line([(cx + 14, 68), (cx - 44, 24)], WHITE, 1)
    p.d.rectangle((cx + 10, 64, cx + 14, 70), fill=BLACK)
    p.d.rectangle((cx + 18, 66, cx + 22, 72), fill=BLACK)
    # the arms: the left on the neck, the right plucking
    p.tube([(cx - 20, 50), (cx - 32, 44), (cx - 32, 37)], 8, SKIN)
    hand(p, cx - 32, 35, 5)
    p.tube([(cx + 20, 52), (cx + 28, 62), (cx + 18, 68)], 8, SKIN)
    hand(p, cx + 16, 69, 5)
    return p.im


def gorilla(up):
    """The gorilla at its congas (the drums are the scene's): a high skull, a heavy brow, a wide muzzle, long
    thick arms. up: the left hand in the air, open; else both hands down on the drums."""
    p = Pic()
    cx = 56
    p.ell(40, 100, 11, 4, SKIN)                                               # the feet
    p.ell(72, 100, 11, 4, SKIN)
    p.tube([(44, 84), (40, 97)], 11, SKIN)
    p.tube([(68, 84), (72, 97)], 11, SKIN)
    p.blob([(cx, 56, 31, 18), (cx, 72, 25, 22)], SKIN)                        # the shoulders and the body
    p.ell(cx, 70, 15, 16, BLACK, COL, 2)                                      # the chest
    p.arc(cx - 7, 62, 7, 6, 20, 160, COL)
    p.arc(cx + 7, 62, 7, 6, 20, 160, COL)
    p.dot(cx, 80, 1, COL)
    for x, y, ddx, ddy in ((27, 46, -4, -4), (32, 42, -3, -5), (85, 46, 4, -4), (80, 42, 3, -5),
                           (30, 78, -4, 3), (82, 78, 4, 3)):                 # fur
        p.line([(x, y), (x + ddx, y + ddy)], INK, 1)
    # the arms
    if up:
        p.tube([(28, 54), (11, 44), (14, 22)], 13, SKIN)
        hand(p, 14, 15, 7, [(-7, -6), (-3, -9), (2, -9), (7, -5)])
    else:
        p.tube([(28, 54), (9, 60), (18, 70)], 13, SKIN)
        hand(p, 21, 74, 7, [(-2, 8), (3, 8), (7, 6)])
    p.tube([(84, 54), (103, 60), (94, 70)], 13, SKIN)
    hand(p, 91, 74, 7, [(2, 8), (-3, 8), (-7, 6)])
    # the head
    p.ell(cx - 19, 26, 4, 5, SKIN)                                            # ears
    p.ell(cx + 19, 26, 4, 5, SKIN)
    p.blob([(cx, 26, 18, 17), (cx, 15, 12, 13)], SKIN)                        # the skull, its crest
    for i in range(-2, 3):                                                    # the hair on top
        p.line([(cx + i * 4, 4), (cx + i * 5, 0)], INK, 1)
    p.d.rounded_rectangle((cx - 15, 17, cx + 15, 23), 3, fill=INK)            # the brow
    p.ell(cx - 7, 26, 4, 3, WHITE, INK, 1)                                    # the eyes
    p.ell(cx + 7, 26, 4, 3, WHITE, INK, 1)
    p.dot(cx - 6, 26, 1, BLACK)
    p.dot(cx + 6, 26, 1, BLACK)
    p.ell(cx, 37, 13, 9, BLACK, INK, 2)                                       # the muzzle
    p.dot(cx - 3, 33, 1.5, INK)
    p.dot(cx + 3, 33, 1.5, INK)
    p.arc(cx, 35, 9, 7, 20, 160, INK, 2)                                      # a grin
    return p.im


def saucer():
    """The flying saucer (PLAITS), 96 x 52: a glass dome with its pilot, a wide hull, a darker belly, three feet.
    The lights on its rim are the scene's (SAUCER_LIGHTS below: where they are)."""
    p = Pic(96, 52)
    p.line([(48, 6), (48, 1)], INK)                                           # an aerial
    p.dot(48, 1, 1.5, COL)
    p.d.chord((29, 4, 67, 44), 180, 360, fill=BLACK, outline=INK)             # the dome
    p.arc(48, 24, 15, 16, 200, 250, WHITE)
    p.ell(48, 15, 8, 7, SKIN, INK, 1)                                         # the pilot: a big head, big eyes
    p.ell(44, 14, 3, 2, WHITE, None)
    p.ell(52, 14, 3, 2, WHITE, None)
    p.dot(45, 14, 0.6, BLACK)
    p.dot(51, 14, 0.6, BLACK)
    for x in (30, 48, 66):                                                    # the feet
        p.line([(x, 40), (x + (x - 48) // 5, 50)], INK, 2)
        p.dot(x + (x - 48) // 5, 50, 1.5, INK)
    p.ell(48, 38, 27, 8, DIM, INK, 2)                                         # the belly
    p.ell(48, 30, 47, 10, SKIN, INK, 2)                                       # the hull
    p.arc(48, 27, 40, 6, 10, 170, INK)
    p.arc(48, 27, 21, 5, 180, 360, COL, 2)                                    # the dome's collar
    for x, y in SAUCER_LIGHTS:
        p.dot(x, y, 2.5, BLACK)
    return p.im


SAUCER_LIGHTS = [(12, 31), (24, 34), (36, 35), (48, 36), (60, 35), (72, 34), (84, 31)]


def wizard():
    """The wizard of the FX pages, turned to the right: a robe with stars, a long white beard, a tall hat with its
    tip bent and a star on it (the track's colour), an arm out with the wand: its tip is the picture's (110, 44)."""
    p = Pic()
    p.poly([(30, 52), (14, 100), (20, 103), (76, 103), (82, 100), (64, 52)], DIM, INK)       # the robe
    p.line([(18, 96), (78, 96)], INK)
    for x, y, c in ((28, 84, WHITE), (44, 90, ORANGE), (62, 82, WHITE), (36, 70, ORANGE), (58, 68, WHITE), (50, 78, COL)):
        p.line([(x - 2, y), (x + 2, y)], c)                                                  # its stars
        p.line([(x, y - 2), (x, y + 2)], c)
    p.ell(28, 102, 8, 2, SKIN, INK, 1)                                                       # shoes
    p.ell(66, 102, 8, 2, SKIN, INK, 1)
    p.tube([(34, 56), (24, 70), (28, 80)], 8, DIM)                                           # the near arm, its hand
    hand(p, 29, 83, 4)
    p.tube([(60, 56), (76, 58), (88, 50)], 8, DIM)                                           # the arm with the wand
    hand(p, 90, 49, 4)
    p.line([(88, 50), (110, 44)], WHITE, 2)
    p.ell(48, 42, 13, 13, SKIN)                                                              # the head
    p.poly([(36, 44), (40, 62), (48, 82), (56, 62), (60, 44), (54, 50), (48, 47), (42, 50)], WHITE, INK)   # the beard
    p.line([(44, 56), (47, 72)], INK)
    p.line([(52, 56), (49, 72)], INK)
    p.ell(53, 46, 3, 2, SKIN, INK, 1)                                                        # a nose
    p.dot(44, 41, 1.5, WHITE)
    p.dot(53, 41, 1.5, WHITE)
    p.dot(45, 41, 0.6, BLACK)
    p.dot(54, 41, 0.6, BLACK)
    p.line([(40, 37), (47, 38)], WHITE, 2)                                                   # brows
    p.line([(51, 38), (58, 37)], WHITE, 2)
    p.poly([(33, 34), (44, 6), (52, 2), (60, 1), (54, 6), (63, 34)], DIM, INK)               # the hat, its tip bent
    p.ell(48, 34, 22, 4, DIM, INK, 1)
    p.line([(36, 28), (60, 28)], COL, 2)
    p.arc(46, 18, 4, 4, 60, 300, WHITE, 1)                                                   # a moon on it
    for i in range(5):                                                                       # the star at its tip
        a = math.radians(i * 72 - 90)
        p.line([(62, 2), (62 + 5 * math.cos(a), 2 + 5 * math.sin(a))], COL, 1)
    p.dot(62, 2, 1.5, COL)
    return p.im


def drummer(mouth):
    """The drummer behind the kit (64 x 52): headphones, a cap, a grin; mouth: open (on the kick). The arms and
    the sticks are the scene's, from the shoulders (14, 36) and (50, 36)."""
    p = Pic(64, 52)
    p.poly([(10, 36), (4, 52), (60, 52), (54, 36), (40, 30), (24, 30)], COL, INK)            # the shirt: black, its
    p.poly([(12, 38), (7, 52), (57, 52), (52, 38), (40, 32), (24, 32)], BLACK, None)         # edge in the track's colour
    p.arc(32, 31, 8, 6, 0, 180, COL)
    p.tube([(32, 26), (32, 31)], 6, SKIN, INK, 1)
    p.ell(32, 16, 13, 13, SKIN)                                                              # the head
    p.d.chord((19, 2, 45, 22), 180, 360, fill=DIM, outline=INK)                              # a cap, its peak
    p.d.rounded_rectangle((36, 10, 50, 13), 1, fill=DIM, outline=INK)
    p.arc(32, 14, 16, 15, 190, 350, COL, 2)                                                  # the headphones
    p.d.rounded_rectangle((13, 11, 19, 23), 2, fill=COL, outline=INK)
    p.d.rounded_rectangle((45, 11, 51, 23), 2, fill=COL, outline=INK)
    p.ell(27, 17, 2, 2, WHITE, None)
    p.ell(37, 17, 2, 2, WHITE, None)
    p.dot(27, 17, 0.6, BLACK)
    p.dot(37, 17, 0.6, BLACK)
    if mouth:
        p.ell(32, 23, 3, 3, BLACK, INK, 1)
    else:
        p.arc(32, 20, 5, 4, 20, 160, WHITE, 1)
    return p.im


POSES = [("WIZARD", wizard()), ("DRUMMER", drummer(False)), ("DRUMMER_HIT", drummer(True)), ("BUDDHA", bassist(False)), ("BUDDHA_LEAN", bassist(True)), ("GORILLA", gorilla(False)),
         ("GORILLA_UP", gorilla(True)), ("SAUCER", saucer())]


def small(im, box, w, h):
    """A small copy of a picture's box, w x h (w even): each of its pixels the colour that weighs most in its
    patch of the large one; outlines and the bright details count for more than the fills, so they survive."""
    WEIGHT = {INK: 1.6, WHITE: 3.0, COL: 2.2, ORANGE: 2.4, GREEN: 2.2, BLUE: 2.0, BLACK: 0.9, SKIN: 1.0, DIM: 1.0}
    x0, y0, x1, y1 = box
    src = im.load()
    out = Image.new("P", (w, h), T)
    dst = out.load()
    for y in range(h):
        for x in range(w):
            ax, bx = x0 + (x1 - x0) * x / w, x0 + (x1 - x0) * (x + 1) / w
            ay, by = y0 + (y1 - y0) * y / h, y0 + (y1 - y0) * (y + 1) / h
            score, n, solid = {}, 0, 0
            for sy in range(int(ay), max(int(ay) + 1, int(math.ceil(by)))):
                for sx in range(int(ax), max(int(ax) + 1, int(math.ceil(bx)))):
                    if 0 <= sx < im.size[0] and 0 <= sy < im.size[1]:
                        v = src[sx, sy]
                        n += 1
                        if v != T:
                            solid += 1
                            score[v] = score.get(v, 0) + WEIGHT[v]
            if n and solid * 3 >= n:
                dst[x, y] = max(score, key=score.get)
    return out


def buddha_m():
    """The Buddha for the mixer, 40 x 36: drawn small, not shrunk (the face would be lost)."""
    p = Pic(40, 36)
    for sx in (-1, 1):
        p.tube([(20 + sx * 8, 27), (20 + sx * 14, 29), (20 + sx * 14, 33)], 3, SKIN, INK, 1)
        p.ell(20 + sx * 15, 34, 4, 1.5, SKIN, INK, 1)
    p.blob([(20, 17, 9, 5), (20, 23, 12, 9)], SKIN, INK, 1)
    p.d.chord((8, 20, 32, 33), 25, 155, fill=COL)
    p.dot(20, 24, 0.6, INK)
    for sx in (-1, 1):
        p.tube([(20 + sx * 9, 17), (20 + sx * 14, 22), (20 + sx * 11, 27)], 2, SKIN, INK, 1)
    for i in range(6):
        a = math.radians(15 + i * 30)
        p.dot(20 + 7 * math.cos(a), 12.5 + 4 * math.sin(a), 1, ORANGE)
    buddha_face(p, 20, 7)
    return p.im


def buddha_face(p, cx, cy):
    p.ell(cx - 7, cy + 1, 1.5, 4, SKIN, INK, 1)
    p.ell(cx + 7, cy + 1, 1.5, 4, SKIN, INK, 1)
    p.ell(cx, cy, 6.5, 7, SKIN, INK, 1)
    p.arc(cx - 3, cy - 0.5, 2, 2, 200, 340, WHITE)
    p.arc(cx + 3, cy - 0.5, 2, 2, 200, 340, WHITE)
    p.d.chord((cx - 4, cy - 1, cx + 4, cy + 5), 10, 170, fill=WHITE)
    p.dot(cx, cy - 4.5, 0.6, COL)


def gorilla_face(p, cx, cy):
    p.ell(cx - 7, cy, 1.5, 2.5, SKIN, INK, 1)
    p.ell(cx + 7, cy, 1.5, 2.5, SKIN, INK, 1)
    p.blob([(cx, cy, 6.5, 6.5), (cx, cy - 4.5, 4.5, 4.5)], SKIN, INK, 1)
    p.d.rectangle((cx - 5, cy - 4, cx + 5, cy - 3), fill=INK)
    p.d.rectangle((cx - 4, cy - 2, cx - 2, cy - 1), fill=WHITE)
    p.d.rectangle((cx + 2, cy - 2, cx + 4, cy - 1), fill=WHITE)
    p.d.point((cx - 3, cy - 1), fill=BLACK)
    p.d.point((cx + 3, cy - 1), fill=BLACK)
    p.ell(cx, cy + 3.5, 4.5, 3.2, BLACK, INK, 1)
    p.d.point((cx - 1, cy + 2), fill=INK)
    p.d.point((cx + 1, cy + 2), fill=INK)
    p.arc(cx, cy + 3, 2.5, 2, 20, 160, INK)


def gorilla_m():
    p = Pic(40, 36)
    for sx in (-1, 1):
        p.tube([(20 + sx * 5, 28), (20 + sx * 6, 33)], 3, SKIN, INK, 1)
        p.ell(20 + sx * 7, 34, 4, 1.5, SKIN, INK, 1)
    p.blob([(20, 19, 11, 6), (20, 24, 9, 8)], SKIN, INK, 1)
    p.ell(20, 24, 5, 5, BLACK, COL, 1)
    for sx in (-1, 1):
        p.tube([(20 + sx * 10, 18), (20 + sx * 16, 23), (20 + sx * 14, 28)], 4, SKIN, INK, 1)
        p.ell(20 + sx * 13, 30, 2.5, 2.5, SKIN, INK, 1)
    gorilla_face(p, 20, 9)
    return p.im


def head(face):
    p = Pic(18, 16)
    face(p, 9, 8 if face is buddha_face else 9)
    return p.im


_P = dict(POSES)
# for the mixer's podiums (about 36 pixels high) and HOME's lanes (16: the heads; the saucer whole). The Buddha and
# the gorilla are drawn at that size; the saucer and the drummer shrink well
SMALL = [("BUDDHA_MX", small(_P["BUDDHA"], (8, 4, 104, 104), 40, 36)), ("GORILLA_MX", gorilla_m()),
         ("SAUCER_MX", small(_P["SAUCER"], (0, 0, 96, 52), 40, 22)), ("DRUMMER_MX", small(_P["DRUMMER"], (0, 0, 64, 52), 36, 29)),
         ("BUDDHA_HM", small(_P["BUDDHA"], (36, 4, 76, 44), 18, 16)), ("GORILLA_HM", head(gorilla_face)),
         ("SAUCER_HM", small(_P["SAUCER"], (0, 0, 96, 52), 22, 12)), ("DRUMMER_HM", small(_P["DRUMMER"], (10, 0, 54, 30), 22, 15))]
POSES += SMALL


def main():
    L = ["/* generated by tools/gen_sprites.py: the synth tracks' characters, 4 bits a pixel (OP_SPAL in ui_op.c),",
         " * the first pixel of a byte in its low half. Drawn by that script: original pictures. */", "#pragma once",
         f"#define OP_SPR_W {W}", f"#define OP_SPR_H {H}"]
    for name, im in POSES:
        px = list(im.getdata())
        if im.size != (W, H):
            L += [f"#define OP_SPR_{name}_W {im.size[0]}", f"#define OP_SPR_{name}_H {im.size[1]}"]
        by = [px[i] | (px[i + 1] << 4) for i in range(0, len(px), 2)]
        L.append(f"static const uint8_t OP_SPR_{name}[{len(by)}] = {{")
        for i in range(0, len(by), 28):
            L.append("    " + ",".join(str(b) for b in by[i:i + 28]) + ",")
        L.append("};")
    L.append("static const uint8_t OP_SAUCER_LIGHTS[%d][2] = {%s};" % (len(SAUCER_LIGHTS), ", ".join("{%d, %d}" % q for q in SAUCER_LIGHTS)))
    OUT.write_text("\n".join(L) + "\n")
    print(OUT, sum(im.size[0] * im.size[1] // 2 for _, im in POSES), "B")
    if len(sys.argv) > 1:
        s = 4
        s = 3
        sheet = Image.new("RGB", (5 * (W * s + 12) + 12, 2 * (H * s + 12) + 12), (30, 30, 34))
        sm = Image.new("RGB", (len(SMALL) * 180, 160), (30, 30, 34))
        for i, (name, im) in enumerate(SMALL):
            rgb = Image.new("RGB", im.size)
            col = [(40, 124, 255), (30, 204, 112), (236, 236, 240), (255, 98, 26)][i % 4]
            rgb.putdata([col if v == COL else RGB[v] for v in im.getdata()])
            sm.paste(rgb.resize((im.size[0] * 4, im.size[1] * 4), Image.NEAREST), (8 + i * 180, 8))
        sm.save(sys.argv[1].replace(".png", "-small.png"))
        for i, (name, im) in enumerate(POSES[:len(POSES) - len(SMALL)]):
            rgb = Image.new("RGB", im.size)
            col = (255, 98, 26) if i < 3 else {0: (40, 124, 255), 1: (30, 204, 112), 2: (236, 236, 240)}[(i - 3) // 2]
            if not i:
                col = (40, 124, 255)
            rgb.putdata([col if v == COL else RGB[v] for v in im.getdata()])
            k = i + 1 if i else 0                    # (the wizard alone in its column)
            sheet.paste(rgb.resize((im.size[0] * s, im.size[1] * s), Image.NEAREST),
                        (12 + (k // 2) * (W * s + 12), 12 + (k % 2) * (H * s + 12)))
        sheet.save(sys.argv[1])


if __name__ == "__main__":
    main()
