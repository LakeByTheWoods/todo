
#define _XOPEN_SOURCE_EXTENDED 1
#define NCURSES_WIDECHAR 1

#include <sys/types.h>
#include <dirent.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <assert.h>
#include <locale.h>
#include <wchar.h>

/* ncurses */
#include <ncurses.h>

#define ARRAY_COUNT(a) (sizeof(a)/sizeof(*a))

enum Todo_State
{
    State_Not_Started   = 0,
    State_Priority      = 1,
    State_Doing         = 2,
    State_Done          = 3,
};

struct Todo_List
{
    struct Todo_List *next;
    time_t time_added;
    time_t time_started;
    time_t time_complete;
    enum Todo_State state;
    wchar_t *text;
};

static void todo_list_save(char *file_path, struct Todo_List *list)
{
    FILE *file = fopen(file_path, "w");

    while (list)
    {
        fwprintf(file, L"%X %X %X %X %ls\n", list->time_added, list->time_started, list->time_complete, list->state, list->text);
        list = list->next;
    }

    fclose(file);
}

static struct Todo_List *todo_list_load(char *file_path)
{
    int fd = open(file_path, O_RDONLY);
    assert(fd != -1);

    off_t offset_end = lseek(fd, 0, SEEK_END);
    assert(offset_end != -1);
    lseek(fd, 0, SEEK_SET);

    char *file_contents = calloc(offset_end + 1, 1);
    read(fd, file_contents, offset_end);

    struct Todo_List *result;
    struct Todo_List **next = &result;

    wchar_t *start_ptr = calloc(offset_end + 1, sizeof (wchar_t));
    swprintf(start_ptr, offset_end + 1, L"%s", file_contents);
    wchar_t *save_ptr = NULL;
    do
    {
        wchar_t *line = wcstok(start_ptr, L"\n", &save_ptr);
        start_ptr = NULL;
        if (! line) break;

        time_t time_added = wcstoll(line, &line, 16);
        time_t time_started = wcstoll(line, &line, 16);
        time_t time_complete = wcstoll(line, &line, 16);
        long long statell = wcstoll(line, &line, 10);

        enum Todo_State state = (enum Todo_State)statell;

        line += wcsspn(line, L" "); // reject spaces until actual text

        *next = calloc(1, sizeof (struct Todo_List));
        **next = 
            (struct Todo_List)
            {
                .time_added = time_added,
                .time_started = time_started,
                .time_complete = time_complete,
                .state = state,
                .text = wcsdup(line),
            };
        next = &(*next)->next;
    } while (1);

    free(file_contents);
    close(fd);

    return result;
}

struct Todo_List_View
{
    struct Todo_List *listing;
    int selection_index;
    int scrolling;
};

static void todo_list_free(struct Todo_List *listing)
{
    while (listing)
    {
        struct Todo_List *to_free = listing;
        listing = listing->next;
        free(to_free);
    }
}

static int todo_list_count(struct Todo_List *listing)
{
    int count = 0;
    while (listing)
    {
        count++;
        listing = listing->next;
    }
    return count;
}

static _Bool s_enable_unicode = false;

int get_week_number(struct tm *tm)
{
    int prev_sunday = tm->tm_yday - tm->tm_wday;
    int week_count = prev_sunday / 7;
    int first_week_length = prev_sunday % 7;
    if (first_week_length)
    {
        week_count += 1;
    }
    return week_count;
}

static void draw_todo_list_view_to_window(WINDOW *window, struct Todo_List_View *lview)
{
    int draw_x = 0,
        draw_y = 0,
        list_index = 0;

    int max_x, max_y; (void)max_y;
    getmaxyx(window, max_y, max_x);
    wchar_t *space = calloc(max_x, sizeof (*space));
    for (int i = 0; i < max_x; ++i)
    {
        wcscat(space, L" ");
    }

    wchar_t scratch_buffer[64];
    struct tm week_tracker_tm = {0};
    struct Todo_List *listing = lview->listing;
    while (listing)
    {
#define IS_SELECTED (lview->selection_index == list_index)
        if (list_index >= lview->scrolling)
        {
            struct tm tm;
            gmtime_r(&listing->time_added, &tm);
            if (get_week_number(&tm) > get_week_number(&week_tracker_tm))
            {
                week_tracker_tm = tm;
                week_tracker_tm.tm_yday -= week_tracker_tm.tm_wday;
                week_tracker_tm.tm_wday = 0;

                size_t date_string_length = wcsftime(scratch_buffer,
                                                     ARRAY_COUNT(scratch_buffer),
                                                     L" %Y %m %d .. ",
                                                     &week_tracker_tm);
                mvwaddnwstr(window, draw_y, draw_x, scratch_buffer, date_string_length);
                draw_x += date_string_length;


                time_t temp_time = mktime(&week_tracker_tm);
                temp_time += 60 * 60 * 24 * 7; // Add one week

                struct tm temp_tm;
                gmtime_r(&temp_time, &temp_tm);

                date_string_length        = wcsftime(scratch_buffer,
                                                     ARRAY_COUNT(scratch_buffer),
                                                     L"%d",
                                                     &temp_tm);
                mvwaddnwstr(window, draw_y, draw_x, scratch_buffer, date_string_length);
                draw_x += date_string_length;

                draw_y += 1;
                draw_x = 0;
                // Clear whole line
                mvwaddnwstr(window, draw_y, 0, space, max_x);
            }

            if (IS_SELECTED) wattron(window, WA_STANDOUT);

            // Clear whole line
            mvwaddnwstr(window, draw_y, 0, space, max_x);

            if (IS_SELECTED)
            {
                mvwaddnwstr(window, draw_y, draw_x, s_enable_unicode ? L"\uF00C    " : L" >  ", 4);
            }
            else
            {
                mvwaddnwstr(window, draw_y, draw_x, L"    ", 4);
            }
            draw_x += 4;

            size_t day_string_length = wcsftime(scratch_buffer,
                                                 ARRAY_COUNT(scratch_buffer),
                                                 L"%a ",
                                                 &tm);
            mvwaddnwstr(window, draw_y, draw_x, scratch_buffer, day_string_length);
            draw_x += day_string_length;


            switch (listing->state)
            {
                case State_Not_Started:
                {
                    wattron(window, COLOR_PAIR(1));
                    mvwaddnwstr(window, draw_y, draw_x, s_enable_unicode ? L"\uF00C  " : L" . ", 3);
                    wattroff(window, COLOR_PAIR(1));
                    break;
                }
                case State_Priority:
                {
                    wattron(window, COLOR_PAIR(2));
                    mvwaddnwstr(window, draw_y, draw_x, s_enable_unicode ? L"\uF00C  " : L" ! ", 3);
                    wattroff(window, COLOR_PAIR(2));
                    break;
                }
                case State_Doing:
                {
                    wattron(window, COLOR_PAIR(4));
                    mvwaddnwstr(window, draw_y, draw_x, s_enable_unicode ? L"\uF00C  " : L" O ", 3);
                    wattroff(window, COLOR_PAIR(4));
                    break;
                }
                case State_Done:
                {
                    wattron(window, COLOR_PAIR(5));
                    mvwaddnwstr(window, draw_y, draw_x, s_enable_unicode ? L"\uF00C  " : L" X ", 3);
                    wattroff(window, COLOR_PAIR(5));
                    break;
                }
            }

            draw_x += 3;

            //mvwaddwstr(window, draw_y, draw_x, L"Hello, \uF00C World!");
            mvwaddwstr(window, draw_y, draw_x, listing->text);
            if (lview->selection_index == list_index) wattroff(window, WA_STANDOUT);
            ++draw_y;
            draw_x = 0;
        }
#undef IS_SELECTED
#undef SHRINK_ON_SELECTION
        listing = listing->next;
        ++list_index;
    }

    free (space);
}

static struct Todo_List *todo_list_view_get_selected(struct Todo_List_View *view)
{
    struct Todo_List *selected = view->listing;
    for (int i = view->selection_index; i; --i)
    {
        selected = selected->next;
    }
    return selected;
}

int main(int argc, char *const *argv)
{
    setlocale(LC_ALL, "");
    
    _Bool enter_graphical_mode = false;
    if (argc != 1)
    { // There's args to parse
        int option;
        while ((option = getopt(argc, argv, "+ugr::f::")) != -1)
        {
            if (option == 'u')
            {
                s_enable_unicode = true;
                enter_graphical_mode = true;
            }

            if (option == 'g')
            {
                enter_graphical_mode = true;
            }

            if (option == 'r')
            {
                // TODO: report
            }

            if (option == 'f')
            {
                // TODO: file name
            }
        }
    }
    else
    {
        enter_graphical_mode = true;
    }

    struct Todo_List *global_listing = todo_list_load("todolist");
    {
        struct Todo_List *last = global_listing;
        while (last->next) last = last->next;
        struct Todo_List **next = &last->next;
        for (int i = optind; i < argc; ++i)
        {
            struct timespec tp;
            clock_gettime(CLOCK_REALTIME_COARSE, &tp);
            time_t t = tp.tv_sec;

            wchar_t *text = calloc(strlen(argv[i]) + 1, sizeof (wchar_t));
            mbstowcs(text, argv[i], strlen(argv[i]) + 1);

            *next = calloc(1, sizeof (struct Todo_List));
            **next = 
                (struct Todo_List)
                {
                    .time_added = t,
                    .time_started = 0,
                    .time_complete = 0,
                    .state = State_Not_Started,
                    .text = text,
                };
            next = &(*next)->next;
        }
    }

    if (! enter_graphical_mode)
    {
        goto save_and_quit;
    }

    initscr();   // start up ncurses
    cbreak();    // don't wait for return when getting chars
    noecho();    // don't echo keypresses
    nonl();      // we wan't to be able to detect the return key
    clear();     // make sure screen is cleared because some implementations don't do it automatically
    curs_set(0); // set cursor invisible
    start_color();
    init_pair(1, COLOR_WHITE, COLOR_BLACK);
    init_pair(2, COLOR_RED, COLOR_BLACK);
    init_pair(4, COLOR_CYAN, COLOR_BLACK);
    init_pair(5, COLOR_GREEN, COLOR_BLACK);

    intrflush(stdscr, FALSE);
    keypad(stdscr, TRUE); // get special keys
    idlok(stdscr, true);
    leaveok(stdscr, true);
    scrollok(stdscr, true); // enable window scrolling
    wtimeout(stdscr, 100);

    {
        struct Todo_List_View *lview = calloc(1, sizeof(*lview));
        lview->listing = global_listing;

        {
            int window_width, window_height; (void)window_width;
            wint_t ch = 0;
            do
            {
                getmaxyx(stdscr, window_height, window_width);
                if (ch == KEY_UP)
                {
                    --lview->selection_index;
                    if (lview->selection_index < lview->scrolling)
                    {
                        if (lview->scrolling == 0)
                        {
                            lview->selection_index = 0;
                        }
                        else
                        {
                            --lview->scrolling;
                        }
                    }
                }
                if (ch == KEY_DOWN)
                {
                    ++lview->selection_index;
                    if (lview->selection_index >= todo_list_count(lview->listing))
                    {
                        lview->selection_index = todo_list_count(lview->listing) - 1;
                    }
                    if (lview->selection_index - lview->scrolling >= window_height)
                    {
                        ++lview->scrolling;
                    }
                }
                if (ch == KEY_LEFT)
                {
                    struct Todo_List *selected = todo_list_view_get_selected(lview);
                    selected->state = State_Not_Started;
                }
                if (ch == KEY_RIGHT)
                {
                    struct Todo_List *item = todo_list_view_get_selected(lview);
                    switch (item->state)
                    {
                        case State_Not_Started:
                        case State_Priority:
                        {
                            item->state = State_Doing;

                            struct timespec tp;
                            clock_gettime(CLOCK_REALTIME_COARSE, &tp);
                            item->time_started = tp.tv_sec;
                            break;
                        }

                        case State_Doing:
                        {
                            item->state = State_Done;

                            struct timespec tp;
                            clock_gettime(CLOCK_REALTIME_COARSE, &tp);
                            item->time_complete = tp.tv_sec;
                            break;
                        }

                        case State_Done:
                            break;
                    }
                }
                if (ch == '!')
                {
                    todo_list_view_get_selected(lview)->state = State_Priority;
                }
                werase(stdscr);
                wrefresh(stdscr);
                draw_todo_list_view_to_window(stdscr, lview);
            } while (get_wch(&ch), ch != 'q');
            free(lview);
        }
    }

    endwin();

save_and_quit:
    {
        todo_list_save("todolist", global_listing);
    }
    todo_list_free(global_listing);
    return 0;
}
