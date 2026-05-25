"""RGB image to dual bitplane conversion for 3-color e-paper (black/white/red)."""

from PIL import Image

from ..config import DISPLAY_WIDTH, DISPLAY_HEIGHT, PLANE_SIZE


def rgb_to_bitplanes(img: Image.Image) -> tuple[bytes, bytes]:
    """Convert 400x300 RGB PIL Image to (black_plane, red_plane) bytes.

    Color mapping:
        White (255,255,255) -> both planes 0
        Black (0,0,0)       -> PLANE_0 bit=1, PLANE_1 bit=0
        Red   (255,0,0)     -> PLANE_0 bit=0, PLANE_1 bit=1
    """
    assert img.size == (DISPLAY_WIDTH, DISPLAY_HEIGHT)
    if img.mode != "RGB":
        img = img.convert("RGB")

    pixels = img.load()
    black_plane = bytearray(PLANE_SIZE)
    red_plane = bytearray(PLANE_SIZE)

    for y in range(DISPLAY_HEIGHT):
        for x in range(DISPLAY_WIDTH):
            r, g, b = pixels[x, y][:3]
            bit_index = y * DISPLAY_WIDTH + x
            byte_index = bit_index >> 3
            bit_offset = 7 - (bit_index & 7)

            if r > 180 and g < 100 and b < 100:
                # Red pixel
                red_plane[byte_index] |= 1 << bit_offset
            elif r < 128 and g < 128 and b < 128:
                # Black pixel (dark)
                black_plane[byte_index] |= 1 << bit_offset
            # else: white, both planes stay 0

    return bytes(black_plane), bytes(red_plane)
