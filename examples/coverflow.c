/**
 * @file sgl/examples/coverflow.c
 * @brief CoverFlow widget example: stacked horizontal card gallery
 *
 * Interaction:
 *  - drag left / right: cards follow the finger with a live scale effect,
 *    release snaps to the nearest card (flick = inertia glide)
 *  - tap a side card: it slides to the center
 *  - tap the centered card: its own event callback fires (a label below
 *    shows the picked album and an info msgbox pops up with its details)
 *  - keyboard: LEFT / RIGHT / UP / DOWN arrows scroll card by card (no focus
 *    needed, the widget is a main menu); ENTER activates the centered card,
 *    running its callback and popping up the same info msgbox
 */

#include <sgl.h>

/* images converted from images/image1.png / images/image2.png (RGB565) */
extern const sgl_pixmap_t rect_pixmap_imgs[2];

/* number of cards: 2 images are reused for the last two cards */
#define COVERFLOW_EX_CARD_NUM             (5)

/* centered label that shows the tapped card name */
static sgl_obj_t *ex_pick_label = NULL;
static char ex_pick_buf[32];

/* card captions, reused by the info msgbox (msg_text stores the pointer,
 * so the strings must outlive the dialog) */
static const char *const ex_card_names[COVERFLOW_EX_CARD_NUM] = {
    "Image 1", "Plain", "Image 2", "Purple", "Orange",
};

/* body text of the info msgbox: a static buffer because sgl_msgbox_set_msg_text
 * keeps the pointer instead of copying the string */
static char ex_msg_buf[96];

/**
 * @brief pop up an info dialog describing the activated card
 * @param index activated card index
 * @return none
 * @note the dialog destroys itself when either button is pressed
 */
static void coverflow_show_msgbox(int16_t index)
{
    sgl_obj_t *msgbox = sgl_msgbox_create(NULL);
    if (msgbox == NULL) {
        return;
    }

    /* centered dialog, about one third of the screen */
    sgl_obj_set_size(msgbox, 360, 180);
    sgl_obj_set_pos_align(msgbox, SGL_ALIGN_CENTER);

    sgl_msgbox_set_color(msgbox, sgl_rgb(250, 250, 250));
    sgl_msgbox_set_border_color(msgbox, sgl_rgb(70, 74, 84));
    sgl_msgbox_set_border_width(msgbox, 2);
    sgl_msgbox_set_radius(msgbox, 8);
    sgl_msgbox_set_alpha(msgbox, 255);
    sgl_msgbox_set_font(msgbox, &consolas14);

    /* build the message from the card index + caption */
    const char *name = (index >= 0 && index < COVERFLOW_EX_CARD_NUM) ?
                       ex_card_names[index] : "Unknown";
    sgl_snprintf(ex_msg_buf, sizeof(ex_msg_buf),
                 "Card #%d activated!\nName: %s\n\nPress a button to close.",
                 (int)index, name);

    sgl_msgbox_set_title_text(msgbox, "CoverFlow");
    sgl_msgbox_set_title_text_color(msgbox, sgl_rgb(40, 44, 52));
    sgl_msgbox_set_title_height(msgbox, 32);
    sgl_msgbox_set_msg_text(msgbox, ex_msg_buf);
    sgl_msgbox_set_msg_text_color(msgbox, sgl_rgb(40, 44, 52));
    sgl_msgbox_set_msg_line_margin(msgbox, 4);
    sgl_msgbox_set_msg_x_offset(msgbox, 16);
    sgl_msgbox_set_msg_y_offset(msgbox, 12);

    /* Close on the left, OK on the right */
    sgl_msgbox_set_left_btn_text(msgbox, "Close");
    sgl_msgbox_set_left_btn_text_color(msgbox, SGL_COLOR_WHITE);
    sgl_msgbox_set_left_btn_color(msgbox, sgl_rgb(120, 120, 120));
    sgl_msgbox_set_right_btn_text(msgbox, "OK");
    sgl_msgbox_set_right_btn_text_color(msgbox, SGL_COLOR_WHITE);
    sgl_msgbox_set_right_btn_color(msgbox, sgl_rgb(0, 122, 255));
}

/**
 * @brief event callback of the centered card: show which card was activated
 * @note fired when the centered card is tapped, and also when ENTER is
 *       pressed (the widget forwards the key click to the center card)
 */
static void coverflow_card_event_cb(sgl_event_t *evt)
{
    if (evt->type != SGL_EVENT_CLICKED) {
        return;
    }

    /* the widget forwards clicks with the card as the event object */
    sgl_obj_t *coverflow = evt->obj->parent;
    int16_t index = -1;
    for (int16_t i = 0; i < COVERFLOW_EX_CARD_NUM; i++) {
        if (sgl_coverflow_get_card(coverflow, i) == evt->obj) {
            index = i;
            break;
        }
    }
    SGL_LOG_INFO("coverflow: card %d activated", index);

    if (ex_pick_label != NULL) {
        sgl_snprintf(ex_pick_buf, sizeof(ex_pick_buf), "Pick: #%d", index);
        sgl_label_set_text_fmt(ex_pick_label, "%s", ex_pick_buf);
    }

    /* popup an info dialog for the activated card */
    coverflow_show_msgbox(index);
}

/**
 * @brief create the coverflow example
 * @param parent parent object, NULL creates it on the active screen
 * @return none
 */
void sgl_coverflow_examples(sgl_obj_t *parent)
{
    static const sgl_coverflow_card_t cards[COVERFLOW_EX_CARD_NUM] = {
        { .color = sgl_rgb(0x2E, 0x86, 0xC1), .pixmap = &rect_pixmap_imgs[0], .text = "Image 1" },
        { .color = sgl_rgb(0x27, 0xAE, 0x60), .pixmap = NULL,                 .text = "Plain"   },
        { .color = sgl_rgb(0xB0, 0x3A, 0x2E), .pixmap = &rect_pixmap_imgs[1], .text = "Image 2" },
        { .color = sgl_rgb(0x7D, 0x3C, 0x98), .pixmap = NULL,                 .text = "Purple"  },
        { .color = sgl_rgb(0xB4, 0x77, 0x0E), .pixmap = NULL,                 .text = "Orange"  },
    };

    /* title */
    sgl_obj_t *title = sgl_label_create(parent);
    sgl_label_set_font(title, &consolas23);
    sgl_label_set_text(title, "CoverFlow Example");
    sgl_label_set_text_color(title, SGL_COLOR_BLACK);
    sgl_obj_set_pos(title, 320, 20);

    /* card gallery */
    sgl_obj_t *coverflow = sgl_coverflow_create(parent, cards, COVERFLOW_EX_CARD_NUM);
    if (coverflow == NULL) {
        return;
    }
    sgl_obj_set_pos(coverflow, 0, 80);
    sgl_obj_set_size(coverflow, 800, 280);
    sgl_coverflow_set_border(coverflow, SGL_COLOR_WHITE, 2);
    sgl_coverflow_set_font(coverflow, &consolas14);
    /* the caption is drawn below the card on the (white) background, so the
     * default white text would be invisible: use a dark color */
    sgl_coverflow_set_text_color(coverflow, SGL_COLOR_BLACK);

    /* click callbacks: when a card is centered and tapped, the widget
     * forwards the click to that card's own event callback. NOTE: only
     * set the event_fn field — making the cards clickable (e.g. with
     * sgl_obj_set_event_cb) would let them steal the click from the
     * coverflow widget, which must keep receiving events to work */
    for (int i = 0; i < COVERFLOW_EX_CARD_NUM; i++) {
        sgl_coverflow_get_card(coverflow, i)->event_fn = coverflow_card_event_cb;
    }

    /* start on the image card */
    sgl_coverflow_set_index(coverflow, 2);
}
