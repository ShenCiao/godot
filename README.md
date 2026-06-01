# Ciallo custom godot

Main changes:
- Pen stylus subpixel coordinate
- Touch as individual event, separated from mouse event on Windows
- Add CGAL Arrangement2D
- Allow nested CanvasGroup nodes, cut children's Z-Index 
(Note: This part is almost fully vibed. Although I know what is modified in rendering, I'm not capable to maintain/modify code manually)

# License
AGPLv3 since using CGAL.