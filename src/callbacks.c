#include <sys/wait.h>
#include <stdint.h>
#include <gdk/gdkkeysyms.h>

#include "termit.h"
#include "configs.h"
#include "sessions.h"
#include "termit_core_api.h"
#include "lua_api.h"
#include "keybindings.h"
#include "callbacks.h"

static gboolean confirm_exit()
{
    if (gtk_notebook_get_n_pages(GTK_NOTEBOOK(termit.notebook)) <= 1)
        return FALSE;

    GtkWidget *dlg = gtk_message_dialog_new(
        GTK_WINDOW(termit.main_window), 
        GTK_DIALOG_MODAL, 
        GTK_MESSAGE_QUESTION,
        GTK_BUTTONS_YES_NO, _("Several tabs are opened.\nClose anyway?"));
    gint response = gtk_dialog_run(GTK_DIALOG(dlg));
    gtk_widget_destroy(dlg);
    if (response == GTK_RESPONSE_YES)
        return FALSE;
    else
        return TRUE;
}

gboolean termit_on_delete_event(GtkWidget *widget, GdkEvent *event, gpointer data)
{
    return confirm_exit();
}

void termit_on_destroy(GtkWidget *widget, gpointer data)
{
    termit_quit();
}

void termit_on_window_title_changed(VteTerminal *vte, gpointer user_data)
{
    gint page = gtk_notebook_get_current_page(GTK_NOTEBOOK(termit.notebook));
    TERMIT_GET_TAB_BY_INDEX(pTab, page);

    const char* title = vte_terminal_get_window_title(VTE_TERMINAL(pTab->vte));
    TRACE("old title: %s, new title: %s", pTab->title, title);
    if (pTab->title)
        g_free(pTab->title);
    pTab->title = g_strdup(title);
    termit_set_window_title(pTab->title);
}

void termit_on_toggle_scrollbar()
{
    TRACE_MSG(__FUNCTION__);
    gint page = gtk_notebook_get_current_page(GTK_NOTEBOOK(termit.notebook));
    TERMIT_GET_TAB_BY_INDEX(pTab, page);

    if (pTab->scrollbar_is_shown)
        gtk_widget_hide(GTK_WIDGET(pTab->scrollbar));
    else
        gtk_widget_show(GTK_WIDGET(pTab->scrollbar));
    pTab->scrollbar_is_shown = !pTab->scrollbar_is_shown;
}

void termit_on_child_exited()
{
    gint page = gtk_notebook_get_current_page(GTK_NOTEBOOK(termit.notebook));
    TERMIT_GET_TAB_BY_INDEX(pTab, page);

    TRACE("waiting for pid %d", pTab->pid);
    
    int status = 0;
    waitpid(pTab->pid, &status, WNOHANG);
    /* TODO: check wait return */    

    termit_close_tab();
}

gboolean termit_on_popup(GtkWidget *widget, GdkEvent *event)
{
    GtkMenu *menu = GTK_MENU(termit.menu);
    GdkEventButton *event_button;

    if (event->type == GDK_BUTTON_PRESS) {
        event_button = (GdkEventButton *) event;
        if (event_button->button == 3) {
            gtk_menu_popup (menu, NULL, NULL, NULL, NULL, 
                              event_button->button, event_button->time);
            return TRUE;
        }
    }

    return FALSE;
}

void termit_on_set_encoding(GtkWidget *widget, void *data)
{
    termit_set_encoding((const gchar*)data);
}

void termit_on_prev_tab()
{
    termit_prev_tab();
}

void termit_on_next_tab()
{
    termit_next_tab();
}

void termit_on_paste()
{
    termit_paste();
}

void termit_on_copy()
{
    termit_copy();
}

void termit_on_close_tab()
{
    termit_close_tab();
}

static gboolean dlg_key_press(GtkWidget *widget, GdkEventKey *event, gpointer user_data)
{
    switch (event->keyval) {
    case GDK_Return:
        g_signal_emit_by_name(GTK_OBJECT(widget), "response", GTK_RESPONSE_ACCEPT, NULL);
        break;
    case GDK_Escape:
        g_signal_emit_by_name(GTK_OBJECT(widget), "response", GTK_RESPONSE_REJECT, NULL);
        break;
    default:
        return FALSE;
    }
    return TRUE;
}

void termit_on_set_tab_name()
{
    GtkWidget *dlg = gtk_dialog_new_with_buttons(
        _("Tab name"), 
        GTK_WINDOW(termit.main_window), 
        GTK_DIALOG_MODAL, 
        GTK_STOCK_OK, GTK_RESPONSE_ACCEPT, GTK_STOCK_CANCEL, GTK_RESPONSE_REJECT, NULL);
    gtk_dialog_set_default_response(GTK_DIALOG(dlg), GTK_RESPONSE_ACCEPT);
    gtk_window_set_modal(GTK_WINDOW(dlg), TRUE);

    gint page = gtk_notebook_get_current_page(GTK_NOTEBOOK(termit.notebook));
    TERMIT_GET_TAB_BY_INDEX(pTab, page);
    GtkWidget *label = gtk_label_new(_("Tab name"));
    GtkWidget *entry = gtk_entry_new();
    gtk_entry_set_text(
        GTK_ENTRY(entry), 
        gtk_notebook_get_tab_label_text(GTK_NOTEBOOK(termit.notebook), pTab->hbox));
    
    GtkWidget *hbox = gtk_hbox_new(FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, FALSE, 5);
    gtk_box_pack_start(GTK_BOX(hbox), entry, TRUE, FALSE, 5);
    
    g_signal_connect(G_OBJECT(dlg), "key-press-event", G_CALLBACK(dlg_key_press), dlg);

    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dlg)->vbox), hbox, FALSE, FALSE, 10);
    gtk_widget_show_all(dlg);
    
    if (GTK_RESPONSE_ACCEPT == gtk_dialog_run(GTK_DIALOG(dlg)))
        termit_set_tab_name(page, gtk_entry_get_text(GTK_ENTRY(entry)));
    
    gtk_widget_destroy(dlg);
}

void termit_on_select_foreground_color()
{
    GtkWidget *dlg = gtk_color_selection_dialog_new(_("Select foreground color"));
    GtkColorSelection* p_color_sel = GTK_COLOR_SELECTION((GTK_COLOR_SELECTION_DIALOG(dlg)->colorsel));
    gtk_color_selection_set_current_color(p_color_sel, &termit.foreground_color);

    if (GTK_RESPONSE_OK == gtk_dialog_run(GTK_DIALOG(dlg))) {
        GdkColor color;
        gtk_color_selection_get_current_color(p_color_sel, &color);
        termit_set_foreground_color(&color);
    }

    gtk_widget_destroy(dlg);
}

void termit_on_select_font()
{
    GtkWidget *dlg = gtk_font_selection_dialog_new(_("Select font"));
    gtk_font_selection_dialog_set_font_name(GTK_FONT_SELECTION_DIALOG(dlg), 
                                            pango_font_description_to_string(termit.font));

    if (GTK_RESPONSE_OK == gtk_dialog_run(GTK_DIALOG(dlg)))
        termit_set_font(gtk_font_selection_dialog_get_font_name(GTK_FONT_SELECTION_DIALOG(dlg)));

    gtk_widget_destroy(dlg);
}

void termit_on_new_tab()
{
    termit_append_tab();
}

void termit_on_exit()
{
    if (confirm_exit() == FALSE)
        termit_quit();
}

void termit_on_switch_page(GtkNotebook *notebook, GtkNotebookPage *page, guint page_num, gpointer user_data)
{
    TERMIT_GET_TAB_BY_INDEX(pTab, page_num);
    // it seems that set_active eventually calls toggle callback
    ((GtkCheckMenuItem*)termit.mi_show_scrollbar)->active = pTab->scrollbar_is_shown;
    termit_set_statusbar_encoding(page_num);
    if (configs.allow_changing_title)
        termit_set_window_title(pTab->title);
}

gint termit_on_double_click(GtkWidget *widget, GdkEventButton *event, gpointer func_data)
{
    if (event->type == GDK_2BUTTON_PRESS)
        termit_append_tab();
    return FALSE;
}

static gchar* termit_get_xdg_data_path()
{
    gchar* fullPath = NULL;
    const gchar *dataHome = g_getenv("XDG_DATA_HOME");
    if (dataHome)
        fullPath = g_strdup_printf("%s/termit", dataHome);
    else
        fullPath = g_strdup_printf("%s/.local/share/termit", g_getenv("HOME"));
    TRACE("XDG_DATA_PATH=%s", fullPath);
    return fullPath;
}

void termit_on_save_session()
{
/*  // debug   
    termit_save_session("tmpSess");
    return;
*/
    gchar* fullPath = termit_get_xdg_data_path();
    
    GtkWidget* dlg = gtk_file_chooser_dialog_new(
        _("Save session"), 
        GTK_WINDOW(termit.main_window), 
        GTK_FILE_CHOOSER_ACTION_SAVE, 
        GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
        GTK_STOCK_SAVE, GTK_RESPONSE_ACCEPT,
        NULL);
    gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(dlg), TRUE);

    gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(dlg), fullPath);
    gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(dlg), "New session");

    if (gtk_dialog_run(GTK_DIALOG(dlg)) != GTK_RESPONSE_ACCEPT) {
        gtk_widget_destroy(dlg);
        g_free(fullPath);
        return;
    }

    gchar* filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dlg));
    termit_save_session(filename);

    g_free(filename);
    gtk_widget_destroy(dlg);
    g_free(fullPath);
}

void termit_on_load_session()
{
    gchar* fullPath = termit_get_xdg_data_path();

    GtkWidget* dlg = gtk_file_chooser_dialog_new(
        _("Open session"), 
        GTK_WINDOW(termit.main_window), 
        GTK_FILE_CHOOSER_ACTION_OPEN, 
        GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
        GTK_STOCK_OPEN, GTK_RESPONSE_ACCEPT,
        NULL);
    gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(dlg), TRUE);
    gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(dlg), fullPath);

    if (gtk_dialog_run(GTK_DIALOG(dlg)) != GTK_RESPONSE_ACCEPT)
        goto free_dlg;

    gchar* filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dlg));
    termit_load_session(filename);
    g_free(filename);
free_dlg:
    gtk_widget_destroy(dlg);
    g_free(fullPath);
}

void termit_on_user_menu_item_selected(GtkWidget *widget, void *data)
{
    struct UserMenuItem* pMi = (struct UserMenuItem*)data;
    TRACE("%s: %s", pMi->name, pMi->userFunc);
    termit_lua_execute(pMi->userFunc);
}

gboolean termit_on_key_press(GtkWidget *widget, GdkEventKey *event, gpointer user_data)
{
    return termit_process_key(event);
}

