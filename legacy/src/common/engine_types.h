/* engine_types.h: binary structures that more than one feature DLL needs.
 *
 * Target: WMAIN.EXE (Star Wars Episode I: The Phantom Menace, Big Ape "Obi" engine, 1999),
 * 32-bit, ImageBase 0x400000, MD5 7c5af8428c19b17cca09ae3a49bd10ef.
 *
 * Only structures used by two or more features live here. A layout that only one feature reads
 * stays in that feature's own header, where the reverse-engineering note that justifies it can
 * sit next to it.
 */
#ifndef COMMON_ENGINE_TYPES_H
#define COMMON_ENGINE_TYPES_H

#include <stddef.h>
#include <stdint.h>

/* Every offset, every pointer patch and every operand repoint in this project assumes a 32-bit
 * process. A 64-bit build would compile and then write garbage. */
_Static_assert(sizeof(void *) == 4,
    "The standalone engine fixes require a 32-bit build");

/* ---------------------------------------------------------------------------------------------
 * The swift widget record.
 *
 * A swift screen is pure .data: a tSwWidget[] terminated by type == -1, plus a bitmap-name table
 * and a font-name table. swmenu_build 0x45e7a3 walks the array once to the terminator, counts,
 * records the default/cancel widgets, stamps SWACTION_STATIC onto the terminator slot and stores
 * the pointer. It allocates nothing per widget.
 *
 * The 0x38 stride is byte-proven everywhere the engine walks such an array.
 * Field meanings that differ per widget class are noted per field.
 * ------------------------------------------------------------------------------------------- */
typedef struct sw_rect {
    int32_t x;
    int32_t y;
    int32_t width;
    int32_t height;
} sw_rect_t;

typedef struct sw_widget {
    int32_t   type;        /* SW_* below; -1 terminates the array                               */
    int32_t   id;          /* the key swmenu_findWidgetByMenu takes                             */
    int32_t   action;      /* SWACTION_* below                                                  */
    int32_t   visible;     /* 0 = no draw, no hit-test, no focus                                */
    int32_t   start;       /* SLIDER: notch count, TEXT: base string id                        */
    int32_t   state;       /* SLIDER: current notch, TEXT: string offset                       */
    int32_t   font_index;  /* SLIDER: the KNOB bitmap index, TEXT: font index (sign=highlight) */
    int32_t   parameter;   /* SLIDER: the GAUGE bitmap index, TEXT/PIC: a widget id to mirror  */
    sw_rect_t rect;        /* x, y, w, h in 640x480 MENU SPACE                                  */
    void     *link;        /* TEXT: the ALIGNMENT (0 centre, 1 left, 2 right, 3 left+vcentre)   */
    void     *data;        /* TEXT: a literal string that OVERRIDES the string table            */
} sw_widget_t;

_Static_assert(sizeof(sw_widget_t) == 0x38, "Unexpected sw_widget_t size");
_Static_assert(offsetof(sw_widget_t, rect) == 0x20, "Unexpected sw_widget_t rect offset");
_Static_assert(offsetof(sw_widget_t, link) == 0x30, "Unexpected sw_widget_t link offset");
_Static_assert(offsetof(sw_widget_t, data) == 0x34, "Unexpected sw_widget_t data offset");

#define SW_TYPE_PIC          0
#define SW_TYPE_TEXT         1
#define SW_TYPE_3D           2
#define SW_TYPE_CHECKBOX     3
#define SW_TYPE_SLIDER       4
#define SW_TYPE_LISTBOX      5
#define SW_TYPE_EDIT         6
#define SW_TYPE_SCROLLPIC    7
#define SW_TYPE_TERMINATOR (-1)

#define SW_ACTION_DEFAULT   (-9)
#define SW_ACTION_CANCEL   (-19)
#define SW_ACTION_STATIC   (-29)  /* skipped by focus AND by the hit-test */
#define SW_ACTION_SELECT   (-39)

#endif /* COMMON_ENGINE_TYPES_H */
