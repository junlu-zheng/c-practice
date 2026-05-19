/*
 * Floating Planner
 * ----------------
 * This is a small GTK desktop planner written in C.
 *
 * Main features:
 * 1. Add a task with date, time, category, title, and note.
 * 2. Display all tasks in a table.
 * 3. Save tasks to a local text file called plans.txt.
 * 4. Load tasks automatically when the app starts.
 * 5. Mark a selected task as done.
 * 6. Delete a selected task.
 *
 * Data format in plans.txt:
 * date|time|category|title|note|done
 *
 * Example:
 * 2026-05-22|14:30|Study|Meet supervisor|Discuss dissertation plan|0
 *
 * done = 0 means unfinished
 * done = 1 means finished
 */


#include <gtk/gtk.h>
#include <stdio.h>
#include <string.h>

#define PLANS_FILE "plans.txt"

/*
 * The task table has six columns.
 * GTK ListStore uses column numbers internally.
 *
 * For example:
 * COL_DATE means column 0.
 * COL_TIME means column 1.
 *
 * N_COLS is not a real column.
 * It stores the total number of columns.
 */
enum {
    COL_DATE,       // Date column, e.g. 2026-05-22
    COL_TIME,       // Time column, e.g. 14:30
    COL_CATEGORY,   // Category column, e.g. Study, Rent, Friend
    COL_TITLE,      // Main task title
    COL_NOTE,       // Extra description
    COL_DONE,       // Whether the task is done
    N_COLS          // Total number of columns
};

/*
 * These variables store important GTK widgets.
 *
 * We make them global because several functions need to access them.
 * For example:
 * - on_add_clicked() needs to read text from the entry boxes.
 * - save_tasks() needs to read all rows from the task list.
 * - set_status() needs to update the status label.
 */
static GtkWidget *date_entry;      // Input box for date
static GtkWidget *time_entry;      // Input box for time
static GtkWidget *category_entry;  // Input box for category
static GtkWidget *title_entry;     // Input box for task title
static GtkWidget *note_entry;      // Input box for notes
static GtkWidget *status_label;    // Label at the bottom showing messages
static GtkWidget *tree_view;       // Table-like view that displays all tasks

/*
 * GtkListStore is the actual data model behind the table.
 * The tree_view displays the data, but the store holds the data.
 */
static GtkListStore *store;

/*
 * This stores the row currently being edited.
 *
 * Why use GtkTreeRowReference instead of GtkTreeIter?
 * A GtkTreeIter can become unsafe if the list changes.
 * GtkTreeRowReference is safer because it keeps a reference to a row path.
 *
 * If editing_row_ref is NULL, it means we are not editing any existing task.
 */
static GtkTreeRowReference *editing_row_ref = NULL;

/*
 * Update the message shown at the bottom of the window.
 *
 * Example:
 * set_status("Task added.");
 * will display "Task added." in the status label.
 */
static void set_status(const char *message) {
    gtk_label_set_text(GTK_LABEL(status_label), message);
}

/*
 * Clean text before saving it into plans.txt.
 *
 * Our file format uses | to separate fields:
 * date|time|category|title|note|done
 *
 * So if the user types | inside a title or note,
 * it would break the file format.
 *
 * This function replaces:
 * - | with a space
 * - newline with a space
 *
 * Example:
 * "Pay rent | urgent" becomes "Pay rent   urgent"
 */
static char *clean_field(const char *text) {
    char *copy = g_strdup(text ? text : "");

    for (int i = 0; copy[i] != '\0'; i++) {
        if (copy[i] == '|' || copy[i] == '\n' || copy[i] == '\r') {
            copy[i] = ' ';
        }
    }

    return copy;
}

/*
 * Add one task to the GtkListStore.
 *
 * This function only updates the list inside the app.
 * It does not directly save to plans.txt.
 *
 * Parameters:
 * date      task date, e.g. "2026-05-22"
 * time      task time, e.g. "14:30"
 * category  task category, e.g. "Study"
 * title     task title, e.g. "Meet supervisor"
 * note      extra details
 * done      0 = not done, 1 = done
 */
static void append_task(
    const char *date,
    const char *time,
    const char *category,
    const char *title,
    const char *note,
    int done
) {
    GtkTreeIter iter;

    /*
     * Add a new empty row to the store.
     * iter will point to this new row.
     */
    gtk_list_store_append(store, &iter);

    /*
     * Fill the new row with task information.
     *
     * The pattern is:
     * column name, value,
     * column name, value,
     * ...
     * -1 means the end of the list.
     */
    gtk_list_store_set(
        store,
        &iter,
        COL_DATE, date,
        COL_TIME, time,
        COL_CATEGORY, category,
        COL_TITLE, title,
        COL_NOTE, note,
        COL_DONE, done ? "Yes" : "No",
        -1
    );
}


/*
 * Save all tasks from the GtkListStore into plans.txt.
 *
 * Important idea:
 * The task list shown on screen is not automatically saved.
 * We need to loop through every row in the store and write it to a file.
 *
 * File format:
 * date|time|category|title|note|done
 */
static void save_tasks(void) {
    /*
     * Open plans.txt in write mode.
     * "w" means:
     * - create the file if it does not exist
     * - overwrite the old content if it already exists
     */
    FILE *fp = fopen(PLANS_FILE, "w");

    if (fp == NULL) {
        set_status("Could not save plans.txt");
        return;
    }

    GtkTreeIter iter;

    /*
     * Get the first row in the store.
     * If the store is empty, valid will be FALSE.
     */
    gboolean valid = gtk_tree_model_get_iter_first(GTK_TREE_MODEL(store), &iter);

    /*
     * Loop through all rows in the store.
     * Each row represents one task.
     */
    while (valid) {
        char *date;
        char *time;
        char *category;
        char *title;
        char *note;
        char *done_text;

        /*
         * Read values from the current row.
         *
         * gtk_tree_model_get() allocates new strings for us,
         * so we must free them later with g_free().
         */
        gtk_tree_model_get(
            GTK_TREE_MODEL(store),
            &iter,
            COL_DATE, &date,
            COL_TIME, &time,
            COL_CATEGORY, &category,
            COL_TITLE, &title,
            COL_NOTE, &note,
            COL_DONE, &done_text,
            -1
        );

        /*
         * Clean each field before saving.
         * This avoids breaking the file format if the user typed | or newline.
         */
        char *clean_date = clean_field(date);
        char *clean_time = clean_field(time);
        char *clean_category = clean_field(category);
        char *clean_title = clean_field(title);
        char *clean_note = clean_field(note);

        /*
         * In the table, done is displayed as "Yes" or "No".
         * In the file, we save it as 1 or 0.
         */
        int done = (done_text != NULL && strcmp(done_text, "Yes") == 0) ? 1 : 0;

        /*
         * Write one task as one line in plans.txt.
         */
        fprintf(
            fp,
            "%s|%s|%s|%s|%s|%d\n",
            clean_date,
            clean_time,
            clean_category,
            clean_title,
            clean_note,
            done
        );

        /*
         * Free strings created by gtk_tree_model_get().
         */
        g_free(date);
        g_free(time);
        g_free(category);
        g_free(title);
        g_free(note);
        g_free(done_text);

        /*
         * Free strings created by clean_field().
         */
        g_free(clean_date);
        g_free(clean_time);
        g_free(clean_category);
        g_free(clean_title);
        g_free(clean_note);

        /*
         * Move to the next row.
         * If there is no next row, valid becomes FALSE and the loop stops.
         */
        valid = gtk_tree_model_iter_next(GTK_TREE_MODEL(store), &iter);
    }

    fclose(fp);
    set_status("Saved successfully.");
}

/*
 * Load tasks from plans.txt when the app starts.
 *
 * Important idea:
 * plans.txt stores tasks line by line.
 * Each line is split into six parts:
 *
 * date|time|category|title|note|done
 *
 * After reading each line, we call append_task()
 * to add that task back into the on-screen list.
 */
static void load_tasks(void) {
    /*
     * Open plans.txt in read mode.
     * If the file does not exist, it means this is probably the first run.
     */
    FILE *fp = fopen(PLANS_FILE, "r");

    if (fp == NULL) {
        set_status("No existing plans.txt. Start adding tasks.");
        return;
    }

    /*
     * A buffer for storing one line from the file.
     * Each line should be shorter than 1024 characters.
     */
    char line[1024];

    /*
     * Read the file line by line.
     * fgets() returns NULL when there are no more lines.
     */
    while (fgets(line, sizeof(line), fp) != NULL) {
        /*
         * Remove the trailing newline character from the line.
         * For example:
         * "2026-05-22|14:30|Study|Meet supervisor|Note|0\n"
         * becomes:
         * "2026-05-22|14:30|Study|Meet supervisor|Note|0"
         */
        g_strchomp(line);

        /*
         * Skip empty lines.
         */
        if (strlen(line) == 0) {
            continue;
        }

        /*
         * Split the line by |.
         *
         * Example:
         * "2026-05-22|14:30|Study|Meet supervisor|Note|0"
         *
         * becomes:
         * parts[0] = "2026-05-22"
         * parts[1] = "14:30"
         * parts[2] = "Study"
         * parts[3] = "Meet supervisor"
         * parts[4] = "Note"
         * parts[5] = "0"
         */
        char **parts = g_strsplit(line, "|", 6);

        /*
         * Only load the task if all six fields exist.
         * This protects the program from broken or incomplete lines.
         */
        if (
            parts[0] != NULL &&
            parts[1] != NULL &&
            parts[2] != NULL &&
            parts[3] != NULL &&
            parts[4] != NULL &&
            parts[5] != NULL
        ) {
            /*
             * In the file:
             * 0 means unfinished
             * 1 means finished
             */
            int done = strcmp(parts[5], "1") == 0;

            /*
             * Add the loaded task back into the visible task list.
             */
            append_task(parts[0], parts[1], parts[2], parts[3], parts[4], done);
        }

        /*
         * Free the array created by g_strsplit().
         */
        g_strfreev(parts);
    }

    fclose(fp);
    set_status("Plans loaded.");
}

/*
 * This function runs when the user clicks the "Add Task" button.
 *
 * Steps:
 * 1. Read text from the input boxes.
 * 2. Check whether Date and Title are empty.
 * 3. Add the task to the list.
 * 4. Clear the title and note boxes.
 * 5. Save all tasks to plans.txt.
 */
static void on_add_clicked(GtkWidget *widget, gpointer data) {
    (void)widget;
    (void)data;

    const char *date = gtk_entry_get_text(GTK_ENTRY(date_entry));
    const char *time = gtk_entry_get_text(GTK_ENTRY(time_entry));
    const char *category = gtk_entry_get_text(GTK_ENTRY(category_entry));
    const char *title = gtk_entry_get_text(GTK_ENTRY(title_entry));
    const char *note = gtk_entry_get_text(GTK_ENTRY(note_entry));

    /*
     * We require date and title.
     * Time, category and note are optional.
     */
    if (strlen(date) == 0 || strlen(title) == 0) {
        set_status("Date and Title are required.");
        return;
    }

    /*
     * Add this task to the list.
     * The last argument 0 means this task is not done yet.
     */
    append_task(date, time, category, title, note, 0);

    /*
     * Clear only title and note.
     * We keep date/time/category because the next task may use the same date.
     */
    gtk_entry_set_text(GTK_ENTRY(title_entry), "");
    gtk_entry_set_text(GTK_ENTRY(note_entry), "");

    /*
     * Save immediately after adding a task.
     */
    save_tasks();
    set_status("Task added.");
}

/*
 * This function runs when the user clicks the "Toggle Done" button.
 *
 * - If the selected task is unfinished, change it to finished.
 * - If the selected task is finished, change it back to unfinished.
 *
 * This makes the app safer because the user can undo an accidental click.
 */
static void on_toggle_done_clicked(GtkWidget *widget, gpointer data) {
    (void)widget;
    (void)data;

    /*
     * Get the currently selected row from the task table.
     */
    GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(tree_view));
    GtkTreeModel *model;
    GtkTreeIter iter;

    if (gtk_tree_selection_get_selected(selection, &model, &iter)) {
        char *done_text = NULL;

        /*
         * Read the current Done value from the selected row.
         * It should be either "Yes" or "No".
         */
        gtk_tree_model_get(
            model,
            &iter,
            COL_DONE, &done_text,
            -1
        );

        /*
         * Toggle the value.
         *
         * If it is currently "Yes", change it to "No".
         * Otherwise, change it to "Yes".
         */
        if (done_text != NULL && strcmp(done_text, "Yes") == 0) {
            gtk_list_store_set(GTK_LIST_STORE(model), &iter, COL_DONE, "No", -1);
            set_status("Task marked as unfinished.");
        } else {
            gtk_list_store_set(GTK_LIST_STORE(model), &iter, COL_DONE, "Yes", -1);
            set_status("Task marked as done.");
        }

        /*
         * done_text was allocated by gtk_tree_model_get(),
         * so we need to free it.
         */
        g_free(done_text);

        /*
         * Save the new Done status immediately.
         */
        save_tasks();
    } else {
        set_status("Please select a task first.");
    }
}

/*
 * This function runs when the user clicks "Edit Selected".
 *
 * Steps:
 * 1. Get the selected row from the task table.
 * 2. Read that row's Date, Time, Category, Title, and Note.
 * 3. Put those values back into the input boxes.
 * 4. Store a reference to this row, so that Update Task knows which row to modify.
 */
static void on_edit_clicked(GtkWidget *widget, gpointer data) {
    (void)widget;
    (void)data;

    GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(tree_view));
    GtkTreeModel *model;
    GtkTreeIter iter;

    if (gtk_tree_selection_get_selected(selection, &model, &iter)) {
        char *date;
        char *time;
        char *category;
        char *title;
        char *note;

        /*
         * Read task information from the selected row.
         */
        gtk_tree_model_get(
            model,
            &iter,
            COL_DATE, &date,
            COL_TIME, &time,
            COL_CATEGORY, &category,
            COL_TITLE, &title,
            COL_NOTE, &note,
            -1
        );

        /*
         * Put the selected task back into the input boxes.
         * Now the user can edit the text.
         */
        gtk_entry_set_text(GTK_ENTRY(date_entry), date);
        gtk_entry_set_text(GTK_ENTRY(time_entry), time);
        gtk_entry_set_text(GTK_ENTRY(category_entry), category);
        gtk_entry_set_text(GTK_ENTRY(title_entry), title);
        gtk_entry_set_text(GTK_ENTRY(note_entry), note);

        /*
         * If we were already editing another row, free the old reference first.
         */
        if (editing_row_ref != NULL) {
            gtk_tree_row_reference_free(editing_row_ref);
            editing_row_ref = NULL;
        }

        /*
         * Create a row reference for the selected task.
         * This tells Update Task which row should be changed later.
         */
        GtkTreePath *path = gtk_tree_model_get_path(model, &iter);
        editing_row_ref = gtk_tree_row_reference_new(model, path);
        gtk_tree_path_free(path);

        set_status("Editing selected task. Modify the fields, then click Update Task.");

        /*
         * Free strings created by gtk_tree_model_get().
         */
        g_free(date);
        g_free(time);
        g_free(category);
        g_free(title);
        g_free(note);
    } else {
        set_status("Please select a task first.");
    }
}

/*
 * This function runs when the user clicks "Update Task".
 *
 * It updates the row that was previously selected by Edit Selected.
 * It does not create a new task.
 */
static void on_update_clicked(GtkWidget *widget, gpointer data) {
    (void)widget;
    (void)data;

    /*
     * If editing_row_ref is NULL, the user has not clicked Edit Selected yet.
     */
    if (editing_row_ref == NULL) {
        set_status("No task is being edited. Select a task and click Edit Selected first.");
        return;
    }

    const char *date = gtk_entry_get_text(GTK_ENTRY(date_entry));
    const char *time = gtk_entry_get_text(GTK_ENTRY(time_entry));
    const char *category = gtk_entry_get_text(GTK_ENTRY(category_entry));
    const char *title = gtk_entry_get_text(GTK_ENTRY(title_entry));
    const char *note = gtk_entry_get_text(GTK_ENTRY(note_entry));

    /*
     * Date and Title are still required when updating a task.
     */
    if (strlen(date) == 0 || strlen(title) == 0) {
        set_status("Date and Title are required.");
        return;
    }

    GtkTreeModel *model = gtk_tree_row_reference_get_model(editing_row_ref);
    GtkTreePath *path = gtk_tree_row_reference_get_path(editing_row_ref);

    /*
     * If path is NULL, the row may have been deleted.
     */
    if (path == NULL) {
        gtk_tree_row_reference_free(editing_row_ref);
        editing_row_ref = NULL;
        set_status("The task being edited no longer exists.");
        return;
    }

    GtkTreeIter iter;

    /*
     * Convert the stored row path back into an iter.
     * The iter lets us modify that row in the GtkListStore.
     */
    if (gtk_tree_model_get_iter(model, &iter, path)) {
        /*
         * Only update the text fields.
         * We keep the Done status unchanged.
         */
        gtk_list_store_set(
            GTK_LIST_STORE(model),
            &iter,
            COL_DATE, date,
            COL_TIME, time,
            COL_CATEGORY, category,
            COL_TITLE, title,
            COL_NOTE, note,
            -1
        );

        save_tasks();
        set_status("Task updated.");

        /*
         * After updating, we leave edit mode.
         */
        gtk_tree_row_reference_free(editing_row_ref);
        editing_row_ref = NULL;
    } else {
        set_status("Could not update task.");
    }

    gtk_tree_path_free(path);
}

static void on_delete_clicked(GtkWidget *widget, gpointer data) {
    (void)widget;
    (void)data;

    GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(tree_view));
    GtkTreeModel *model;
    GtkTreeIter iter;

    if (gtk_tree_selection_get_selected(selection, &model, &iter)) {
        gtk_list_store_remove(GTK_LIST_STORE(model), &iter);

	/*
     	* If the user deletes a task while editing,
     	* cancel the current edit mode to avoid updating a deleted row.
     	*/
    	if (editing_row_ref != NULL) {
        	gtk_tree_row_reference_free(editing_row_ref);
        	editing_row_ref = NULL;
    	}

        save_tasks();
        set_status("Task deleted.");
    } else {
        set_status("Please select a task first.");
    }
}

static void on_save_clicked(GtkWidget *widget, gpointer data) {
    (void)widget;
    (void)data;

    save_tasks();
}

static void on_window_destroy(GtkWidget *widget, gpointer data) {
    (void)widget;
    (void)data;

    save_tasks();
    gtk_main_quit();
}

static void add_column(const char *title, int column_id) {
    GtkCellRenderer *renderer = gtk_cell_renderer_text_new();

    GtkTreeViewColumn *column = gtk_tree_view_column_new_with_attributes(
        title,
        renderer,
        "text",
        column_id,
        NULL
    );

    gtk_tree_view_column_set_resizable(column, TRUE);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree_view), column);
}

int main(int argc, char *argv[]) {
    gtk_init(&argc, &argv);

    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "Floating Planner");
    gtk_window_set_default_size(GTK_WINDOW(window), 760, 520);
    gtk_window_set_keep_above(GTK_WINDOW(window), TRUE);
    gtk_container_set_border_width(GTK_CONTAINER(window), 10);

    GtkWidget *main_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_container_add(GTK_CONTAINER(window), main_box);

    GtkWidget *header = gtk_label_new("Floating Planner / To-do List");
    gtk_box_pack_start(GTK_BOX(main_box), header, FALSE, FALSE, 0);

    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 6);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
    gtk_box_pack_start(GTK_BOX(main_box), grid, FALSE, FALSE, 0);

    GtkWidget *date_label = gtk_label_new("Date:");
    GtkWidget *time_label = gtk_label_new("Time:");
    GtkWidget *category_label = gtk_label_new("Category:");
    GtkWidget *title_label = gtk_label_new("Title:");
    GtkWidget *note_label = gtk_label_new("Note:");

    date_entry = gtk_entry_new();
    time_entry = gtk_entry_new();
    category_entry = gtk_entry_new();
    title_entry = gtk_entry_new();
    note_entry = gtk_entry_new();

    gtk_entry_set_placeholder_text(GTK_ENTRY(date_entry), "2026-05-22");
    gtk_entry_set_placeholder_text(GTK_ENTRY(time_entry), "14:30");
    gtk_entry_set_placeholder_text(GTK_ENTRY(category_entry), "Study / Rent / Friend / Deadline");
    gtk_entry_set_placeholder_text(GTK_ENTRY(title_entry), "Meet supervisor");
    gtk_entry_set_placeholder_text(GTK_ENTRY(note_entry), "Discuss dissertation plan");

    gtk_grid_attach(GTK_GRID(grid), date_label, 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), date_entry, 1, 0, 1, 1);

    gtk_grid_attach(GTK_GRID(grid), time_label, 2, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), time_entry, 3, 0, 1, 1);

    gtk_grid_attach(GTK_GRID(grid), category_label, 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), category_entry, 1, 1, 3, 1);

    gtk_grid_attach(GTK_GRID(grid), title_label, 0, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), title_entry, 1, 2, 3, 1);

    gtk_grid_attach(GTK_GRID(grid), note_label, 0, 3, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), note_entry, 1, 3, 3, 1);

    GtkWidget *button_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_pack_start(GTK_BOX(main_box), button_box, FALSE, FALSE, 0);

    GtkWidget *add_button = gtk_button_new_with_label("Add Task");
    GtkWidget *edit_button = gtk_button_new_with_label("Edit Selected");
    GtkWidget *update_button = gtk_button_new_with_label("Update Task");
    GtkWidget *done_button = gtk_button_new_with_label("Toggle Done");
    GtkWidget *delete_button = gtk_button_new_with_label("Delete Selected");
    GtkWidget *save_button = gtk_button_new_with_label("Save");

    gtk_box_pack_start(GTK_BOX(button_box), add_button, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(button_box), edit_button, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(button_box), update_button, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(button_box), done_button, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(button_box), delete_button, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(button_box), save_button, FALSE, FALSE, 0);

    store = gtk_list_store_new(
        N_COLS,
        G_TYPE_STRING,
        G_TYPE_STRING,
        G_TYPE_STRING,
        G_TYPE_STRING,
        G_TYPE_STRING,
        G_TYPE_STRING
    );

    tree_view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
    add_column("Date", COL_DATE);
    add_column("Time", COL_TIME);
    add_column("Category", COL_CATEGORY);
    add_column("Title", COL_TITLE);
    add_column("Note", COL_NOTE);
    add_column("Done", COL_DONE);

    GtkWidget *scrolled_window = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(
        GTK_SCROLLED_WINDOW(scrolled_window),
        GTK_POLICY_AUTOMATIC,
        GTK_POLICY_AUTOMATIC
    );

    gtk_container_add(GTK_CONTAINER(scrolled_window), tree_view);
    gtk_box_pack_start(GTK_BOX(main_box), scrolled_window, TRUE, TRUE, 0);

    status_label = gtk_label_new("Ready.");
    gtk_box_pack_start(GTK_BOX(main_box), status_label, FALSE, FALSE, 0);

    g_signal_connect(add_button, "clicked", G_CALLBACK(on_add_clicked), NULL);
    g_signal_connect(edit_button, "clicked", G_CALLBACK(on_edit_clicked), NULL);
    g_signal_connect(update_button, "clicked", G_CALLBACK(on_update_clicked), NULL);
    g_signal_connect(done_button, "clicked", G_CALLBACK(on_toggle_done_clicked), NULL);
    g_signal_connect(delete_button, "clicked", G_CALLBACK(on_delete_clicked), NULL);
    g_signal_connect(save_button, "clicked", G_CALLBACK(on_save_clicked), NULL);
    g_signal_connect(window, "destroy", G_CALLBACK(on_window_destroy), NULL);

    load_tasks();

    gtk_widget_show_all(window);
    gtk_main();

    return 0;
}
