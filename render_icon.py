import ctypes, sys
from ctypes import c_void_p, c_int, c_double, c_char_p, POINTER, Structure, byref

svg, out, size = sys.argv[1], sys.argv[2], int(sys.argv[3])
cairo = ctypes.CDLL("libcairo.so.2")
rsvg = ctypes.CDLL("librsvg-2.so.2")

class RsvgRectangle(Structure):
    _fields_ = [("x", c_double), ("y", c_double), ("width", c_double), ("height", c_double)]

for f, res, args in [
    (cairo.cairo_image_surface_create, c_void_p, [c_int, c_int, c_int]),
    (cairo.cairo_create, c_void_p, [c_void_p]),
    (cairo.cairo_set_source_rgb, None, [c_void_p, c_double, c_double, c_double]),
    (cairo.cairo_paint, None, [c_void_p]),
    (cairo.cairo_surface_write_to_png, c_int, [c_void_p, c_char_p]),
    (cairo.cairo_destroy, None, [c_void_p]),
    (cairo.cairo_surface_destroy, None, [c_void_p]),
    (rsvg.rsvg_handle_new_from_file, c_void_p, [c_char_p, POINTER(c_void_p)]),
    (rsvg.rsvg_handle_get_intrinsic_size_in_pixels, c_int, [c_void_p, POINTER(c_double), POINTER(c_double)]),
    (rsvg.rsvg_handle_render_document, c_int, [c_void_p, c_void_p, POINTER(RsvgRectangle), POINTER(c_void_p)]),
]:
    f.restype = res; f.argtypes = args

err = c_void_p()
h = rsvg.rsvg_handle_new_from_file(svg.encode(), byref(err))
assert h, "failed to load svg"
w, hh = c_double(), c_double()
rsvg.rsvg_handle_get_intrinsic_size_in_pixels(h, byref(w), byref(hh))
print("svg size", w.value, hh.value)

surf = cairo.cairo_image_surface_create(0, size, size)  # ARGB32
cr = cairo.cairo_create(surf)
cairo.cairo_set_source_rgb(cr, 0.05, 0.06, 0.08)   # near-black background
cairo.cairo_paint(cr)

pad = 0.06
tw = size * (1 - 2 * pad)
scale = tw / w.value
th = hh.value * scale
rect = RsvgRectangle((size - tw) / 2, (size - th) / 2, tw, th)
ok = rsvg.rsvg_handle_render_document(h, cr, byref(rect), byref(err))
assert ok, "render failed"
cairo.cairo_destroy(cr)
assert cairo.cairo_surface_write_to_png(surf, out.encode()) == 0
cairo.cairo_surface_destroy(surf)
print("wrote", out)
