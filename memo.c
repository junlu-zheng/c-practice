#include <gtk/gtk.h>
#include <stdio.h>

#define MEMO_FILE "memo.txt"

static GtkWidget *text_view;

static void save_memo(void) {
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(text_view));
    GtkTextIter start, end;

    gtk_text_buffer_get_bounds(buffer, &start, &end);

    char *text = gtk_text_buffer_get_text(buffer, &start, &end, FALSE);

    FILE *fp = fopen(MEMO_FILE, "w");
    if (fp == NULL) {
        g_printerr("Could not save memo.\n");
        g_free(text);
        return;
    }

    fputs(text, fp);
    fclose(fp);
    g_free(text);
}

static void load_memo(void) {
    FILE *fp = fopen(MEMO_FILE, "r");
    if (fp == NULL) {
        return;
    }

    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    rewind(fp);

    if (size < 0) {
        fclose(fp);
        return;
    }

    char *content = g_malloc(size + 1);
    size_t read_size = fread(content, 1, size, fp);
    content[read_size] = '\0';

    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(text_view));
    gtk_text_buffer_set_text(buffer, content, -1);

    g_free(content);
    fclose(fp);
}

static void on_save_clicked(GtkWidget *widget, gpointer data) {
    (void)widget;
    (void)data;

    save_memo();
    g_print("Memo saved.\n");
}

static void on_window_destroy(GtkWidget *widget, gpointer data) {
    (void)widget;
    (void)data;

    save_memo();
    gtk_main_quit();
}

int main(int argc, char *argv[]) {
    gtk_init(&argc, &argv);

    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "Floating Memo");
    gtk_window_set_default_size(GTK_WINDOW(window), 320, 420);
    gtk_window_set_keep_above(GTK_WINDOW(window), TRUE);
    gtk_container_set_border_width(GTK_CONTAINER(window), 10);

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_container_add(GTK_CONTAINER(window), box);

    GtkWidget *label = gtk_label_new("Memo / To-do List");
    gtk_box_pack_start(GTK_BOX(box), label, FALSE, FALSE, 0);

    GtkWidget *scrolled_window = gtk_scrolled_window_new(NULL, NULL);
    gtk_box_pack_start(GTK_BOX(box), scrolled_window, TRUE, TRUE, 0);

    text_view = gtk_text_view_new();
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(text_view), GTK_WRAP_WORD_CHAR);
    gtk_container_add(GTK_CONTAINER(scrolled_window), text_view);

    GtkWidget *save_button = gtk_button_new_with_label("Save");
    gtk_box_pack_start(GTK_BOX(box), save_button, FALSE, FALSE, 0);

    g_signal_connect(save_button, "clicked", G_CALLBACK(on_save_clicked), NULL);
    g_signal_connect(window, "destroy", G_CALLBACK(on_window_destroy), NULL);

    load_memo();

    gtk_widget_show_all(window);
    gtk_main();

    return 0;
}
