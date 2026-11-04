# Black: black backgrounds, white text and borders, greys in between. No hues.
# A palette is plain shell variables; theme/render.sh puts them into the
# templates in theme/ (${name} with '#').

name="Black"
dark=1                      # 1 = dark GTK apps, 0 = light

# Base surfaces, darkest to lightest
bg="#000000"                # windows, bar, terminal
bg_alt="#0a0a0a"            # sidebars, inputs
surface="#1a1a1a"           # hover, separators
selection="#333333"         # selected rows, text selection
muted="#4a4a4a"             # placeholders, inactive borders
dim="#707070"               # inactive text

# Text
subtext="#a6a6a6"           # secondary text (bar status)
fg="#ffffff"                # main text
fg_bright="#ffffff"         # emphasised text, cursor

# Accents: white (text on accent fills is black)
accent="#ffffff"            # active workspace, borders, focus
accent_bright="#ffffff"     # prompt, highlights, matches
accent_soft="#d0d0d0"       # match on selected row
urgent="#ffffff"            # errors, urgent (shown as white on black, like active)
urgent_soft="#bdbdbd"
warn="#d9d9d9"
warn_soft="#bfbfbf"
good="#9e9e9e"

# Terminal opacity (alacritty) and popup opacity (fuzzel/mako, hex alpha)
term_opacity="0.95"
popup_alpha="f2"

# 16 ANSI colours, as greys of different brightness so they still differ
color0="#1a1a1a";  color8="#5c5c5c"
color1="#b8b8b8";  color9="#d6d6d6"
color2="#8f8f8f";  color10="#adadad"
