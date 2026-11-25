import json
import pathlib

import numpy as np
from manim import *

HERE = pathlib.Path(__file__).parent
TRACE = json.loads((HERE / "trace.json").read_text())
TRACE2 = json.loads((HERE / "trace2.json").read_text())

BG = "#12161c"
DEL_C = "#ff6b6b"
INS_C = "#69db7c"
KEPT_C = "#8a949e"
OUT_C = "#dee2e6"
IN_C = "#ffa94d"
OPT_C = "#ffd43b"
ALT_C = "#4dabf7"
DIM_C = "#495057"
BOX_C = "#343b45"
MONO = "monospace"

config.background_color = BG

S = 0.62             # tiny-example lattice spacing
X0, Y0 = -5.2, 2.55  # tiny-example cell (0,0) center
BOX_W = 0.56

def node(i, j):
    return np.array([X0 + j * S, Y0 - i * S, 0])


def fmt(x):
    return str(int(x)) if abs(x - round(x)) < 1e-9 else f"{x:g}"


def glyph(ch):
    return "␣" if ch == " " else ch


STEP_COLOR = {"MOVE": KEPT_C, "CROSS": KEPT_C, "DELETE": DEL_C, "CHANGE": DEL_C,
              "ENTER": IN_C, "TYPE": IN_C, "EXIT": IN_C, "LEADING": OUT_C}


def fill_grid(scene, tables, point, size, font_size, i0=0, j0=0):
    content = VGroup()
    for col in range(len(tables["out"][0])):
        arrivals = []
        for row in range(len(tables["out"])):
            i, j = i0 + row, j0 + col
            for table in ("out", "in"):
                cell = tables[table][row][col]
                if cell is None:
                    continue
                direction = UP if table == "out" else DOWN
                center = point(i, j) + direction * size / 4
                label = Text(fmt(cell["cost"]), font=MONO, font_size=font_size,
                             color=OUT_C if table == "out" else IN_C).move_to(center)
                mark = VGroup(label)
                step = cell["step"]
                if step != "LEADING":
                    if "pi" in cell:
                        back = normalize(point(cell["pi"], cell["pj"]) - point(i, j))
                    else:
                        back = {"DELETE": UP, "EXIT": DOWN, "ENTER": LEFT, "TYPE": LEFT}[step]
                    start = center + LEFT * size * 0.33
                    mark.add(Line(start, start + back * size * 0.17,
                                  color=STEP_COLOR[step], stroke_width=1.7))
                content.add(mark)
                arrivals.append(FadeIn(mark))
        if arrivals:
            scene.play(LaggedStart(*arrivals, lag_ratio=0.025), run_time=0.48)
    return content


class DPWalkthrough(Scene):
    def construct(self):
        A = TRACE["example"]["initial"]
        B = TRACE["example"]["goal"]
        n, m = len(A), len(B)
        penalty, esc = TRACE["penalty"], TRACE["esc"]

        caption = VGroup()

        def say(text, size=26, wait=0.0):
            new = Text(text, font_size=size, color=OUT_C, line_spacing=0.9)
            new.to_edge(DOWN, buff=0.35)
            if len(caption):
                self.play(FadeOut(caption[0]), run_time=0.2)
                caption.remove(caption[0])
            caption.add(new)
            self.play(FadeIn(new, shift=0.08 * UP), run_time=0.35)
            self.wait(max(wait, min(2.4, 0.020 * len(text))))

        # ---- Act 1: the problem -------------------------------------------
        title = Text("How vimficiency partitions a change", font_size=30, color=OUT_C)
        title.to_edge(UP, buff=0.4)
        src = Text(A, font=MONO, font_size=44, color=OUT_C)
        dst = Text(B, font=MONO, font_size=44, color=OUT_C)
        arrow = Arrow(LEFT * 0.6, RIGHT * 0.6, color=KEPT_C, stroke_width=4)
        VGroup(src, arrow, dst).arrange(RIGHT, buff=0.5).shift(UP * 0.8)
        self.play(FadeIn(title), Write(src), run_time=1.2)
        self.play(GrowArrow(arrow), Write(dst), run_time=1.0)

        def span_rect(text_mobj, lo, hi, color):
            return SurroundingRectangle(
                VGroup(*text_mobj[lo:hi]), color=color, buff=0.06, stroke_width=3
            )

        say("Plan 1: two edits — change aa, hop over b, change cc", wait=0.2)
        p1 = VGroup(
            span_rect(src, 0, 2, DEL_C), span_rect(src, 3, 5, DEL_C),
            span_rect(dst, 0, 2, INS_C), span_rect(dst, 3, 5, INS_C),
        )
        self.play(*map(Create, p1), run_time=0.9)
        self.wait(0.8)
        say("Plan 2: one edit — delete everything, retype everything", wait=0.2)
        p2 = VGroup(span_rect(src, 0, 5, DEL_C), span_rect(dst, 0, 5, INS_C))
        self.play(FadeOut(p1), *map(Create, p2), run_time=0.9)
        self.wait(0.8)
        say("Score both partitions using uniform key effort\nand a flat charge for each planned edit.", wait=1.0)
        self.play(FadeOut(p2), FadeOut(arrow), FadeOut(title))

        # ---- Act 2: the grid ----------------------------------------------
        boxes = VGroup()
        dividers = VGroup()
        in_tints = VGroup()
        for i in range(n + 1):
            for j in range(m + 1):
                b = RoundedRectangle(corner_radius=0.05, width=BOX_W, height=BOX_W,
                                     stroke_color=BOX_C, stroke_width=1.4)
                b.move_to(node(i, j))
                boxes.add(b)
                dividers.add(Line(node(i, j) + LEFT * (BOX_W / 2 - 0.05),
                                  node(i, j) + RIGHT * (BOX_W / 2 - 0.05),
                                  stroke_color=BOX_C, stroke_width=1.0))
                tint = Rectangle(width=BOX_W - 0.06, height=BOX_W / 2 - 0.05,
                                 stroke_width=0, fill_color=IN_C, fill_opacity=0.06)
                tint.move_to(node(i, j) + DOWN * (BOX_W / 4 - 0.005))
                in_tints.add(tint)
        col_labels = VGroup(*[
            Text(glyph(B[j]), font=MONO, font_size=24, color=INS_C)
            .move_to(node(0, j) + (S / 2) * RIGHT + 0.55 * UP)
            for j in range(m)
        ])
        row_labels = VGroup(*[
            Text(glyph(A[i]), font=MONO, font_size=24, color=DEL_C)
            .move_to(node(i, 0) + (S / 2) * DOWN + 0.55 * LEFT)
            for i in range(n)
        ])
        goal_tag = Text("goal (typed) →", font_size=20, color=INS_C)
        goal_tag.next_to(col_labels, UP, buff=0.15).align_to(col_labels, LEFT)
        init_tag = Text("initial ↓", font_size=20, color=DEL_C)
        init_tag.next_to(row_labels, UP, buff=0.3).align_to(row_labels, RIGHT)

        say("Lay both strings on a grid of cells.")
        self.play(
            src.animate.scale(24 / 44).move_to(row_labels.get_center()).set_opacity(0),
            dst.animate.scale(24 / 44).move_to(col_labels.get_center()).set_opacity(0),
            LaggedStart(*[Create(b) for b in boxes], lag_ratio=0.008),
            FadeIn(dividers), FadeIn(in_tints),
            FadeIn(col_labels), FadeIn(row_labels), FadeIn(goal_tag), FadeIn(init_tag),
            run_time=1.8,
        )
        self.remove(src, dst)

        say("At (i, j) the planner has consumed i source characters\nand produced j goal characters. Each half stores a score.")
        spot = SurroundingRectangle(boxes[3 * (m + 1) + 2], color=OUT_C, buff=0.03)
        self.play(Create(spot), run_time=0.6)
        self.wait(1.2)
        legend = VGroup(
            Text("top half: OUT · normal mode", font_size=20, color=OUT_C),
            Text("bottom half: IN · insert mode", font_size=20, color=IN_C),
        ).arrange(DOWN, aligned_edge=LEFT, buff=0.25).to_corner(UR, buff=0.6)
        say("Each cell holds two costs: normal mode on top,\ninsert mode below.", wait=0.4)
        self.play(FadeIn(legend), run_time=0.8)
        self.play(FadeOut(spot))

        # ---- Act 3: the moves ---------------------------------------------
        def arr(a, b, color):
            return Arrow(a, b, color=color, stroke_width=4, buff=0.28,
                         max_tip_length_to_length_ratio=0.18)

        def demo(mobjs, text, wait=1.6):
            say(text)
            self.play(*[Create(mo) if isinstance(mo, (Arrow, CurvedArrow, Line)) else FadeIn(mo) for mo in mobjs], run_time=0.8)
            self.wait(wait)
            self.play(*map(FadeOut, mobjs), run_time=0.4)

        d_arrow = arr(node(0, 0), node(2, 0), DEL_C)
        d_lbl = Text(f"de · {fmt(TRACE['demoDelete'])} keys", font=MONO, font_size=20, color=DEL_C).next_to(d_arrow, RIGHT, buff=0.1)
        demo([d_arrow, d_lbl], "DELETE moves down: counted Vim deletes (x, dw, dd, D …)\npriced by the tiling oracle.")

        t_arrow = arr(node(2, 0), node(2, 2), IN_C)
        t_lbl = Text(f"ixx · {fmt(TRACE['entry'])}+1+1 keys", font=MONO, font_size=20, color=IN_C).next_to(t_arrow, UP, buff=0.1)
        demo([t_arrow, t_lbl], "ENTER/TYPE moves right on the insert layer:\npay for i and each character. EXIT pays for <Esc>.")

        c_arrow = arr(node(0, 0), node(2, 1) + (BOX_W / 4) * DOWN, STEP_COLOR["CHANGE"])
        c_lbl = Text("cex · entry key free", font=MONO, font_size=20, color=STEP_COLOR["CHANGE"]).next_to(c_arrow, RIGHT, buff=0.1)
        demo([c_arrow, c_lbl], "CHANGE fuses them: the c-form deletes and lands in\ninsert mode directly — the entry key is never paid.")

        mv_arrow = arr(node(2, 2), node(4, 4), KEPT_C)
        mv_lbl = Text(f"w · {fmt(TRACE['demoMove'])} + {fmt(penalty)}", font=MONO, font_size=20, color=OUT_C).next_to(mv_arrow, RIGHT, buff=0.05).shift(0.1 * UP)
        demo([mv_arrow, mv_lbl], "Cross matching text on the diagonal. Pay for the motion\nand add the flat charge for the next region.")

        say(f"The first region starts at {fmt(penalty)}. Each half-cell records its\nbest score and a pointer to the step that reached it.", wait=1)
        fill_grid(self, TRACE["grid"], node, BOX_W, 13)
        self.play(FadeOut(legend))

        focus = TRACE["focus"]
        ring = SurroundingRectangle(boxes[5 * (m + 1) + 5], color=OPT_C, buff=0.03)
        title = Text("Normal mode at (5, 5)", font_size=24, color=OUT_C)
        rows = VGroup(title)
        for step, terms in (
            ("MOVE", (focus["move"], focus["moveSpan"], penalty)),
            ("DELETE", (focus["delete"], focus["deleteSpan"])),
            ("EXIT", (focus["exit"], esc)),
        ):
            formula = " + ".join(map(fmt, terms))
            rows.add(Text(f"{step:6}  {formula} = {fmt(sum(terms))}", font=MONO, font_size=20,
                          color=OPT_C if step == focus["winner"] else OUT_C))
        rows.arrange(DOWN, aligned_edge=LEFT, buff=0.25).to_corner(UR, buff=0.6)
        say("Three ways into this cell: cross the middle, delete a span,\nor leave insert mode. Here are their scores.")
        self.play(Create(ring), FadeIn(rows), run_time=1)
        self.wait(2)
        say("MOVE and EXIT tie on cost. EXIT reached this cell with fewer\nplanned regions, so its predecessor is the one retained.", wait=2)
        self.play(FadeOut(ring), FadeOut(rows))

        say("Read back from the goal. Each pointer identifies the previous\ncell and mode on the chosen path.")
        path = TRACE["path"]

        def cell_position(p):
            half = UP if p["table"] == "out" else DOWN
            return node(p["i"], p["j"]) + half * BOX_W / 4

        for last, prev in zip(reversed(path[1:]), reversed(path[:-1])):
            self.play(Create(Line(cell_position(last), cell_position(prev),
                                  color=OPT_C, stroke_width=5)), run_time=0.32)

        plan, split = TRACE["plan"], TRACE["split"]
        edit_cost = split["delete"] + split["type"] + 2 * esc
        two_cost = edit_cost + split["move"] + 2 * penalty
        tally = VGroup(
            Text("One planned edit", font_size=23, color=OPT_C),
            Text(f"delete {fmt(plan['delete'])} + insert {fmt(plan['insert'])} + region {fmt(penalty)}",
                 font=MONO, font_size=18),
            Text(f"score  {fmt(plan['cost'])}", font=MONO, font_size=24, color=OPT_C),
        ).arrange(DOWN, aligned_edge=LEFT, buff=0.18).to_corner(UR, buff=0.6)
        self.play(FadeIn(tally))
        self.wait(2)
        comparison = VGroup(
            Text("Keep the middle, edit both ends", font_size=22, color=ALT_C),
            Text(f"edits {fmt(edit_cost)} + move {fmt(split['move'])} + regions {fmt(2 * penalty)}",
                 font=MONO, font_size=18),
            Text(f"score  {fmt(two_cost)}", font=MONO, font_size=24, color=ALT_C),
        ).arrange(DOWN, aligned_edge=LEFT, buff=0.18).next_to(tally, DOWN, buff=0.5, aligned_edge=LEFT)
        say("Price the two-edit idea separately. Retyping the short middle\nsaves a move, an insert exit, and a second region charge.")
        self.play(FadeIn(comparison))
        self.wait(2.4)

        # ---- Sealing the large example --------------------------------------
        say("Same job at real size — still just main().", wait=0.2)
        self.play(*[FadeOut(mo) for mo in self.mobjects if mo is not caption[0]],
                  run_time=0.8)

        A2 = TRACE2["example"]["initial"]
        B2 = TRACE2["example"]["goal"]
        blocks2 = TRACE2["blocks"]
        N2, M2 = len(A2), len(B2)
        plan = TRACE2["plan"]
        SEAL_C = "#57c47a"

        adv = Text("mmmmmmmmmm", font=MONO, font_size=13).width / 10
        LH = 0.205

        def make_panel(sname):
            rows = {}
            grp = VGroup()
            for li, ln in enumerate(sname.split("\n")):
                if not ln.strip():
                    continue
                t = Text(ln, font=MONO, font_size=13, color=OUT_C)
                indent = len(ln) - len(ln.lstrip(" "))
                t.move_to(np.array([indent * adv, -li * LH, 0]), aligned_edge=UL)
                rows[li] = t
                grp.add(t)
            return grp, rows

        def line_slices(sname, lo, hi):
            starts = [0] + [k + 1 for k, c in enumerate(sname) if c == "\n"]
            out = []
            for li, ls in enumerate(starts):
                le = (starts[li + 1] - 1) if li + 1 < len(starts) else len(sname)
                a, b = max(lo, ls), min(hi, le)
                if a >= b:
                    continue
                gl = sum(1 for c in sname[ls:a] if not c.isspace())
                gh = gl + sum(1 for c in sname[a:b] if not c.isspace())
                if gh > gl:
                    out.append((li, gl, gh))
            return out

        src2, src_rows = make_panel(A2)
        dst2, dst_rows = make_panel(B2)
        panels = VGroup(src2, dst2).arrange(DOWN, buff=0.3, aligned_edge=LEFT)
        panels.to_corner(UR, buff=0.35)

        def span_boxes(sname, rows, lo, hi, color):
            return [SurroundingRectangle(VGroup(*rows[li][gl:gh]), color=color,
                                         buff=0.045, stroke_width=2)
                    for li, gl, gh in line_slices(sname, lo, hi)]

        marks2 = VGroup()
        for r in plan["regions"]:
            for bx in span_boxes(A2, src_rows, r["aBegin"], r["aEnd"], DEL_C):
                marks2.add(bx)
            for bx in span_boxes(B2, dst_rows, r["bBegin"], r["bEnd"], INS_C):
                marks2.add(bx)
        say("Real edits: a new line, 10 → n, and += → -= foo(…).")
        self.play(Write(src2), Write(dst2), run_time=1.6)
        self.play(*map(Create, marks2), run_time=0.9)
        self.wait(0.6)

        sealA0, sealA1 = blocks2[0]["aEnd"], blocks2[1]["aBegin"]
        sealB0, sealB1 = blocks2[0]["bEnd"], blocks2[1]["bBegin"]
        say("First sweep: every matched run is tested against the\nseal gate. This one is long enough — sealed.")
        seal_glyphs = VGroup(
            *[g for li, gl, gh in line_slices(A2, sealA0, sealA1) for g in src_rows[li][gl:gh]],
            *[g for li, gl, gh in line_slices(B2, sealB0, sealB1) for g in dst_rows[li][gl:gh]])
        self.play(seal_glyphs.animate.set_color(SEAL_C), run_time=0.9)
        self.wait(0.8)
        say("Shorter matched runs stay in play — retyping a few\nchars can beat a worst-case tiling cut.", wait=1.2)

        S3 = min(4.6 / (M2 + 1), 4.2 / (N2 + 1))
        GX, GY = -6.85, 3.15

        def node3(i, j):
            return np.array([GX + j * S3, GY - i * S3, 0])

        full_rect = DashedVMobject(
            Rectangle(width=(M2 + 1) * S3, height=(N2 + 1) * S3,
                      stroke_color=DIM_C, stroke_width=2),
            num_dashes=90)
        full_rect.move_to(node3(N2 / 2, M2 / 2))
        block_rects = VGroup()
        free_lines = VGroup()
        for b in blocks2:
            w = (b["bEnd"] - b["bBegin"] + 1) * S3
            h = (b["aEnd"] - b["aBegin"] + 1) * S3
            br = Rectangle(width=w, height=h, stroke_color=OUT_C, stroke_width=2,
                           fill_color=OUT_C, fill_opacity=0.04)
            br.move_to(node3((b["aBegin"] + b["aEnd"]) / 2, (b["bBegin"] + b["bEnd"]) / 2))
            block_rects.add(br)
            if b["lead"]:
                free_lines.add(DashedLine(node3(b["aBegin"], b["bBegin"]),
                                          node3(b["aBegin"] + b["lead"], b["bBegin"] + b["lead"]),
                                          color=INS_C, stroke_width=2, dash_length=0.05))
            if b["trail"]:
                free_lines.add(DashedLine(node3(b["aEnd"] - b["trail"], b["bEnd"] - b["trail"]),
                                          node3(b["aEnd"], b["bEnd"]),
                                          color=INS_C, stroke_width=2, dash_length=0.05))
        seal_line = Line(node3(sealA0, sealB0), node3(sealA1, sealB1),
                         color=SEAL_C, stroke_width=4)
        seal_lbl = Text("sealed", font_size=16, color=SEAL_C)
        seal_lbl.next_to(seal_line.get_center(), UR, buff=0.06)

        full_cells = (N2 + 1) * (M2 + 1)
        block_cells = sum((b["aEnd"] - b["aBegin"] + 1) * (b["bEnd"] - b["bBegin"] + 1)
                          for b in blocks2)
        say("Now the grid — the seal splits it,\nand only the blocks are ever built.")
        self.play(Create(full_rect), run_time=1.0)
        self.play(*map(Create, block_rects), Create(seal_line), FadeIn(seal_lbl),
                  *map(Create, free_lines), run_time=1.4)
        say(f"{full_cells:,} cells shrink to {block_cells:,} —\n"
            f"{100 - round(100 * block_cells / full_cells)}% eliminated before the DP runs.", wait=1.4)

        win = TRACE2["insetWin"]
        i0, i1, j0, j1 = win["i0"], win["i1"], win["j0"], win["j1"]
        S4 = 0.3
        IX, IY = 0.5, 2.2

        def node4(i, j):
            return np.array([IX + (j - j0) * S4, IY - (i - i0) * S4, 0])

        marker = Rectangle(width=(j1 - j0 + 1) * S3, height=(i1 - i0 + 1) * S3,
                           stroke_color=OPT_C, stroke_width=2)
        marker.move_to(node3((i0 + i1) / 2, (j0 + j1) / 2))
        inset_frame = Rectangle(width=(j1 - j0 + 2) * S4, height=(i1 - i0 + 2) * S4,
                                stroke_color=OPT_C, stroke_width=1.5)
        inset_frame.move_to(node4((i0 + i1) / 2, (j0 + j1) / 2))
        callout = VGroup(
            Line(marker.get_corner(UR), inset_frame.get_corner(UL), color=OPT_C, stroke_width=1.2),
            Line(marker.get_corner(DR), inset_frame.get_corner(DL), color=OPT_C, stroke_width=1.2))
        say("Focus on what remains: magnify block 1, right where\nthe leading diagonal meets the first edit.")
        self.play(FadeOut(panels), FadeOut(marks2), run_time=0.5)
        self.play(Create(marker), Create(inset_frame), Create(callout), run_time=1.0)

        inset_stuff = VGroup()
        ibox = VGroup(*[
            RoundedRectangle(corner_radius=0.03, width=S4 - 0.04, height=S4 - 0.04,
                             stroke_color=BOX_C, stroke_width=1).move_to(node4(i, j))
            for i in range(i0, i1 + 1) for j in range(j0, j1 + 1)])
        inset_stuff.add(ibox)
        self.play(FadeIn(ibox), run_time=0.6)

        say("Magnified cells show the same two mode costs\nand the predecessors retained inside this block.")
        inset_stuff.add(fill_grid(self, TRACE2["grid"], node4, S4, 8, i0, j0))
        self.wait(0.8)

        say("The rest fills the same way; one CROSS move rides the seal.")
        for k, b in enumerate(blocks2):
            if k:
                self.play(Indicate(seal_line, color=SEAL_C, scale_factor=1.05), run_time=0.7)
            bh = (b["aEnd"] - b["aBegin"]) * S3
            w = Rectangle(width=0.01, height=bh, stroke_width=0,
                          fill_color=OUT_C, fill_opacity=0.12)
            w.next_to(node3((b["aBegin"] + b["aEnd"]) / 2, b["bBegin"]), RIGHT, buff=0)
            self.add(w)
            self.play(w.animate.stretch_to_fit_width((b["bEnd"] - b["bBegin"]) * S3,
                                                     about_edge=LEFT), run_time=0.8)
            self.play(FadeOut(w), run_time=0.2)

        self.play(FadeOut(inset_stuff), FadeOut(inset_frame), FadeOut(callout),
                  FadeOut(marker), run_time=0.6)

        points = [node3(0, 0)]
        for region in plan["regions"]:
            points.extend([node3(region["aBegin"], region["bBegin"]),
                           node3(region["aEnd"], region["bBegin"]),
                           node3(region["aEnd"], region["bEnd"])])
        points.append(node3(N2, M2))
        partition = VMobject(color=OPT_C, stroke_width=3.5).set_points_as_corners(points)
        say("The yellow line follows the chosen partition across both blocks.\nEach edited region becomes one task for the search.")
        self.play(Create(partition), run_time=2.4)
        tally = VGroup(
            Text(f"{len(plan['regions'])} planned edits", color=OPT_C, font_size=29),
            Text(f"planner score  {fmt(plan['cost'])}", color=OUT_C, font=MONO, font_size=22),
        ).arrange(DOWN, buff=0.28).move_to(RIGHT * 3 + UP)
        self.play(FadeIn(tally))
        say("The optimizer now searches for Vim commands that realize\nthese edits, using the keyboard effort model.", size=26)
        self.wait(2.4)
