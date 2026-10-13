# portable core as an IDF component. tools/preview builds the same files on PC with MSVC
idf_component_register(SRCS "app.c" "map.c" "store.c" "gfx.c" "font.c" "ui.c" "ui_radar.c" "ui_views.c"
                            "ui_round.c" "ui_paper.c" "paper.c"
                            "font_small.c" "font_medium.c" "font_large.c" "font_huge.c"
                            "font_paper_s.c" "font_paper_m.c" "font_paper_l.c" "font_paper_xl.c" "font_paper_xxl.c"
                       INCLUDE_DIRS ".")
target_compile_options(${COMPONENT_LIB} PRIVATE -O2 -ffast-math)
