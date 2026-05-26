"""RGB image to dual bitplane conversion for 3-color e-paper (black/white/red)."""

from PIL import Image

from ..config import DISPLAY_WIDTH, DISPLAY_HEIGHT, PLANE_SIZE


def rgb_to_bitplanes(img: Image.Image) -> tuple[bytes, bytes]:
    """Convert 400x300 RGB PIL Image to (black_plane, red_plane) bytes.

    Color mapping for this display:
        White -> PLANE_0 bit=1, PLANE_1 bit=0
        Black -> PLANE_0 bit=0, PLANE_1 bit=0
        Red   -> PLANE_0 bit=0, PLANE_1 bit=1
    """
    assert img.size == (DISPLAY_WIDTH, DISPLAY_HEIGHT)
    if img.mode != "RGB":
        img = img.convert("RGB")

    pixels = img.load()
    black_plane = bytearray([0xFF] * PLANE_SIZE)
    red_plane = bytearray(PLANE_SIZE)

    for y in range(DISPLAY_HEIGHT):
        for x in range(DISPLAY_WIDTH):
            r, g, b = pixels[x, y][:3]
            bit_index = y * DISPLAY_WIDTH + x
            byte_index = bit_index >> 3
            bit_offset = 7 - (bit_index & 7)
            mask = 1 << bit_offset

            if r > 180 and g < 100 and b < 100:
                # Red: keep PLANE_0=1 (white base), set PLANE_1
                red_plane[byte_index] |= mask
            elif r < 128 and g < 128 and b < 128:
                # Black: clear PLANE_0
                black_plane[byte_index] &= ~mask
            # else: white, PLANE_0 stays 1

    return bytes(black_plane), bytes(red_plane)
