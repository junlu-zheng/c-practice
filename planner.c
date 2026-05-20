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
#include <time.h>

#define PLANS_FILE "plans.txt"
#define DELETED_FILE "deleted_plans.txt"

/*
 * The task table has visible columns and hidden helper columns.
 *
 * Visible columns:
 * - Date
 * - Weekday
 * - Time
 * - Category
 * - Title
 * - Note
 * - Done
 *
 * Hidden columns:
 * - Sort key
 * - Text color
 */
enum {
    COL_DATE,
    COL_WEEKDAY,
    COL_TIME,
    COL_CATEGORY,
    COL_TITLE,
    COL_NOTE,
    COL_DONE,
    COL_SORT_KEY,   // Hidden column used only for sorting
    COL_COLOR,      // Hidden column used only for text color
    N_COLS
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
 * filter_model sits between the real data store and the visible table.
 *
 * store:
 * - contains all tasks
 *
 * filter_model:
 * - decides which tasks should be visible in the current view
 *
 * tree_view:
 * - displays only the tasks allowed by filter_model
 */
static GtkTreeModelFilter *filter_model;

/*
 * View controls.
 *
 * view_mode_combo lets the user choose:
 * - All
 * - Day
 * - Month
 *
 * view_date_entry stores the date used for filtering.
 */
static GtkWidget *view_mode_combo;
static GtkWidget *view_date_entry;

/*
 * Function declaration.
 *
 * move_view_date() uses refresh_view(),
 * but refresh_view() is defined later in the file.
 * In C, we need to declare it before it is used.
 */
static void refresh_view(void);

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
 * Format the date entry as YYYY-MM-DD.
 *
 * The user can type only digits, for example:
 * 20260521
 *
 * The program will format it as:
 * 2026-05-21
 *
 * This function is not called after every single key press.
 * Instead, we call it when the user leaves the date box
 * or when the user clicks Add / Update.
 *
 * This avoids cursor-jumping problems.
 */
static gboolean format_date_entry(GtkWidget *entry) {
    const char *text = gtk_entry_get_text(GTK_ENTRY(entry));

    /*
     * Keep only digits from the user's input.
     * This means inputs like 2026/05/21 or 2026.05.21
     * can still be converted correctly.
     */
    char digits[9];
    int digit_count = 0;

    for (int i = 0; text[i] != '\0' && digit_count < 8; i++) {
        if (text[i] >= '0' && text[i] <= '9') {
            digits[digit_count] = text[i];
            digit_count++;
        }
    }

    digits[digit_count] = '\0';

    /*
     * Build the formatted date.
     *
     * 2026     -> 2026
     * 202605   -> 2026-05
     * 20260521 -> 2026-05-21
     */
    char formatted[11];
    int j = 0;

    for (int i = 0; i < digit_count; i++) {
        if (i == 4 || i == 6) {
            formatted[j] = '-';
            j++;
        }

        formatted[j] = digits[i];
        j++;
    }

    formatted[j] = '\0';

    gtk_entry_set_text(GTK_ENTRY(entry), formatted);
    gtk_editable_set_position(GTK_EDITABLE(entry), -1);

    /*
     * Return TRUE if the date is complete.
     * A complete date has exactly 8 digits.
     */
    return digit_count == 8;
}

/*
 * This function runs when the user leaves the date input box.
 *
 * Example:
 * The user types 20260521, then clicks Time.
 * The date box will become 2026-05-21.
 */
static gboolean on_date_focus_out(GtkWidget *widget, GdkEvent *event, gpointer data) {
    (void)event;
    (void)data;

    format_date_entry(widget);

    /*
     * Returning FALSE means GTK can continue its normal behaviour.
     */
    return FALSE;
}

/*
 * Format the time entry as HH:MM.
 *
 * The user only needs to type digits.
 *
 * Example:
 * User types: 1430
 * App shows: 14:30
 *
 * If the user types symbols such as : or ： or . or /,
 * we ignore them and keep only digits.
 *
 * A valid time should have:
 * - 4 digits
 * - hour between 00 and 23
 * - minute between 00 and 59
 */
static gboolean format_time_entry(GtkWidget *entry) {
    const char *text = gtk_entry_get_text(GTK_ENTRY(entry));

    /*
     * Keep only digits from the user's input.
     * We only need 4 digits:
     * HHMM
     */
    char digits[5];
    int digit_count = 0;

    for (int i = 0; text[i] != '\0' && digit_count < 4; i++) {
        if (text[i] >= '0' && text[i] <= '9') {
            digits[digit_count] = text[i];
            digit_count++;
        }
    }

    digits[digit_count] = '\0';

    /*
     * Build the formatted time.
     *
     * 1      -> 1
     * 14     -> 14
     * 143    -> 14:3
     * 1430   -> 14:30
     */
    char formatted[6];
    int j = 0;

    for (int i = 0; i < digit_count; i++) {
        if (i == 2) {
            formatted[j] = ':';
            j++;
        }

        formatted[j] = digits[i];
        j++;
    }

    formatted[j] = '\0';

    gtk_entry_set_text(GTK_ENTRY(entry), formatted);
    gtk_editable_set_position(GTK_EDITABLE(entry), -1);

    /*
     * Empty time is allowed because some tasks may not have a specific time.
     */
    if (digit_count == 0) {
        return TRUE;
    }

    /*
     * A complete time must have exactly 4 digits.
     */
    if (digit_count != 4) {
        return FALSE;
    }

    /*
     * Check whether the time is logically valid.
     */
    int hour = (digits[0] - '0') * 10 + (digits[1] - '0');
    int minute = (digits[2] - '0') * 10 + (digits[3] - '0');

    if (hour < 0 || hour > 23) {
        return FALSE;
    }

    if (minute < 0 || minute > 59) {
        return FALSE;
    }

    return TRUE;
}

/*
 * Convert a date string in YYYY-MM-DD format into a GDate.
 *
 * Example:
 * input:  "2026-05-21"
 * output: a valid GDate object representing 21 May 2026
 *
 * We return TRUE if the date is valid.
 * We return FALSE if the date is missing or invalid.
 */
static gboolean parse_date_string(const char *date_text, GDate *date) {
    if (date_text == NULL || strlen(date_text) != 10) {
        return FALSE;
    }

    int year;
    int month;
    int day;

    /*
     * Read numbers from YYYY-MM-DD.
     */
    if (sscanf(date_text, "%d-%d-%d", &year, &month, &day) != 3) {
        return FALSE;
    }

    /*
     * Check whether the date is logically valid.
     * For example:
     * 2026-02-30 is not valid.
     */
    if (!g_date_valid_dmy(day, month, year)) {
        return FALSE;
    }

    g_date_set_dmy(date, day, month, year);
    return TRUE;
}

/*
 * Check whether task_date is in the same Monday-to-Sunday week
 * as view_date.
 *
 * Example:
 * view_date = 2026-05-21
 * That week is 2026-05-18 to 2026-05-24.
 *
 * Any task date inside that range should be visible.
 */
static gboolean is_same_week(const char *task_date_text, const char *view_date_text) {
    GDate task_date;
    GDate week_start;
    GDate week_end;

    g_date_clear(&task_date, 1);
    g_date_clear(&week_start, 1);
    g_date_clear(&week_end, 1);

    if (!parse_date_string(task_date_text, &task_date)) {
        return FALSE;
    }

    if (!parse_date_string(view_date_text, &week_start)) {
        return FALSE;
    }

    /*
     * GDateWeekday uses:
     * Monday = 1
     * Tuesday = 2
     * ...
     * Sunday = 7
     *
     * To get Monday of the current week,
     * subtract weekday - 1 days.
     */
    GDateWeekday weekday = g_date_get_weekday(&week_start);
    g_date_subtract_days(&week_start, weekday - 1);

    /*
     * Week end is Sunday.
     */
    week_end = week_start;
    g_date_add_days(&week_end, 6);

    /*
     * g_date_compare(a, b):
     * < 0 means a is before b
     * = 0 means same date
     * > 0 means a is after b
     */
    return (
        g_date_compare(&task_date, &week_start) >= 0 &&
        g_date_compare(&task_date, &week_end) <= 0
    );
}

/*
 * Write a GDate back into a GtkEntry in YYYY-MM-DD format.
 *
 * Example:
 * GDate representing 21 May 2026
 * becomes:
 * 2026-05-21
 */
static void set_entry_from_date(GtkWidget *entry, const GDate *date) {
    char buffer[11];

    snprintf(
        buffer,
        sizeof(buffer),
        "%04d-%02d-%02d",
        g_date_get_year(date),
        g_date_get_month(date),
        g_date_get_day(date)
    );

    gtk_entry_set_text(GTK_ENTRY(entry), buffer);
}

/*
 * Show a diary reminder if the app is opened after 21:00.
 *
 * Behaviour:
 * - If current local time is before 21:00, do nothing.
 * - If current local time is after 21:00, show a dialog.
 * - If the user clicks Yes, fill the input boxes with a diary task.
 *
 * We only fill the input boxes.
 * We do not automatically add the task.
 * This prevents duplicate diary tasks if the user opens the app many times.
 */
static void show_diary_prompt_if_needed(GtkWindow *parent) {
    time_t now = time(NULL);
    struct tm *local = localtime(&now);

    if (local == NULL) {
        return;
    }

    /*
     * tm_hour uses 24-hour time.
     * 21 means 21:00, or 9pm.
     */
    if (local->tm_hour < 21) {
        return;
    }

    GtkWidget *dialog = gtk_message_dialog_new(
        parent,
        GTK_DIALOG_MODAL,
        GTK_MESSAGE_QUESTION,
        GTK_BUTTONS_YES_NO,
        "It is after 21:00. Do you want to write today's diary?"
    );

    gtk_window_set_title(GTK_WINDOW(dialog), "Diary Reminder");

    int response = gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);

    if (response == GTK_RESPONSE_YES) {
        char date_text[11];

        /*
         * tm_year counts years since 1900.
         * tm_mon counts months from 0 to 11.
         */
        snprintf(
            date_text,
            sizeof(date_text),
            "%04d-%02d-%02d",
            local->tm_year + 1900,
            local->tm_mon + 1,
            local->tm_mday
        );

        /*
         * Fill the input boxes with a diary task.
         * The user can still edit it before clicking Add Task.
         */
        gtk_entry_set_text(GTK_ENTRY(date_entry), date_text);
        gtk_entry_set_text(GTK_ENTRY(time_entry), "21:00");
        gtk_entry_set_text(GTK_ENTRY(category_entry), "Diary");
        gtk_entry_set_text(GTK_ENTRY(title_entry), "Write diary");
        gtk_entry_set_text(GTK_ENTRY(note_entry), "Write a short diary for today.");

        set_status("Diary task prepared. Click Add Task to save it.");
    }
}

/*
 * Move the current view date forward or backward.
 *
 * direction = -1 means Previous
 * direction =  1 means Next
 *
 * The movement depends on the current view mode:
 * - Day view:   move by 1 day
 * - Week view:  move by 7 days
 * - Month view: move by 1 month
 */
static void move_view_date(int direction) {
    if (view_mode_combo == NULL || view_date_entry == NULL) {
        return;
    }

    int view_mode = gtk_combo_box_get_active(GTK_COMBO_BOX(view_mode_combo));

    /*
     * All view does not use a date.
     */
    if (view_mode == 0) {
        set_status("Previous / Next only works in Day, Week, or Month view.");
        return;
    }

    /*
     * Format the view date first.
     * This allows the user to type 20260521 before clicking Next.
     */
    format_date_entry(view_date_entry);

    const char *view_date_text = gtk_entry_get_text(GTK_ENTRY(view_date_entry));

    GDate date;
    g_date_clear(&date, 1);

    if (!parse_date_string(view_date_text, &date)) {
        set_status("Please enter a valid view date first, for example 20260521.");
        return;
    }

    if (view_mode == 1) {
        /*
         * Day view:
         * move one day at a time.
         */
        if (direction > 0) {
            g_date_add_days(&date, 1);
        } else {
            g_date_subtract_days(&date, 1);
        }
    } else if (view_mode == 2) {
        /*
         * Week view:
         * move seven days at a time.
         */
        if (direction > 0) {
            g_date_add_days(&date, 7);
        } else {
            g_date_subtract_days(&date, 7);
        }
    } else if (view_mode == 3) {
        /*
         * Month view:
         * move one month at a time.
         */
        if (direction > 0) {
            g_date_add_months(&date, 1);
        } else {
            g_date_subtract_months(&date, 1);
        }
    }

    /*
     * Put the new date back into the View date box.
     */
    set_entry_from_date(view_date_entry, &date);

    /*
     * Re-apply the filter so the table updates immediately.
     */
    refresh_view();

    if (direction > 0) {
        set_status("Moved to next period.");
    } else {
        set_status("Moved to previous period.");
    }
}

/*
 * This function runs when the user clicks Previous.
 */
static void on_previous_clicked(GtkWidget *widget, gpointer data) {
    (void)widget;
    (void)data;

    move_view_date(-1);
}

/*
 * This function runs when the user clicks Next.
 */
static void on_next_clicked(GtkWidget *widget, gpointer data) {
    (void)widget;
    (void)data;

    move_view_date(1);
}

/*
 * Decide whether a task should be visible in the current view.
 *
 * This function is used by GtkTreeModelFilter.
 *
 * It checks:
 * 1. Which view mode the user selected.
 * 2. The task's date.
 * 3. The date entered in the view date box.
 *
 * View modes:
 * 0 = All
 * 1 = Day
 * 2 = Week
 * 3 = Month
 */
static gboolean task_visible_func(GtkTreeModel *model, GtkTreeIter *iter, gpointer data) {
    (void)data;

    /*
     * If the view controls are not ready yet,
     * show everything by default.
     */
    if (view_mode_combo == NULL || view_date_entry == NULL) {
        return TRUE;
    }

    int view_mode = gtk_combo_box_get_active(GTK_COMBO_BOX(view_mode_combo));

    /*
     * All view:
     * show every task.
     */
    if (view_mode == 0) {
        return TRUE;
    }

    const char *view_date = gtk_entry_get_text(GTK_ENTRY(view_date_entry));

    /*
     * If the user chooses Day or Month view but has not entered
     * a complete date yet, show nothing.
     */
    if (strlen(view_date) != 10) {
        return FALSE;
    }

    char *task_date = NULL;

    /*
     * Read the Date column from the current task row.
     */
    gtk_tree_model_get(
        model,
        iter,
        COL_DATE, &task_date,
        -1
    );

    if (task_date == NULL) {
        return FALSE;
    }

    gboolean visible = FALSE;
    
    if (view_mode == 1) {
        /*
         * Day view:
         * the whole date must match.
         */
        visible = strcmp(task_date, view_date) == 0;
    } else if (view_mode == 2) {
        /*
         * Week view:
         * show tasks in the same Monday-to-Sunday week
         * as the view date.
         */
        visible = is_same_week(task_date, view_date);
    } else if (view_mode == 3) {
        /*
         * Month view:
         * only compare YYYY-MM.
         */
        visible = strncmp(task_date, view_date, 7) == 0;
    }
 
    g_free(task_date);
    return visible;
}

/*
 * Re-apply the current view filter.
 *
 * We call this when:
 * - the user changes View mode
 * - the user changes View date
 * - the user clicks Apply View
 */
static void refresh_view(void) {
    if (view_date_entry != NULL) {
        format_date_entry(view_date_entry);
    }

    if (filter_model != NULL) {
        gtk_tree_model_filter_refilter(filter_model);
    }
}

/*
 * This function runs when the user changes the view controls.
 */
static void on_view_changed(GtkWidget *widget, gpointer data) {
    (void)widget;
    (void)data;

    refresh_view();
    set_status("View updated.");
}

/*
 * This function runs when the user leaves the time input box.
 *
 * Example:
 * The user types 1430, then clicks Category.
 * The time box will become 14:30.
 */
static gboolean on_time_focus_out(GtkWidget *widget, GdkEvent *event, gpointer data) {
    (void)event;
    (void)data;

    format_time_entry(widget);

    return FALSE;
}

/*
 * Convert a date string in YYYY-MM-DD format into a weekday name.
 *
 * Example:
 * 2026-05-21 -> Thursday
 *
 * If the date is invalid, return an empty string.
 *
 * We return constant strings such as "Monday".
 * Therefore, the caller does not need to free the result.
 */
static const char *get_weekday_name(const char *date_text) {
    if (date_text == NULL || strlen(date_text) != 10) {
        return "";
    }

    int year;
    int month;
    int day;

    if (sscanf(date_text, "%d-%d-%d", &year, &month, &day) != 3) {
        return "";
    }

    if (!g_date_valid_dmy(day, month, year)) {
        return "";
    }

    GDate date;
    g_date_clear(&date, 1);
    g_date_set_dmy(&date, day, month, year);

    GDateWeekday weekday = g_date_get_weekday(&date);

    switch (weekday) {
        case G_DATE_MONDAY:
            return "Monday";
        case G_DATE_TUESDAY:
            return "Tuesday";
        case G_DATE_WEDNESDAY:
            return "Wednesday";
        case G_DATE_THURSDAY:
            return "Thursday";
        case G_DATE_FRIDAY:
            return "Friday";
        case G_DATE_SATURDAY:
            return "Saturday";
        case G_DATE_SUNDAY:
            return "Sunday";
        default:
            return "";
    }
}

/*
 * Decide the text color of a task based on its category.
 *
 * Current rule:
 * - Important / Urgent / Deadline tasks are shown in orange.
 * - Other tasks use the default text color.
 *
 * The return value is a constant string.
 * The caller does not need to free it.
 */
static const char *get_task_color(const char *category) {
    if (category == NULL) {
        return "black";
    }

    /*
     * g_ascii_strcasecmp() compares strings without caring about case.
     *
     * Example:
     * "important", "Important", and "IMPORTANT"
     * are treated as the same word.
     */
    if (
        g_ascii_strcasecmp(category, "Important") == 0 ||
        g_ascii_strcasecmp(category, "Urgent") == 0 ||
        g_ascii_strcasecmp(category, "Deadline") == 0
    ) {
        return "orange";
    }

    return "black";
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
     * Build a hidden sort key from date and time.
     *
     * Example:
     * date = "2026-05-21"
     * time = "14:30"
     * sort_key = "2026-05-21 14:30"
     *
     * If time is empty, the key becomes:
     * "2026-05-21 "
     *
     * This means tasks without a time will appear before timed tasks
     * on the same date.
     */
    char *sort_key = g_strdup_printf("%s %s", date, time);
    /*
     * Calculate weekday from the date.
     * This is displayed in the table but not saved separately.
     * When the app loads tasks from plans.txt, it calculates weekday again.
     */
    const char *weekday = get_weekday_name(date);

    /*
     * Decide the display color for this task.
     * Important / Urgent / Deadline tasks will be orange.
     */
    const char *color = get_task_color(category);

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
	COL_WEEKDAY, weekday,
        COL_TIME, time,
        COL_CATEGORY, category,
        COL_TITLE, title,
        COL_NOTE, note,
        COL_DONE, done ? "Yes" : "No",
	COL_SORT_KEY, sort_key,
	COL_COLOR, color,
        -1
    );

    g_free(sort_key);

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
 * Save one deleted task into deleted_plans.txt.
 *
 * This is our simple "trash" system.
 * When the user deletes a task, we do not lose it immediately.
 * Instead, we append it to deleted_plans.txt.
 *
 * The format is the same as plans.txt:
 * date|time|category|title|note|done
 */
static void save_deleted_task(
    const char *date,
    const char *time,
    const char *category,
    const char *title,
    const char *note,
    const char *done_text
) {
    /*
     * Open deleted_plans.txt in append mode.
     *
     * "a" means:
     * - create the file if it does not exist
     * - add new content at the end
     * - do not overwrite old deleted tasks
     */
    FILE *fp = fopen(DELETED_FILE, "a");

    if (fp == NULL) {
        set_status("Could not save deleted task.");
        return;
    }

    /*
     * Clean fields before saving.
     * This avoids breaking the | separated file format.
     */
    char *clean_date = clean_field(date);
    char *clean_time = clean_field(time);
    char *clean_category = clean_field(category);
    char *clean_title = clean_field(title);
    char *clean_note = clean_field(note);

    /*
     * In the table, done is stored as "Yes" or "No".
     * In the file, we store it as 1 or 0.
     */
    int done = (done_text != NULL && strcmp(done_text, "Yes") == 0) ? 1 : 0;

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

    fclose(fp);

    g_free(clean_date);
    g_free(clean_time);
    g_free(clean_category);
    g_free(clean_title);
    g_free(clean_note);
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
 * Get the selected row from the visible table
 * and convert it back to the real row in the store.
 *
 * Why do we need this?
 *
 * After adding Day / Month views, the tree_view no longer displays
 * the store directly. It displays filter_model.
 *
 * So when the user selects a visible row, that row belongs to filter_model.
 * But editing, deleting and saving must happen in the real store.
 */
static gboolean get_selected_store_iter(GtkTreeIter *store_iter) {
    GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(tree_view));
    GtkTreeModel *selected_model;
    GtkTreeIter filter_iter;

    if (!gtk_tree_selection_get_selected(selection, &selected_model, &filter_iter)) {
        return FALSE;
    }

    gtk_tree_model_filter_convert_iter_to_child_iter(
        filter_model,
        store_iter,
        &filter_iter
    );

    return TRUE;
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

    /*
     * Format the date and time before reading it.
     * This is useful if the user clicks Add directly
     * without first leaving the date input box.
     */
    gboolean date_ok = format_date_entry(date_entry);
    gboolean time_ok = format_time_entry(time_entry);

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

    if (!date_ok || strlen(date) != 10) {
        set_status("Please enter a complete date, for example 20260522.");
        return;
    }

    /*
     * Time is optional.
     * But if the user enters a time, it must be complete and valid.
     *
     * Valid examples:
     * 0930 -> 09:30
     * 1430 -> 14:30
     * 2359 -> 23:59
     */
    if (strlen(time) > 0 && !time_ok) {
        set_status("Please enter a valid time, for example 1430.");
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

    GtkTreeModel *model = GTK_TREE_MODEL(store);
    GtkTreeIter iter;

    if (get_selected_store_iter(&iter)) {
        char *done_text = NULL;

        gtk_tree_model_get(
            model,
            &iter,
            COL_DONE, &done_text,
            -1
        );

        if (done_text != NULL && strcmp(done_text, "Yes") == 0) {
            gtk_list_store_set(store, &iter, COL_DONE, "No", -1);
            set_status("Task marked as unfinished.");
        } else {
            gtk_list_store_set(store, &iter, COL_DONE, "Yes", -1);
            set_status("Task marked as done.");
        }

        g_free(done_text);
        save_tasks();
        refresh_view();
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

    GtkTreeModel *model = GTK_TREE_MODEL(store);
    GtkTreeIter iter;

    if (get_selected_store_iter(&iter)) {
        char *date;
        char *time;
        char *category;
        char *title;
        char *note;

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

        gtk_entry_set_text(GTK_ENTRY(date_entry), date);
        gtk_entry_set_text(GTK_ENTRY(time_entry), time);
        gtk_entry_set_text(GTK_ENTRY(category_entry), category);
        gtk_entry_set_text(GTK_ENTRY(title_entry), title);
        gtk_entry_set_text(GTK_ENTRY(note_entry), note);

        if (editing_row_ref != NULL) {
            gtk_tree_row_reference_free(editing_row_ref);
            editing_row_ref = NULL;
        }

        GtkTreePath *path = gtk_tree_model_get_path(model, &iter);
        editing_row_ref = gtk_tree_row_reference_new(model, path);
        gtk_tree_path_free(path);

        set_status("Editing selected task. Modify the fields, then click Update Task.");

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

    /*
     * Format date and time before updating the selected task.
     */
    gboolean date_ok = format_date_entry(date_entry);
    gboolean time_ok = format_time_entry(time_entry);

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

    /*
     * A complete formatted date should have 10 characters:
     * YYYY-MM-DD
     */
    if (!date_ok || strlen(date) != 10) {
        set_status("Please enter a complete date, for example 20260522.");
        return;
    }

    if (strlen(time) > 0 && !time_ok) {
        set_status("Please enter a valid time, for example 1430.");
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
         * Rebuild the hidden sort key after editing date or time.
	 * Otherwise, the visible date/time changes,
	 * but the sorting order may still use the old value.
	 */
	char *sort_key = g_strdup_printf("%s %s", date, time);
	/*
         * Recalculate weekday after editing the date.
         */
        const char *weekday = get_weekday_name(date);
	/*
         * Recalculate color after editing the category.
         */
        const char *color = get_task_color(category);

	gtk_list_store_set(
	    GTK_LIST_STORE(model),
	    &iter,
	    COL_DATE, date,
	    COL_WEEKDAY, weekday,
	    COL_TIME, time,
	    COL_CATEGORY, category,
	    COL_TITLE, title,
	    COL_NOTE, note,
	    COL_SORT_KEY, sort_key,
	    COL_COLOR, color,
	    -1
	);

        g_free(sort_key);

        save_tasks();
	refresh_view();
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

/*
 * This function runs when the user clicks "Delete Selected".
 *
 * New behaviour:
 * The task is not permanently lost.
 * Before removing it from the visible list, we save it into deleted_plans.txt.
 *
 * This gives the user a chance to restore it later.
 */

static void on_delete_clicked(GtkWidget *widget, gpointer data) {
    (void)widget;
    (void)data;

    GtkTreeModel *model = GTK_TREE_MODEL(store);
    GtkTreeIter iter;

    if (get_selected_store_iter(&iter)) {
        char *date;
        char *time;
        char *category;
        char *title;
        char *note;
        char *done_text;

        gtk_tree_model_get(
            model,
            &iter,
            COL_DATE, &date,
            COL_TIME, &time,
            COL_CATEGORY, &category,
            COL_TITLE, &title,
            COL_NOTE, &note,
            COL_DONE, &done_text,
            -1
        );

        save_deleted_task(date, time, category, title, note, done_text);

        gtk_list_store_remove(store, &iter);

        if (editing_row_ref != NULL) {
            gtk_tree_row_reference_free(editing_row_ref);
            editing_row_ref = NULL;
        }

        save_tasks();
        refresh_view();

        set_status("Task moved to trash. You can restore it with Restore Last Deleted.");

        g_free(date);
        g_free(time);
        g_free(category);
        g_free(title);
        g_free(note);
        g_free(done_text);
    } else {
        set_status("Please select a task first.");
    }
}

/*
 * This function runs when the user clicks "Restore Last Deleted".
 *
 * It restores the most recently deleted task from deleted_plans.txt.
 *
 * How it works:
 * 1. Read all lines from deleted_plans.txt.
 * 2. Find the last non-empty line.
 * 3. Convert that line back into a task.
 * 4. Add the task back to the visible list.
 * 5. Rewrite deleted_plans.txt without that restored line.
 */
static void on_restore_clicked(GtkWidget *widget, gpointer data) {
    (void)widget;
    (void)data;

    char *contents = NULL;
    gsize length = 0;
    GError *error = NULL;

    /*
     * Read the whole deleted_plans.txt file into memory.
     */
    if (!g_file_get_contents(DELETED_FILE, &contents, &length, &error)) {
        if (error != NULL) {
            g_error_free(error);
        }

        set_status("No deleted tasks to restore.");
        return;
    }

    /*
     * Split the file into lines.
     */
    char **lines = g_strsplit(contents, "\n", -1);

    /*
     * Find the last non-empty line.
     * This represents the most recently deleted task.
     */
    int last_index = -1;

    for (int i = 0; lines[i] != NULL; i++) {
        if (strlen(lines[i]) > 0) {
            last_index = i;
        }
    }

    if (last_index == -1) {
        g_strfreev(lines);
        g_free(contents);
        set_status("No deleted tasks to restore.");
        return;
    }

    /*
     * Split the last deleted task into fields.
     */
    char **parts = g_strsplit(lines[last_index], "|", 6);

    if (
        parts[0] != NULL &&
        parts[1] != NULL &&
        parts[2] != NULL &&
        parts[3] != NULL &&
        parts[4] != NULL &&
        parts[5] != NULL
    ) {
        int done = strcmp(parts[5], "1") == 0;

        /*
         * Add the deleted task back to the visible list.
         */
        append_task(parts[0], parts[1], parts[2], parts[3], parts[4], done);

        /*
         * Save active tasks again, because the restored task is now back.
         */
        save_tasks();

        set_status("Last deleted task restored.");
    } else {
        set_status("Could not restore deleted task.");
    }

    g_strfreev(parts);

    /*
     * Rewrite deleted_plans.txt without the restored line.
     */
    GString *new_contents = g_string_new("");

    for (int i = 0; lines[i] != NULL; i++) {
        if (i == last_index || strlen(lines[i]) == 0) {
            continue;
        }

        g_string_append(new_contents, lines[i]);
        g_string_append_c(new_contents, '\n');
    }

    if (!g_file_set_contents(DELETED_FILE, new_contents->str, -1, &error)) {
        if (error != NULL) {
            g_error_free(error);
        }

        set_status("Task restored, but could not update deleted_plans.txt.");
    }

    g_string_free(new_contents, TRUE);
    g_strfreev(lines);
    g_free(contents);
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

/*
 * Add one visible text column to the task table.
 *
 * Besides the text itself, this function also connects
 * the renderer's foreground color to COL_COLOR.
 *
 * This means every visible column in the same row
 * can use the row's color.
 */
static void add_column(const char *title, int column_id) {
    GtkCellRenderer *renderer = gtk_cell_renderer_text_new();

    GtkTreeViewColumn *column = gtk_tree_view_column_new_with_attributes(
        title,
        renderer,
        "text",
        column_id,
        "foreground",
        COL_COLOR,
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
    /*
     * Format the date when the user leaves the date input box.
     * This is safer than formatting on every key press.
     */
    g_signal_connect(date_entry, "focus-out-event", G_CALLBACK(on_date_focus_out), NULL);
    /*
     * Format the time when the user leaves the time input box.
     * This avoids confusion between English colon : and Chinese colon ：.
     */
    g_signal_connect(time_entry, "focus-out-event", G_CALLBACK(on_time_focus_out), NULL);

    gtk_entry_set_placeholder_text(GTK_ENTRY(date_entry), "Type 20260522");    
    gtk_entry_set_placeholder_text(GTK_ENTRY(time_entry), "Type 1430");
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

    /*
     * View control area.
     *
     * This lets the user choose whether to see:
     * - all tasks
     * - tasks for one day
     * - tasks for one month
     */
    GtkWidget *view_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_pack_start(GTK_BOX(main_box), view_box, FALSE, FALSE, 0);

    GtkWidget *view_label = gtk_label_new("View:");
    view_mode_combo = gtk_combo_box_text_new();

    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(view_mode_combo), "All");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(view_mode_combo), "Day");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(view_mode_combo), "Week");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(view_mode_combo), "Month");

    gtk_combo_box_set_active(GTK_COMBO_BOX(view_mode_combo), 0);

    GtkWidget *view_date_label = gtk_label_new("View date:");
    view_date_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(view_date_entry), "Type 20260521");

    GtkWidget *apply_view_button = gtk_button_new_with_label("Apply View");
    GtkWidget *previous_button = gtk_button_new_with_label("Previous");
    GtkWidget *next_button = gtk_button_new_with_label("Next");

    gtk_box_pack_start(GTK_BOX(view_box), view_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(view_box), view_mode_combo, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(view_box), view_date_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(view_box), view_date_entry, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(view_box), apply_view_button, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(view_box), previous_button, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(view_box), next_button, FALSE, FALSE, 0);

    g_signal_connect(view_mode_combo, "changed", G_CALLBACK(on_view_changed), NULL);
    g_signal_connect(apply_view_button, "clicked", G_CALLBACK(on_view_changed), NULL);
    g_signal_connect(previous_button, "clicked", G_CALLBACK(on_previous_clicked), NULL);
    g_signal_connect(next_button, "clicked", G_CALLBACK(on_next_clicked), NULL);
    g_signal_connect(view_date_entry, "focus-out-event", G_CALLBACK(on_date_focus_out), NULL);

    GtkWidget *button_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_pack_start(GTK_BOX(main_box), button_box, FALSE, FALSE, 0);

    GtkWidget *add_button = gtk_button_new_with_label("Add Task");
    GtkWidget *edit_button = gtk_button_new_with_label("Edit Selected");
    GtkWidget *update_button = gtk_button_new_with_label("Update Task");
    GtkWidget *done_button = gtk_button_new_with_label("Toggle Done");
    GtkWidget *delete_button = gtk_button_new_with_label("Delete Selected");
    GtkWidget *restore_button = gtk_button_new_with_label("Restore Last Deleted");
    GtkWidget *save_button = gtk_button_new_with_label("Save");

    gtk_box_pack_start(GTK_BOX(button_box), add_button, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(button_box), edit_button, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(button_box), update_button, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(button_box), done_button, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(button_box), delete_button, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(button_box), restore_button, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(button_box), save_button, FALSE, FALSE, 0);

    /*
     * Create the data model for the task table.
     *
     * There are 9 columns in the model:
     * 7 visible columns and 2 hidden sort key column.
     */
    store = gtk_list_store_new(
        N_COLS,
        G_TYPE_STRING,  // COL_DATE
	G_TYPE_STRING,  // COL_WEEKDAY
        G_TYPE_STRING,  // COL_TIME
        G_TYPE_STRING,  // COL_CATEGORY
        G_TYPE_STRING,  // COL_TITLE
        G_TYPE_STRING,  // COL_NOTE
        G_TYPE_STRING,  // COL_DONE
        G_TYPE_STRING,   // COL_SORT_KEY, hidden
	G_TYPE_STRING   // COL_COLOR, hidden
    );

    /*
     * Create a filter model on top of the real store.
     *
     * The store keeps all tasks.
     * The filter_model decides which tasks are visible.
     */
    filter_model = GTK_TREE_MODEL_FILTER(
        gtk_tree_model_filter_new(GTK_TREE_MODEL(store), NULL)
    );

    gtk_tree_model_filter_set_visible_func(
        filter_model,
        task_visible_func,
        NULL,
        NULL
    );

    /*
     * The tree view displays the filter model, not the store directly.
     */
    tree_view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(filter_model));
    add_column("Date", COL_DATE);
    add_column("Weekday", COL_WEEKDAY);
    add_column("Time", COL_TIME);
    add_column("Category", COL_CATEGORY);
    add_column("Title", COL_TITLE);
    add_column("Note", COL_NOTE);
    add_column("Done", COL_DONE);
    
    /*
     * Sort the task list automatically by the hidden sort key.
     *
     * Since the sort key is built as:
     * YYYY-MM-DD HH:MM
     *
     * GTK_SORT_ASCENDING means earlier tasks appear first.
     */
    gtk_tree_sortable_set_sort_column_id(
        GTK_TREE_SORTABLE(store),
        COL_SORT_KEY,
        GTK_SORT_ASCENDING
    );

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
    g_signal_connect(restore_button, "clicked", G_CALLBACK(on_restore_clicked), NULL);
    g_signal_connect(save_button, "clicked", G_CALLBACK(on_save_clicked), NULL);
    g_signal_connect(window, "destroy", G_CALLBACK(on_window_destroy), NULL);

    load_tasks();

    gtk_widget_show_all(window);

    /*
     * After the window is shown, check whether we should remind the user
     * to write a diary.
     */
    show_diary_prompt_if_needed(GTK_WINDOW(window));

    gtk_main();

    return 0;
}
