/* soup_ui.h - the upstream film picker on the 60D (soup_ui.c). GPL v2. */
#ifndef SOUP_UI_H
#define SOUP_UI_H

#define SOUP_UI_MAX 32

/* provided by soup.c */
int         soup_ui_count(void);
const char *soup_ui_name(int i);
const char *soup_ui_desc(int i);
int         soup_ui_current(void);
void        soup_ui_choose(int i);      /* select stock i and turn Film on */
int         soup_ui_film_on(void);

/* provided by soup_ui.c */
void         soup_ui_start(void);
void         soup_ui_open_from_menu(void);
unsigned int soup_ui_keypress(unsigned int key);

#endif
