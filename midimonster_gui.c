#include <stdio.h>
#include <gtk/gtk.h>

#ifndef _WIN32
	#define MM_API __attribute__((visibility("default")))
#else
	#define MM_API __attribute__((dllexport))
#endif

#include "midimonster.h"
#include "core/core.h"
#include "core/config.h"

/*
 * TODO
 *  * disable menu items (load, start, stop) when appropriate
 *  * ringbuffer for log entries
 */

static GtkTreeView* list_view = NULL;
static GtkWidget* window = NULL;

enum {
	UI_NONE_ID = 0,
	UI_MENU_EXIT,
	UI_MENU_LOAD,
	UI_MENU_START,
	UI_MENU_STOP,
	UI_MENU_ABOUT,
	UI_MENU_HOMEPAGE,
	UI_SENTINEL_ID
};

static void ui_listview_push(char* module, char* message){
	GtkTreeIter iter;
	GtkListStore* list_store = GTK_LIST_STORE(gtk_tree_view_get_model(list_view));
	gtk_list_store_append(list_store, &iter);
	gtk_list_store_set(list_store, &iter, 0, module, 1, message, -1);
	//return gtk_tree_model_iter_n_children(GTK_TREE_MODEL(list_store),NULL) - 1;
}

MM_API int log_printf(int level, char* module, char* fmt, ...){
	int rv = 0;
	va_list args;
	va_start(args, fmt);
	fprintf(stderr, "%s%s\t", level ? "debug/" : "", module);
	rv = vfprintf(stderr, fmt, args);
	va_end(args);
	return rv;
}

static void ui_filechooser(){
	GtkFileChooserAction action = GTK_FILE_CHOOSER_ACTION_OPEN;
	gint res;

	GtkWidget* dialog = gtk_file_chooser_dialog_new("Load configuration", GTK_WINDOW(window), action, "_Cancel", GTK_RESPONSE_CANCEL, "_Open", GTK_RESPONSE_ACCEPT, NULL);
	res = gtk_dialog_run(GTK_DIALOG(dialog));
	if(res == GTK_RESPONSE_ACCEPT){
		char* filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
		ui_listview_push("ui", filename);
		g_free(filename);
	}

	gtk_widget_destroy(dialog);
}

static gboolean ui_menu_handler(GtkWidget* widget, GdkEvent* raw_event, gpointer data){
	GdkEventButton* event = (GdkEventButton*) raw_event;
	if(event->type != GDK_BUTTON_PRESS){
		return FALSE;
	}

	switch((size_t) data){
		case UI_MENU_EXIT:
			gtk_main_quit();
			break;
		case UI_MENU_START:
			ui_listview_push("ui", "Starting midimonster core");
			break;
		case UI_MENU_LOAD:
			ui_filechooser();
			break;
		case UI_MENU_STOP:
			ui_listview_push("ui", "Stopping midimonster core");
			break;
		case UI_MENU_HOMEPAGE:
			//gtk_show_uri(NULL, "https://midimonster.net/", event->time, NULL);
			break;
		default:
			break;
	}
	return TRUE;
}

static void ui_menu_item(GtkWidget* menu, char* title, size_t identifier, void* callback){
	GtkWidget* item = gtk_menu_item_new_with_label(title);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
	//g_signal_connect(item, "activate", G_CALLBACK(callback), (gpointer) identifier);
	g_signal_connect(item, "event", G_CALLBACK(callback), (gpointer) identifier);
	gtk_widget_show(item);
}

static void ui_menu_submenu(GtkWidget* menu, char* title, GtkWidget* submenu){
	GtkWidget* item = gtk_menu_item_new_with_label(title);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
	gtk_menu_item_set_submenu(GTK_MENU_ITEM(item), submenu);
	gtk_widget_show(item);
}

static void ui_rootmenu(GtkWidget* menu_bar){
	GtkWidget* file_menu = gtk_menu_new();
	GtkWidget* translation_menu = gtk_menu_new();
	GtkWidget* help_menu = gtk_menu_new();

	ui_menu_item(file_menu, "Load configuration", UI_MENU_LOAD, ui_menu_handler);
	ui_menu_item(file_menu, "Exit", UI_MENU_EXIT, ui_menu_handler);

	ui_menu_item(translation_menu, "Start", UI_MENU_START, ui_menu_handler);
	ui_menu_item(translation_menu, "Stop", UI_MENU_STOP, ui_menu_handler);

	ui_menu_item(help_menu, "About", UI_MENU_ABOUT, ui_menu_handler);
	ui_menu_item(help_menu, "Open homepage", UI_MENU_HOMEPAGE, ui_menu_handler);

	ui_menu_submenu(menu_bar, "File", file_menu);
	ui_menu_submenu(menu_bar, "Translation", translation_menu);
	ui_menu_submenu(menu_bar, "Help", help_menu);
}

static GtkTreeViewColumn* ui_list_column(GtkTreeView* listview, char* name, size_t identifier){
	GtkTreeViewColumn* column = gtk_tree_view_column_new_with_attributes(name, gtk_cell_renderer_text_new(), "text", identifier, NULL);
	gtk_tree_view_append_column(GTK_TREE_VIEW(listview), column);
	gtk_tree_view_column_set_resizable(column, TRUE);
	return column;
}

int main(int argc, char** argv){
	GtkWidget* menu_bar, *scroller, *vbox;

	gtk_init(&argc, &argv);
	window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_widget_set_size_request(GTK_WIDGET(window), 400, 200);
	gtk_window_set_title(GTK_WINDOW(window), MIDIMONSTER_VERSION);
	g_signal_connect(window, "delete-event", G_CALLBACK(gtk_main_quit), NULL);
	//gtk_container_set_border_width(GTK_CONTAINER(window), 10);

	menu_bar = gtk_menu_bar_new();
	ui_rootmenu(menu_bar);

	#if GTK_MAJOR_VERSION >= 3
	vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	#else
	vbox = gtk_vbox_new(FALSE, 0);
	#endif
	gtk_box_pack_start(GTK_BOX(vbox), menu_bar, FALSE, FALSE, 2);

	GtkListStore *store = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_STRING, -1);
	list_view = GTK_TREE_VIEW(gtk_tree_view_new_with_model(GTK_TREE_MODEL(store)));
	g_object_unref(store);
	gtk_tree_view_set_reorderable(list_view, FALSE);
	gtk_tree_selection_set_mode(gtk_tree_view_get_selection(GTK_TREE_VIEW(list_view)), GTK_SELECTION_NONE);

	ui_list_column(list_view, "Module", 0);
	ui_list_column(list_view, "Message", 1);

	scroller = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroller), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_container_add(GTK_CONTAINER(scroller), GTK_WIDGET(list_view));
	gtk_box_pack_end(GTK_BOX(vbox), GTK_WIDGET(scroller), TRUE, TRUE, 2);

	gtk_container_add(GTK_CONTAINER(window), vbox);
	gtk_widget_show_all(window);
	gtk_main();
	return 0;
}
