"""Bundle only the image formats accepted by the LCD image library."""

hiddenimports = [
    "PIL.BmpImagePlugin",
    "PIL.JpegImagePlugin",
    "PIL.PngImagePlugin",
    "PIL.WebPImagePlugin",
]
